#include "Renderer.h"

#include "Passes/DepthPrepass.h"
#include "Passes/RenderPassScope.h"
#include "Passes/ShadowPass.h"
#include "RenderGraph.h"
#include "Scene/ECS_Registry.h"
#include <spdlog/spdlog.h>
#include <string>

namespace Cortex::Graphics {

Renderer::RenderGraphPassResult
Renderer::ExecuteDepthPrepassInRenderGraph(Scene::ECS_Registry* registry) {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_depthResources.buffer || !m_pipelineState.depthOnly) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_depth_prerequisites_missing";
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions, false);
        RenderDepthPrepass(registry);
        result.executed = true;
        return result;
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth");

    bool stageFailed = false;
    std::string stageError;
    DepthPrepass::GraphContext graphContext{};
    graphContext.depth = depthHandle;
    graphContext.execute = [&]() {
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions, true);
        RenderDepthPrepass(registry);
        return true;
    };
    graphContext.failStage = [&](const char* stage) {
        stageFailed = true;
        stageError = stage ? stage : "unknown";
    };

    const RGResourceHandle depthResult = DepthPrepass::AddToGraph(*m_services.renderGraph, graphContext);
    if (!depthResult.IsValid()) {
        stageFailed = true;
        if (stageError.empty()) {
            stageError = "depth_prepass_graph_contract";
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);
    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "depth_prepass_graph_stage_failed";
        if (!stageError.empty()) {
            result.fallbackReason += ": " + stageError;
        }
    } else {
        m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("DepthPrepass RG: {} (falling back to legacy barriers)", result.fallbackReason);
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions, false);
        RenderDepthPrepass(registry);
        result.executed = true;
    }

    return result;
}

Renderer::RenderGraphPassResult
Renderer::ExecuteShadowPassInRenderGraph(Scene::ECS_Registry* registry) {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_shadowResources.map) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_shadow_prerequisites_missing";
        RenderShadowPass(registry);
        result.executed = true;
        return result;
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle shadowHandle =
        m_services.renderGraph->ImportResource(m_shadowResources.map.Get(), m_shadowResources.resourceState, "ShadowMap");

    bool stageFailed = false;
    std::string stageError;
    ShadowPass::GraphContext graphContext{};
    graphContext.shadowMap = shadowHandle;
    graphContext.execute = [&]() {
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions, true);
        RenderShadowPass(registry);
        return true;
    };
    graphContext.failStage = [&](const char* stage) {
        stageFailed = true;
        stageError = stage ? stage : "unknown";
    };

    const RGResourceHandle shadowResult = ShadowPass::AddToGraph(*m_services.renderGraph, graphContext);
    if (!shadowResult.IsValid()) {
        stageFailed = true;
        if (stageError.empty()) {
            stageError = "shadow_graph_contract";
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);
    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "shadow_graph_stage_failed";
        if (!stageError.empty()) {
            result.fallbackReason += ": " + stageError;
        }
    } else {
        m_shadowResources.resourceState = m_services.renderGraph->GetResourceState(shadowHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("Shadow RG: {} (falling back to legacy barriers)", result.fallbackReason);
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions, false);
        RenderShadowPass(registry);
        result.executed = true;
    }

    return result;
}

} // namespace Cortex::Graphics
