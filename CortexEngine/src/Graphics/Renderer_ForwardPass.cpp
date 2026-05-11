#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <glm/gtx/component_wise.hpp>

namespace Cortex::Graphics {

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    MeshDrawPass::PipelineStateContext pipelineContext{};
    pipelineContext.commandList = m_commandResources.graphicsList.Get();
    pipelineContext.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    pipelineContext.pipelineState = m_pipelineState.geometry->GetPipelineState();
    pipelineContext.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    pipelineContext.frameConstants = m_constantBuffers.currentFrameGPU;
    pipelineContext.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return;
    }

    // Bind biome materials buffer (b4) if valid
    if (m_constantBuffers.biomeMaterialsValid) {
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(7, m_constantBuffers.biomeMaterials.gpuAddress);
    }

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }

    const int entityCount = static_cast<int>(snapshot->renderableEntityCount);
    int drawnCount = 0;

    for (uint32_t entryIndex : snapshot->depthWritingIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        // Simple frustum/near-far culling using a bounding sphere derived
        // from the mesh's object-space bounds and the entity transform. This
        // avoids submitting obviously off-screen objects in large scenes
        // such as the RT showcase gallery without changing visibility for
        // anything inside the camera frustum.
        const auto& meshData = *renderable.mesh;
        if (meshData.hasBounds) {
            const glm::vec3 centerWS = glm::vec3(entry.worldMatrix * glm::vec4(meshData.boundsCenter, 1.0f));
            const float maxScale = GetMaxWorldScale(entry.worldMatrix);
            const float radiusWS = meshData.boundsRadius * maxScale;

            const glm::vec3 toCenter = centerWS - m_cameraState.positionWS;
            const float distAlongFwd = glm::dot(toCenter, glm::normalize(m_cameraState.forwardWS));

            // Cull objects entirely behind the near plane or far beyond the
            // far plane, with a small radius cushion.
            if (distAlongFwd + radiusWS < m_cameraState.nearPlane ||
                distAlongFwd - radiusWS > m_cameraState.farPlane) {
                continue;
            }
        }

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

        // Global fractal parameters (applied uniformly to all materials)
        const auto& fractal = m_fractalSurfaceState;
        materialData.fractalParams0 = glm::vec4(
            fractal.amplitude,
            fractal.frequency,
            fractal.octaves,
            (fractal.amplitude > 0.0f ? 1.0f : 0.0f));
        materialData.fractalParams1 = glm::vec4(
            fractal.coordMode,
            fractal.scaleX,
            fractal.scaleZ,
            materialModel.materialType);
        materialData.fractalParams2 = glm::vec4(
            fractal.lacunarity,
            fractal.gain,
            fractal.warpStrength,
            fractal.noiseType);

        // Update object constants
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }

        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entry.entity);
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);

        ObjectConstants objectData = {};
        objectData.modelMatrix = modelMatrix;
        objectData.normalMatrix = entry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);

        // Descriptor tables are warmed via PrewarmMaterialDescriptors().
        if (!renderable.textures.gpuState || !renderable.textures.gpuState->descriptors[0].IsValid()) {
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
            ++m_frameDiagnostics.contract.drawCounts.opaqueDraws;
            drawnCount++;
        } else {
            // Log this warning only once to avoid spamming the console every
            // frame if the scene contains placeholder entities without mesh
            // data (for example, when scene setup fails part-way through).
            if (!m_frameLifecycle.missingBufferWarningLogged) {
                spdlog::warn("  Entity {} has no vertex/index buffers", entityCount);
                m_frameLifecycle.missingBufferWarningLogged = true;
            }
        }
    }
 
    if (drawnCount == 0 && entityCount > 0 && !m_frameLifecycle.zeroDrawWarningLogged) {
        spdlog::warn("RenderScene: Found {} entities but drew 0!", entityCount);
        m_frameLifecycle.zeroDrawWarningLogged = true;
    }

    // Debug: periodic render statistics
    static uint32_t s_renderLogCounter = 0;
    if (++s_renderLogCounter % 120 == 0) {  // Log every ~2 seconds at 60fps
        spdlog::info("RenderScene: entities={} drawn={}", entityCount, drawnCount);
    }
}

} // namespace Cortex::Graphics
