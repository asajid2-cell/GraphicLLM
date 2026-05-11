#include "Renderer.h"

#include "Graphics/Passes/ForwardTargetBindingPass.h"
#include "Graphics/Passes/FullscreenPass.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <vector>
namespace Cortex::Graphics {

void Renderer::RenderOverlays(Scene::ECS_Registry* registry) {
    if (!m_pipelineState.overlay || !m_mainTargets.hdr.resources.color || !m_depthResources.resources.buffer) {
        return;
    }

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }
    if (snapshot->overlayIndices.empty()) {
        return;
    }

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(m_constantBuffers.frameCPU.viewProjectionNoJitter);

    ForwardTargetBindingPass::BindContext targetContext{};
    targetContext.commandList = m_commandResources.graphicsList.Get();
    targetContext.hdrColor = m_mainTargets.hdr.resources.color.Get();
    targetContext.hdrState = &m_mainTargets.hdr.resources.state;
    targetContext.hdrRtv = m_mainTargets.hdr.descriptors.rtv;
    targetContext.depthBuffer = m_depthResources.resources.buffer.Get();
    targetContext.depthState = &m_depthResources.resources.resourceState;
    targetContext.depthDsv = m_depthResources.descriptors.dsv;
    targetContext.readOnlyDepthDsv = m_depthResources.descriptors.readOnlyDsv;
    targetContext.readOnlyDepthState = kDepthSampleState;
    if (!ForwardTargetBindingPass::BindHdrAndDepthReadOnly(targetContext)) {
        return;
    }

    FullscreenPass::SetViewportAndScissor(m_commandResources.graphicsList.Get(),
                                          m_mainTargets.hdr.resources.color.Get());

    MeshDrawPass::PipelineStateContext pipelineContext{};
    pipelineContext.commandList = m_commandResources.graphicsList.Get();
    pipelineContext.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    pipelineContext.pipelineState = m_pipelineState.overlay->GetPipelineState();
    pipelineContext.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    pipelineContext.frameConstants = m_constantBuffers.currentFrameGPU;
    pipelineContext.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return;
    }

    std::vector<uint32_t> overlayIndices;
    overlayIndices.reserve(snapshot->overlayIndices.size());

    for (uint32_t entryIndex : snapshot->overlayIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        // Frustum culling to avoid drawing off-screen decals/markings.
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        if (renderable.mesh->hasBounds) {
            glm::vec3 centerWS = glm::vec3(entry.worldMatrix[3]);
            float radiusWS = 1.0f;
            centerWS = glm::vec3(entry.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            radiusWS = renderable.mesh->boundsRadius * GetMaxWorldScale(entry.worldMatrix);
            if (!SphereIntersectsFrustumCPU(frustum, centerWS, radiusWS)) {
                continue;
            }
        }

        overlayIndices.push_back(entryIndex);
    }

    if (overlayIndices.empty()) {
        return;
    }

    // Deterministic ordering: older overlays first so newer entities (higher IDs)
    // land on top when multiple overlays overlap.
    std::sort(overlayIndices.begin(), overlayIndices.end(),
              [&](uint32_t a, uint32_t b) {
                  return static_cast<uint32_t>(snapshot->entries[a].entity) <
                         static_cast<uint32_t>(snapshot->entries[b].entity);
              });

    for (uint32_t entryIndex : overlayIndices) {
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        EnsureMaterialTextures(renderable);

        const MaterialTextureFallbacks materialFallbacks{
            m_materialFallbacks.albedo.get(),
            m_materialFallbacks.normal.get(),
            m_materialFallbacks.metallic.get(),
            m_materialFallbacks.roughness.get()
        };
        const MaterialModel materialModel = MaterialResolver::ResolveRenderable(renderable, materialFallbacks);
        MaterialConstants materialData = MaterialResolver::BuildMaterialConstants(materialModel);

        FillMaterialTextureIndices(renderable, materialData);

        ObjectConstants objectData{};
        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entry.entity);
        if (renderable.mesh && !renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);
        objectData.modelMatrix  = modelMatrix;
        objectData.normalMatrix = entry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);

        if (!renderable.textures.gpuState ||
            !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }

        MeshDrawPass::ObjectMaterialContext materialContext{};
        materialContext.commandList = m_commandResources.graphicsList.Get();
        materialContext.objectConstants = objectCB;
        materialContext.materialConstants = materialCB;
        materialContext.materialTable = renderable.textures.gpuState->descriptors[0];
        if (!MeshDrawPass::BindObjectMaterial(materialContext)) {
            continue;
        }

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
        if (drawResult.submitted) {
            ++m_frameDiagnostics.contract.drawCounts.overlayDraws;
        }
    }
}

} // namespace Cortex::Graphics
