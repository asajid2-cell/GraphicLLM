#include "Renderer.h"
#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

namespace Cortex::Graphics {

void Renderer::RenderDepthPrepass(Scene::ECS_Registry* registry) {
    if (!registry || !m_depthResources.resources.buffer || !m_pipelineState.depthOnly) {
        return;
    }

    // Ensure depth buffer is writable for the prepass.
    if (!m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions &&
        m_depthResources.resources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.resources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resources.resourceState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Bind depth stencil only; no color targets for this pass.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthResources.descriptors.dsv.cpu;
    m_commandResources.graphicsList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    // Clear depth to far plane.
    m_commandResources.graphicsList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor to match the depth buffer.
    const D3D12_RESOURCE_DESC depthDesc = m_depthResources.resources.buffer->GetDesc();
    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<float>(depthDesc.Width);
    viewport.Height   = static_cast<float>(depthDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left   = 0;
    scissorRect.top    = 0;
    scissorRect.right  = static_cast<LONG>(depthDesc.Width);
    scissorRect.bottom = static_cast<LONG>(depthDesc.Height);

    m_commandResources.graphicsList->RSSetViewports(1, &viewport);
    m_commandResources.graphicsList->RSSetScissorRects(1, &scissorRect);

    // Bind root signature and depth-only pipeline. Alpha-tested depth draws
    // sample material textures, so bind the shader-visible heap before any
    // optional descriptor-table access.
    if (m_services.descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
        m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    }
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.depthOnly->GetPipelineState());
    DX12Pipeline* currentDepthPipeline = m_pipelineState.depthOnly.get();

    // Frame constants (b1)
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);

    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

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
        auto& renderable = *sceneEntry.renderable;
        const RenderableDepthClass depthClass = sceneEntry.depthClass;
        if (!sceneEntry.hasGpuBuffers) {
            continue;
        }

        DX12Pipeline* desiredDepthPipeline = nullptr;
        if (IsAlphaTestedDepthClass(depthClass)) {
            desiredDepthPipeline =
                (IsDoubleSidedDepthClass(depthClass) && m_pipelineState.depthOnlyAlphaDoubleSided)
                    ? m_pipelineState.depthOnlyAlphaDoubleSided.get()
                    : m_pipelineState.depthOnlyAlpha.get();
            if (!desiredDepthPipeline) {
                continue;
            }
        } else {
            desiredDepthPipeline =
                (IsDoubleSidedDepthClass(depthClass) && m_pipelineState.depthOnlyDoubleSided)
                    ? m_pipelineState.depthOnlyDoubleSided.get()
                    : m_pipelineState.depthOnly.get();
        }

        if (desiredDepthPipeline != currentDepthPipeline) {
            m_commandResources.graphicsList->SetPipelineState(desiredDepthPipeline->GetPipelineState());
            currentDepthPipeline = desiredDepthPipeline;
        }

        // Object constants (b0). Alpha-tested draws also bind b2/material
        // textures below so the depth prepass clips to the authored mask.
        ObjectConstants objectData = {};
        glm::mat4 modelMatrix = sceneEntry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(sceneEntry.entity);
        if (renderable.mesh && !renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);
        objectData.modelMatrix  = modelMatrix;
        objectData.normalMatrix = sceneEntry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            m_constantBuffers.object.AllocateAndWrite(objectData);
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);

        if (IsAlphaTestedDepthClass(depthClass)) {
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

            D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);
            m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(2, materialCB);

            if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
                m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);
            }
        }

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes    = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
        vbv.StrideInBytes  = sizeof(Vertex);

        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes    = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
        ibv.Format         = DXGI_FORMAT_R32_UINT;

        m_commandResources.graphicsList->IASetVertexBuffers(0, 1, &vbv);
        m_commandResources.graphicsList->IASetIndexBuffer(&ibv);

        m_commandResources.graphicsList->DrawIndexedInstanced(
            static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
        ++m_frameDiagnostics.contract.drawCounts.depthPrepassDraws;
    }
}

} // namespace Cortex::Graphics
