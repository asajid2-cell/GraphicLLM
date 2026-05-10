#include "Renderer.h"

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
    if (!m_pipelineState.overlay || !m_mainTargets.hdrColor || !m_depthResources.buffer) {
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

    // Ensure HDR is writable.
    if (m_mainTargets.hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_mainTargets.hdrColor.Get();
        barrier.Transition.StateBefore = m_mainTargets.hdrState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Depth-test overlays without writing depth. If we have a read-only DSV,
    // keep the depth buffer in DEPTH_READ; otherwise fall back to DEPTH_WRITE.
    const bool hasReadOnlyDsv = m_depthResources.readOnlyDsv.IsValid();
    const D3D12_RESOURCE_STATES desiredDepthState = hasReadOnlyDsv ? kDepthSampleState : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (m_depthResources.resourceState != desiredDepthState) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resourceState;
        depthBarrier.Transition.StateAfter = desiredDepthState;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resourceState = desiredDepthState;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_mainTargets.hdrRTV.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = hasReadOnlyDsv ? m_depthResources.readOnlyDsv.cpu : m_depthResources.dsv.cpu;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdrColor->GetDesc();
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(hdrDesc.Width);
    viewport.Height = static_cast<float>(hdrDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(hdrDesc.Width);
    scissorRect.bottom = static_cast<LONG>(hdrDesc.Height);
    m_commandResources.graphicsList->RSSetViewports(1, &viewport);
    m_commandResources.graphicsList->RSSetScissorRects(1, &scissorRect);

    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.overlay->GetPipelineState());
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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

        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(2, materialCB);

        if (!renderable.textures.gpuState ||
            !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);

        if (renderable.mesh->gpuBuffers &&
            renderable.mesh->gpuBuffers->vertexBuffer &&
            renderable.mesh->gpuBuffers->indexBuffer) {
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
            vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
            vbv.StrideInBytes = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
            ibv.Format = DXGI_FORMAT_R32_UINT;

            m_commandResources.graphicsList->IASetVertexBuffers(0, 1, &vbv);
            m_commandResources.graphicsList->IASetIndexBuffer(&ibv);
            m_commandResources.graphicsList->DrawIndexedInstanced(static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
            ++m_frameDiagnostics.contract.drawCounts.overlayDraws;
        }
    }
}

} // namespace Cortex::Graphics
