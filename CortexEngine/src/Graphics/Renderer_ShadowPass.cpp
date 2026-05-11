#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Graphics/Passes/ShadowPass.h"
#include "Scene/ECS_Registry.h"

namespace Cortex::Graphics {

void Renderer::RenderShadowPass(Scene::ECS_Registry* registry) {
    if (!registry || !m_shadowResources.resources.map || !m_pipelineState.shadow) {
        return;
    }

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

    ShadowPass::DrawContext draw{};
    draw.target.commandList = m_commandResources.graphicsList.Get();
    draw.target.shadowMap = m_shadowResources.resources.map.Get();
    draw.target.resourceState = &m_shadowResources.resources.resourceState;
    draw.target.initializedForEditor = &m_shadowResources.resources.initializedForEditor;
    draw.target.skipTransitions = m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions;
    draw.dsvs = std::span<const DescriptorHandle>(m_shadowResources.resources.dsvs.data(),
                                                  m_shadowResources.resources.dsvs.size());
    draw.viewport = m_shadowResources.raster.viewport;
    draw.scissor = m_shadowResources.raster.scissor;
    draw.pipeline.commandList = m_commandResources.graphicsList.Get();
    draw.pipeline.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    draw.pipeline.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    draw.shadow = m_pipelineState.shadow.get();
    draw.shadowDoubleSided = m_pipelineState.shadowDoubleSided.get();
    draw.shadowAlpha = m_pipelineState.shadowAlpha.get();
    draw.shadowAlphaDoubleSided = m_pipelineState.shadowAlphaDoubleSided.get();
    draw.snapshot = snapshot;
    draw.objectConstants = &m_constantBuffers.object;
    draw.materialConstants = &m_constantBuffers.material;
    draw.shadowConstants = &m_constantBuffers.shadow;
    draw.frameConstants = m_constantBuffers.currentFrameGPU;
    draw.materialFallbacks = {
        m_materialFallbacks.albedo.get(),
        m_materialFallbacks.normal.get(),
        m_materialFallbacks.metallic.get(),
        m_materialFallbacks.roughness.get()
    };
    draw.drawCounter = &m_frameDiagnostics.contract.drawCounts.shadowDraws;
    draw.cascadeCount = kShadowCascadeCount;
    draw.maxShadowedLocalLights = kMaxShadowedLocalLights;
    draw.shadowArraySize = kShadowArraySize;
    draw.localShadowHasShadow = m_localShadowState.hasShadow;
    draw.localShadowCount = m_localShadowState.count;

    (void)ShadowPass::Draw(draw);
}

} // namespace Cortex::Graphics
