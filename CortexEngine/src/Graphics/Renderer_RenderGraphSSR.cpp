#include "Renderer.h"

#include "Passes/RenderPassScope.h"
#include "Passes/SSRPass.h"
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
Renderer::ExecuteSSRInRenderGraph() {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_pipelineState.ssr || !m_ssrResources.resources.color ||
        !m_mainTargets.hdrColor || !m_depthResources.buffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssr_prerequisites_missing";
        RenderSSR();
        result.executed = true;
        return result;
    }

    ID3D12Resource* normalResource = m_mainTargets.gbufferNormalRoughness.Get();
    D3D12_RESOURCE_STATES normalState = m_mainTargets.gbufferNormalRoughnessState;
    const bool usesVBNormal =
        m_visibilityBufferState.renderedThisFrame &&
        m_services.visibilityBuffer &&
        m_services.visibilityBuffer->GetNormalRoughnessBuffer();

    VisibilityBufferRenderer::ResourceStateSnapshot vbInitialStates{};
    if (usesVBNormal) {
        vbInitialStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
        normalState = vbInitialStates.normalRoughness;
    }

    if (!normalResource) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssr_normal_resource_missing";
        RenderSSR();
        result.executed = true;
        return result;
    }

    bool stageFailed = false;
    std::string stageError;
    auto failStage = [&](const char* stage) {
        if (!stageFailed) {
            stageError = stage ? stage : "ssr_graph_stage_failed";
        }
        stageFailed = true;
    };

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdrColor.Get(), m_mainTargets.hdrState, "HDR_SSR");
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_SSR");
    const RGResourceHandle normalHandle =
        m_services.renderGraph->ImportResource(normalResource, normalState, usesVBNormal ? "VB_NormalRoughness_SSR" : "NormalRoughness_SSR");
    const RGResourceHandle ssrHandle =
        m_services.renderGraph->ImportResource(m_ssrResources.resources.color.Get(), m_ssrResources.resources.resourceState, "SSRColor");

    SSRPass::GraphContext ssrContext{};
    ssrContext.hdr = hdrHandle;
    ssrContext.depth = depthHandle;
    ssrContext.normalRoughness = normalHandle;
    ssrContext.ssr = ssrHandle;
    ssrContext.failStage = failStage;
    ssrContext.execute = [&]() {
        m_mainTargets.hdrState = kScreenSpaceShaderResourceState;
        m_depthResources.resourceState = kDepthSampleState;
        m_ssrResources.resources.resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (usesVBNormal) {
            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.normalRoughness = kScreenSpaceShaderResourceState;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);
        } else {
            m_mainTargets.gbufferNormalRoughnessState = kScreenSpaceShaderResourceState;
        }

        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions, true);
        RenderSSR();
        return static_cast<bool>(m_ssrResources.resources.color);
    };
    (void)SSRPass::AddToGraph(*m_services.renderGraph, ssrContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "ssr_graph_stage_failed: " + stageError;
    } else {
        m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(hdrHandle);
        m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_ssrResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssrHandle);
        if (usesVBNormal) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        } else {
            m_mainTargets.gbufferNormalRoughnessState = m_services.renderGraph->GetResourceState(normalHandle);
        }
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("SSR RG: {} (falling back to legacy path)", result.fallbackReason);
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions, false);
        RenderSSR();
        result.executed = true;
    }

    return result;
}


} // namespace Cortex::Graphics
