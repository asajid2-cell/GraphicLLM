#include "Renderer.h"

#include "Passes/MotionVectorPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <span>
#include <string>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kScreenSpaceShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::RenderGraphPassResult
Renderer::ExecuteMotionVectorsInRenderGraph() {
    RenderGraphPassResult result{};
    m_frameDiagnostics.contract.motionVectors.planned = true;
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_temporalScreenState.velocityBuffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_motion_vectors_prerequisites_missing";
        return result;
    }

    const bool useVisibilityBufferMotion =
        m_visibilityBufferState.enabled &&
        m_services.visibilityBuffer &&
        m_services.visibilityBuffer->GetVisibilityBuffer() &&
        !m_visibilityBufferState.meshDraws.empty() &&
        !m_visibilityBufferState.instances.empty();
    m_frameDiagnostics.contract.motionVectors.visibilityBufferMotion = useVisibilityBufferMotion;
    m_frameDiagnostics.contract.motionVectors.cameraOnlyFallback = !useVisibilityBufferMotion;

    bool motionStageFailed = false;
    std::string motionStageError;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle velocityHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity");
    RGResourceHandle depthHandle{};
    if (!useVisibilityBufferMotion && m_depthResources.resources.buffer) {
        depthHandle = m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_MotionVectors");
    }

    RGResourceHandle visibilityHandle{};
    VisibilityBufferRenderer::ResourceStateSnapshot vbInitialStates{};
    if (useVisibilityBufferMotion) {
        vbInitialStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        visibilityHandle = m_services.renderGraph->ImportResource(
            m_services.visibilityBuffer->GetVisibilityBuffer(), vbInitialStates.visibility, "VB_Visibility_MotionVectors");
    }

    MotionVectorPass::GraphContext graphContext{};
    graphContext.velocity = velocityHandle;
    graphContext.depth = depthHandle;
    graphContext.visibility = visibilityHandle;
    graphContext.useVisibilityBufferMotion = useVisibilityBufferMotion;
    graphContext.visibilityMotion.renderer = m_services.visibilityBuffer.get();
    graphContext.visibilityMotion.commandList = m_commandResources.graphicsList.Get();
    graphContext.visibilityMotion.velocityBuffer = m_temporalScreenState.velocityBuffer.Get();
    graphContext.visibilityMotion.velocityState = &m_temporalScreenState.velocityState;
    graphContext.visibilityMotion.meshDraws = &m_visibilityBufferState.meshDraws;
    graphContext.visibilityMotion.frameConstants = m_constantBuffers.currentFrameGPU;
    graphContext.visibilityMotion.visibilityShaderResourceState = kScreenSpaceShaderResourceState;
    graphContext.visibilityMotion.error = &motionStageError;
    graphContext.cameraTarget.commandList = m_commandResources.graphicsList.Get();
    graphContext.cameraTarget.velocity = {
        m_temporalScreenState.velocityBuffer.Get(),
        &m_temporalScreenState.velocityState,
    };
    graphContext.cameraTarget.depth = {
        m_depthResources.resources.buffer.Get(),
        &m_depthResources.resources.resourceState,
    };
    graphContext.cameraTarget.depthSampleState = kDepthSampleState;
    graphContext.cameraTarget.skipTransitions = true;
    if (!useVisibilityBufferMotion && m_temporalScreenState.motionVectorSrvTableValid) {
        auto& motionTable = m_temporalScreenState.motionVectorSrvTables[m_frameRuntime.frameIndex % kFrameCount];
        graphContext.cameraDraw.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        graphContext.cameraDraw.commandList = m_commandResources.graphicsList.Get();
        graphContext.cameraDraw.descriptorManager = m_services.descriptorManager.get();
        graphContext.cameraDraw.rootSignature = m_pipelineState.rootSignature.get();
        graphContext.cameraDraw.frameConstants = m_constantBuffers.currentFrameGPU;
        graphContext.cameraDraw.pipeline = m_pipelineState.motionVectors.get();
        graphContext.cameraDraw.target = m_temporalScreenState.velocityBuffer.Get();
        graphContext.cameraDraw.targetRtv = m_temporalScreenState.velocityRTV;
        graphContext.cameraDraw.depth = m_depthResources.resources.buffer.Get();
        graphContext.cameraDraw.srvTable = std::span<DescriptorHandle>(motionTable.data(), motionTable.size());
    }
    graphContext.failStage = [&](const char* stage) {
        motionStageFailed = true;
        motionStageError = stage ? stage : "unknown";
    };

    RGResourceHandle motionResult{};
    if (useVisibilityBufferMotion) {
        motionResult = MotionVectorPass::AddToGraph(*m_services.renderGraph, graphContext);
    } else {
        if (!m_depthResources.resources.buffer || !m_pipelineState.motionVectors || !m_temporalScreenState.motionVectorSrvTableValid) {
            m_services.renderGraph->EndFrame();
            result.fallbackUsed = true;
            result.fallbackReason = !m_temporalScreenState.motionVectorSrvTableValid
                ? "render_graph_motion_vectors_camera_descriptor_table_missing"
                : "render_graph_motion_vectors_camera_prerequisites_missing";
            m_frameDiagnostics.contract.motionVectors.visibilityBufferMotion = false;
            m_frameDiagnostics.contract.motionVectors.cameraOnlyFallback = true;
            return result;
        }

        motionResult = MotionVectorPass::AddToGraph(*m_services.renderGraph, graphContext);
    }
    if (!motionResult.IsValid()) {
        motionStageFailed = true;
        if (motionStageError.empty()) {
            motionStageError = "motion_vectors_graph_contract";
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (motionStageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "motion_vectors_graph_stage_failed: " + motionStageError;
    } else {
        m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        if (depthHandle.IsValid()) {
            m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        }
        if (visibilityHandle.IsValid()) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.visibility = m_services.renderGraph->GetResourceState(visibilityHandle);
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        }
        result.executed = true;
        m_frameDiagnostics.contract.motionVectors.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("MotionVectors RG: {} (graph path did not execute)", result.fallbackReason);
        m_frameDiagnostics.contract.motionVectors.visibilityBufferMotion = false;
        m_frameDiagnostics.contract.motionVectors.cameraOnlyFallback = true;
    }

    return result;
}


} // namespace Cortex::Graphics
