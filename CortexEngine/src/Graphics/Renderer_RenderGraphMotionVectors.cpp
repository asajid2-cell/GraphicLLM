#include "Renderer.h"

#include "Passes/MotionVectorPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

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
    graphContext.computeVisibilityBufferMotion = [&]() {
        m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
        states.visibility = kScreenSpaceShaderResourceState;
        m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

        auto mvResult = m_services.visibilityBuffer->ComputeMotionVectors(
            m_commandResources.graphicsList.Get(),
            m_temporalScreenState.velocityBuffer.Get(),
            m_visibilityBufferState.meshDraws,
            m_constantBuffers.currentFrameGPU);
        if (mvResult.IsErr()) {
            motionStageError = mvResult.Error();
            return false;
        }
        return true;
    };
    graphContext.drawCameraMotion = [&]() {
        m_depthResources.resources.resourceState = kDepthSampleState;
        m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        RenderMotionVectors();
        return true;
    };
    graphContext.failStage = [&](const char* stage) {
        motionStageFailed = true;
        motionStageError = stage ? stage : "unknown";
    };

    RGResourceHandle motionResult{};
    if (useVisibilityBufferMotion) {
        motionResult = MotionVectorPass::AddToGraph(*m_services.renderGraph, graphContext);
    } else {
        if (!m_depthResources.resources.buffer || !m_pipelineState.motionVectors) {
            m_services.renderGraph->EndFrame();
            result.fallbackUsed = true;
            result.fallbackReason = "render_graph_motion_vectors_camera_prerequisites_missing";
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
