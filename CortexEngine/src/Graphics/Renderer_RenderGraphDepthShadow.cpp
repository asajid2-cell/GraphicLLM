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
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_depthResources.resources.buffer || !m_pipelineState.depthOnly) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_depth_prerequisites_missing";
        return result;
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth");

    bool stageFailed = false;
    std::string stageError;
    DepthPrepass::GraphContext graphContext{};
    graphContext.depth = depthHandle;
    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &localSnapshot;
    }
    graphContext.draw.target.commandList = m_commandResources.graphicsList.Get();
    graphContext.draw.target.depthBuffer = m_depthResources.resources.buffer.Get();
    graphContext.draw.target.depthState = &m_depthResources.resources.resourceState;
    graphContext.draw.target.depthDsv = m_depthResources.descriptors.dsv;
    graphContext.draw.target.skipTransitions = true;
    graphContext.draw.pipeline.commandList = m_commandResources.graphicsList.Get();
    graphContext.draw.pipeline.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    graphContext.draw.pipeline.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    graphContext.draw.pipeline.frameConstants = m_constantBuffers.currentFrameGPU;
    graphContext.draw.depthOnly = m_pipelineState.depthOnly.get();
    graphContext.draw.depthOnlyDoubleSided = m_pipelineState.depthOnlyDoubleSided.get();
    graphContext.draw.depthOnlyAlpha = m_pipelineState.depthOnlyAlpha.get();
    graphContext.draw.depthOnlyAlphaDoubleSided = m_pipelineState.depthOnlyAlphaDoubleSided.get();
    graphContext.draw.snapshot = snapshot;
    graphContext.draw.objectConstants = &m_constantBuffers.object;
    graphContext.draw.materialConstants = &m_constantBuffers.material;
    graphContext.draw.materialFallbacks = {
        m_materialFallbacks.albedo.get(),
        m_materialFallbacks.normal.get(),
        m_materialFallbacks.metallic.get(),
        m_materialFallbacks.roughness.get()
    };
    graphContext.draw.drawCounter = &m_frameDiagnostics.contract.drawCounts.depthPrepassDraws;
    graphContext.draw.ensureMaterialTextures = [&](Scene::RenderableComponent& renderable) {
        EnsureMaterialTextures(renderable);
    };
    graphContext.draw.fillMaterialTextureIndices =
        [&](const Scene::RenderableComponent& renderable, MaterialConstants& materialData) {
            FillMaterialTextureIndices(renderable, materialData);
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
        m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("DepthPrepass RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}

Renderer::RenderGraphPassResult
Renderer::ExecuteShadowPassInRenderGraph(Scene::ECS_Registry* registry) {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_shadowResources.resources.map) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_shadow_prerequisites_missing";
        return result;
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle shadowHandle =
        m_services.renderGraph->ImportResource(m_shadowResources.resources.map.Get(), m_shadowResources.resources.resourceState, "ShadowMap");

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
        m_shadowResources.resources.resourceState = m_services.renderGraph->GetResourceState(shadowHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("Shadow RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}

} // namespace Cortex::Graphics
