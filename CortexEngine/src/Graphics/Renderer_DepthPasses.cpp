#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Graphics/Passes/DepthPrepass.h"
#include "Scene/ECS_Registry.h"

namespace Cortex::Graphics {

void Renderer::RenderDepthPrepass(Scene::ECS_Registry* registry) {
    if (!registry || !m_depthResources.resources.buffer || !m_pipelineState.depthOnly) {
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

    DepthPrepass::DrawContext draw{};
    draw.target.commandList = m_commandResources.graphicsList.Get();
    draw.target.depthBuffer = m_depthResources.resources.buffer.Get();
    draw.target.depthState = &m_depthResources.resources.resourceState;
    draw.target.depthDsv = m_depthResources.descriptors.dsv;
    draw.target.skipTransitions = m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions;
    draw.pipeline.commandList = m_commandResources.graphicsList.Get();
    draw.pipeline.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    draw.pipeline.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    draw.pipeline.frameConstants = m_constantBuffers.currentFrameGPU;
    draw.depthOnly = m_pipelineState.depthOnly.get();
    draw.depthOnlyDoubleSided = m_pipelineState.depthOnlyDoubleSided.get();
    draw.depthOnlyAlpha = m_pipelineState.depthOnlyAlpha.get();
    draw.depthOnlyAlphaDoubleSided = m_pipelineState.depthOnlyAlphaDoubleSided.get();
    draw.snapshot = snapshot;
    draw.objectConstants = &m_constantBuffers.object;
    draw.materialConstants = &m_constantBuffers.material;
    draw.materialFallbacks = {
        m_materialFallbacks.albedo.get(),
        m_materialFallbacks.normal.get(),
        m_materialFallbacks.metallic.get(),
        m_materialFallbacks.roughness.get()
    };
    draw.drawCounter = &m_frameDiagnostics.contract.drawCounts.depthPrepassDraws;

    (void)DepthPrepass::Draw(draw);
}

} // namespace Cortex::Graphics
