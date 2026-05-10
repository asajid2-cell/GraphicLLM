#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <glm/gtx/component_wise.hpp>

namespace Cortex::Graphics {

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    // Ensure graphics pipeline and root signature are bound after any compute work
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.geometry->GetPipelineState());
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind frame constants
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);

    // Bind shadow map + environment descriptor table if available (t4-t6)
    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
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

        // Bind constants
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(2, materialCB);

        m_commandResources.graphicsList->SetPipelineState(m_pipelineState.geometry->GetPipelineState());
        m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Descriptor tables are warmed via PrewarmMaterialDescriptors().
        if (!renderable.textures.gpuState || !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);

        // Bind vertex and index buffers
        if (renderable.mesh->gpuBuffers && renderable.mesh->gpuBuffers->vertexBuffer && renderable.mesh->gpuBuffers->indexBuffer) {
            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
            vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
            vbv.StrideInBytes = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
            ibv.Format = DXGI_FORMAT_R32_UINT;

            m_commandResources.graphicsList->IASetVertexBuffers(0, 1, &vbv);
            m_commandResources.graphicsList->IASetIndexBuffer(&ibv);

            m_commandResources.graphicsList->DrawIndexedInstanced(static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
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
