#include "Renderer.h"

#include "Passes/MotionVectorPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
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
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_temporal.ScreenState().velocityBuffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_motion_vectors_prerequisites_missing";
        return result;
    }

    const char* vbMotionEnv = std::getenv("CORTEX_ENABLE_VB_MOTION_VECTORS");
    const bool allowVisibilityBufferMotion =
        vbMotionEnv && std::string(vbMotionEnv) == "1";
    const bool useVisibilityBufferMotion =
        allowVisibilityBufferMotion &&
        m_vb.State().enabled &&
        m_services.visibilityBuffer &&
        m_services.visibilityBuffer->GetVisibilityBuffer() &&
        !m_vb.State().meshDraws.empty() &&
        !m_vb.State().instances.empty();
    m_frameDiagnostics.contract.motionVectors.visibilityBufferMotion = useVisibilityBufferMotion;
    m_frameDiagnostics.contract.motionVectors.cameraOnlyFallback = !useVisibilityBufferMotion;

    bool motionStageFailed = false;
    const char* motionStage = nullptr;
    std::string motionStageError;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle velocityHandle =
        m_services.renderGraph->ImportResource(m_temporal.ScreenState().velocityBuffer.Get(), m_temporal.ScreenState().velocityState, "Velocity");
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
    graphContext.visibilityMotion.velocityBuffer = m_temporal.ScreenState().velocityBuffer.Get();
    graphContext.visibilityMotion.velocityState = &m_temporal.ScreenState().velocityState;
    graphContext.visibilityMotion.meshDraws = &m_vb.State().meshDraws;
    graphContext.visibilityMotion.frameConstants = m_constantBuffers.currentFrameGPU;
    graphContext.visibilityMotion.visibilityShaderResourceState = kScreenSpaceShaderResourceState;
    graphContext.visibilityMotion.error = &motionStageError;
    graphContext.status.failed = &motionStageFailed;
    graphContext.status.stage = &motionStage;
    graphContext.cameraTarget.commandList = m_commandResources.graphicsList.Get();
    graphContext.cameraTarget.velocity = {
        m_temporal.ScreenState().velocityBuffer.Get(),
        &m_temporal.ScreenState().velocityState,
    };
    graphContext.cameraTarget.depth = {
        m_depthResources.resources.buffer.Get(),
        &m_depthResources.resources.resourceState,
    };
    graphContext.cameraTarget.depthSampleState = kDepthSampleState;
    graphContext.cameraTarget.skipTransitions = true;
    if (!useVisibilityBufferMotion && m_temporal.ScreenState().motionVectorSrvTableValid) {
        auto& motionTable = m_temporal.ScreenState().motionVectorSrvTables[m_frameRuntime.frameIndex % kFrameCount];
        graphContext.cameraDraw.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        graphContext.cameraDraw.commandList = m_commandResources.graphicsList.Get();
        graphContext.cameraDraw.descriptorManager = m_services.descriptorManager.get();
        graphContext.cameraDraw.rootSignature = m_pipelineState.rootSignature.get();
        graphContext.cameraDraw.frameConstants = m_constantBuffers.currentFrameGPU;
        graphContext.cameraDraw.pipeline = m_pipelineState.motionVectors.get();
        graphContext.cameraDraw.target = m_temporal.ScreenState().velocityBuffer.Get();
        graphContext.cameraDraw.targetRtv = m_temporal.ScreenState().velocityRTV;
        graphContext.cameraDraw.depth = m_depthResources.resources.buffer.Get();
        graphContext.cameraDraw.srvTable = std::span<DescriptorHandle>(motionTable.data(), motionTable.size());
    }
    RGResourceHandle motionResult{};
    if (useVisibilityBufferMotion) {
        motionResult = MotionVectorPass::AddToGraph(*m_services.renderGraph, graphContext);
    } else {
        if (!m_depthResources.resources.buffer || !m_pipelineState.motionVectors || !m_temporal.ScreenState().motionVectorSrvTableValid) {
            m_services.renderGraph->EndFrame();
            result.fallbackUsed = true;
            result.fallbackReason = !m_temporal.ScreenState().motionVectorSrvTableValid
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
            motionStageError = motionStage ? motionStage : "motion_vectors_graph_contract";
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (motionStageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "motion_vectors_graph_stage_failed";
        if (!motionStageError.empty()) {
            result.fallbackReason += ": " + motionStageError;
        } else if (motionStage) {
            result.fallbackReason += ": ";
            result.fallbackReason += motionStage;
        }
    } else {
        m_temporal.ScreenState().velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
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
