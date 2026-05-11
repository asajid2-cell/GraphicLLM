#include "Renderer.h"

#include "Passes/DepthPrepass.h"
#include "Passes/RenderPassScope.h"
#include "Passes/ShadowPass.h"
#include "RenderableClassification.h"
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
    const char* stageError = nullptr;
    DepthPrepass::GraphContext graphContext{};
    graphContext.depth = depthHandle;
    graphContext.status.failed = &stageFailed;
    graphContext.status.stage = &stageError;
    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &localSnapshot;
    }
    for (uint32_t entryIndex : snapshot->depthWritingIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& sceneEntry = snapshot->entries[entryIndex];
        if (IsAlphaTestedDepthClass(sceneEntry.depthClass) && sceneEntry.renderable) {
            EnsureMaterialTextures(*sceneEntry.renderable);
        }
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

    const RGResourceHandle depthResult = DepthPrepass::AddToGraph(*m_services.renderGraph, graphContext);
    if (!depthResult.IsValid()) {
        stageFailed = true;
        if (!stageError) {
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
        if (stageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += stageError;
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
    const char* stageError = nullptr;
    ShadowPass::GraphContext graphContext{};
    graphContext.shadowMap = shadowHandle;
    graphContext.status.failed = &stageFailed;
    graphContext.status.stage = &stageError;
    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &localSnapshot;
    };
    for (uint32_t entryIndex : snapshot->depthWritingIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& sceneEntry = snapshot->entries[entryIndex];
        if (IsAlphaTestedDepthClass(sceneEntry.depthClass) && sceneEntry.renderable) {
            EnsureMaterialTextures(*sceneEntry.renderable);
        }
    }
    graphContext.draw.target.commandList = m_commandResources.graphicsList.Get();
    graphContext.draw.target.shadowMap = m_shadowResources.resources.map.Get();
    graphContext.draw.target.resourceState = &m_shadowResources.resources.resourceState;
    graphContext.draw.target.initializedForEditor = &m_shadowResources.resources.initializedForEditor;
    graphContext.draw.target.skipTransitions = true;
    graphContext.draw.dsvs = std::span<const DescriptorHandle>(m_shadowResources.resources.dsvs.data(),
                                                               m_shadowResources.resources.dsvs.size());
    graphContext.draw.viewport = m_shadowResources.raster.viewport;
    graphContext.draw.scissor = m_shadowResources.raster.scissor;
    graphContext.draw.pipeline.commandList = m_commandResources.graphicsList.Get();
    graphContext.draw.pipeline.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    graphContext.draw.pipeline.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    graphContext.draw.shadow = m_pipelineState.shadow.get();
    graphContext.draw.shadowDoubleSided = m_pipelineState.shadowDoubleSided.get();
    graphContext.draw.shadowAlpha = m_pipelineState.shadowAlpha.get();
    graphContext.draw.shadowAlphaDoubleSided = m_pipelineState.shadowAlphaDoubleSided.get();
    graphContext.draw.snapshot = snapshot;
    graphContext.draw.objectConstants = &m_constantBuffers.object;
    graphContext.draw.materialConstants = &m_constantBuffers.material;
    graphContext.draw.shadowConstants = &m_constantBuffers.shadow;
    graphContext.draw.frameConstants = m_constantBuffers.currentFrameGPU;
    graphContext.draw.materialFallbacks = {
        m_materialFallbacks.albedo.get(),
        m_materialFallbacks.normal.get(),
        m_materialFallbacks.metallic.get(),
        m_materialFallbacks.roughness.get()
    };
    graphContext.draw.drawCounter = &m_frameDiagnostics.contract.drawCounts.shadowDraws;
    graphContext.draw.cascadeCount = kShadowCascadeCount;
    graphContext.draw.maxShadowedLocalLights = kMaxShadowedLocalLights;
    graphContext.draw.shadowArraySize = kShadowArraySize;
    graphContext.draw.localShadowHasShadow = m_localShadowState.hasShadow;
    graphContext.draw.localShadowCount = m_localShadowState.count;

    const RGResourceHandle shadowResult = ShadowPass::AddToGraph(*m_services.renderGraph, graphContext);
    if (!shadowResult.IsValid()) {
        stageFailed = true;
        if (!stageError) {
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
        if (stageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += stageError;
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
