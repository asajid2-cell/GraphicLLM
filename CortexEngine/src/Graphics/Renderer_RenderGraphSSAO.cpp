#include "Renderer.h"

#include "Passes/RenderPassScope.h"
#include "Passes/SSAOPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <string>

namespace Cortex::Graphics {

Renderer::RenderGraphPassResult
Renderer::ExecuteSSAOInRenderGraph() {
    RenderGraphPassResult result{};
    const bool useComputeSSAO = m_pipelineState.ssaoCompute && m_frameRuntime.asyncComputeSupported;

    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_ssaoResources.controls.enabled || !m_ssaoResources.resources.texture ||
        !m_depthResources.buffer || !m_depthResources.srv.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssao_prerequisites_missing";
        if (useComputeSSAO) {
            RenderSSAOAsync();
        } else {
            RenderSSAO();
        }
        result.executed = true;
        return result;
    }

    if (useComputeSSAO && !m_ssaoResources.resources.uav.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssao_uav_missing";
        RenderSSAOAsync();
        result.executed = true;
        return result;
    }

    bool stageFailed = false;
    std::string stageError;
    auto failStage = [&](const char* stage) {
        if (!stageFailed) {
            stageError = stage ? stage : "ssao_graph_stage_failed";
        }
        stageFailed = true;
    };

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_SSAO");
    const RGResourceHandle ssaoHandle =
        m_services.renderGraph->ImportResource(m_ssaoResources.resources.texture.Get(), m_ssaoResources.resources.resourceState, "SSAO");

    SSAOPass::GraphContext ssaoContext{};
    ssaoContext.depth = depthHandle;
    ssaoContext.ssao = ssaoHandle;
    ssaoContext.useCompute = useComputeSSAO;
    ssaoContext.failStage = failStage;
    ssaoContext.execute = [&]() {
        m_depthResources.resourceState = kDepthSampleState;
        m_ssaoResources.resources.resourceState = useComputeSSAO
            ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
            : D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.ssaoSkipTransitions, true);
        if (useComputeSSAO) {
            RenderSSAOAsync();
        } else {
            RenderSSAO();
        }
        return static_cast<bool>(m_ssaoResources.resources.texture);
    };
    (void)SSAOPass::AddToGraph(*m_services.renderGraph, ssaoContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "ssao_graph_stage_failed: " + stageError;
    } else {
        m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_ssaoResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssaoHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("SSAO RG: {} (falling back to legacy path)", result.fallbackReason);
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.ssaoSkipTransitions, false);
        if (useComputeSSAO) {
            RenderSSAOAsync();
        } else {
            RenderSSAO();
        }
        result.executed = true;
    }

    return result;
}



} // namespace Cortex::Graphics
