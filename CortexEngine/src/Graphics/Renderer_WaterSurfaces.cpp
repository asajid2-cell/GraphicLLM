#include "Renderer.h"

#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
namespace Cortex::Graphics {

void Renderer::RenderWaterSurfaces(Scene::ECS_Registry* registry) {
    if (!m_pipelineState.waterOverlay || !m_mainTargets.hdr.resources.color || !m_depthResources.resources.buffer) {
        return;
    }

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }
    if (snapshot->waterIndices.empty()) {
        return;
    }

    // Ensure HDR is writable.
    if (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        barrier.Transition.StateBefore = m_mainTargets.hdr.resources.state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Depth-test water without writing depth. Prefer read-only DSV so the depth
    // buffer can stay in DEPTH_READ after VB resolve.
    const bool hasReadOnlyDsv = m_depthResources.descriptors.readOnlyDsv.IsValid();
    const D3D12_RESOURCE_STATES desiredDepthState = hasReadOnlyDsv ? kDepthSampleState : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (m_depthResources.resources.resourceState != desiredDepthState) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.resources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resources.resourceState;
        depthBarrier.Transition.StateAfter = desiredDepthState;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resources.resourceState = desiredDepthState;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_mainTargets.hdr.descriptors.rtv.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = hasReadOnlyDsv ? m_depthResources.descriptors.readOnlyDsv.cpu : m_depthResources.descriptors.dsv.cpu;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
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
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.waterOverlay->GetPipelineState());
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(m_constantBuffers.frameCPU.viewProjectionNoJitter);

    for (uint32_t entryIndex : snapshot->waterIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        if (!renderable.mesh) {
            continue;
        }
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        glm::vec3 centerWS = glm::vec3(entry.worldMatrix[3]);
        float radiusWS = 1.0f;
        if (renderable.mesh->hasBounds) {
            centerWS = glm::vec3(entry.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            radiusWS = renderable.mesh->boundsRadius * GetMaxWorldScale(entry.worldMatrix);
        }
        if (!SphereIntersectsFrustumCPU(frustum, centerWS, radiusWS)) {
            continue;
        }

        EnsureMaterialTextures(renderable);

        MaterialConstants materialData{};
        materialData.albedo    = renderable.albedoColor;
        materialData.metallic  = 0.0f;
        materialData.roughness = glm::clamp(m_waterState.roughness, 0.01f, 1.0f);
        materialData.ao        = glm::clamp(renderable.ao, 0.0f, 1.0f);
        materialData._pad0     = 0.0f;
        materialData.mapFlags  = glm::uvec4(0u);
        materialData.mapFlags2 = glm::uvec4(0u);
        materialData.specularParams =
            glm::vec4(glm::clamp(m_waterState.fresnelStrength, 0.0f, 3.0f), 0.0f, 0.0f, 0.0f);

        ObjectConstants objectData{};
        objectData.modelMatrix  = entry.worldMatrix;
        objectData.normalMatrix = entry.normalMatrix;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);

        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(2, materialCB);

        if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
            m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);
        } else if (m_materialFallbacks.descriptorTable[0].IsValid()) {
            m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, m_materialFallbacks.descriptorTable[0].gpu);
        }

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
            ++m_frameDiagnostics.contract.drawCounts.waterDraws;
        }
    }
}

} // namespace Cortex::Graphics
