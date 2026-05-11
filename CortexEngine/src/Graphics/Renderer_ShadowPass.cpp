#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>

namespace Cortex::Graphics {

void Renderer::RenderShadowPass(Scene::ECS_Registry* registry) {
    if (!registry || !m_shadowResources.resources.map || !m_pipelineState.shadow) {
        return;
    }

    // Transition shadow map to depth write.
    // The shadow map is a texture array with kShadowArraySize slices (cascades + local
    // lights). We must ensure ALL subresources are in DEPTH_WRITE state before any
    // depth clears or writes occur.
    if (!m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions) {
        // If the tracked state indicates we need a transition, issue it.
        // Also check m_shadowResources.resources.initializedForEditor to handle the first frame after
        // switching to editor mode - the RenderGraph path may have left the shadow
        // map in a different state than our tracking indicates.
        if (m_shadowResources.resources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_shadowResources.resources.map.Get();
            barrier.Transition.StateBefore = m_shadowResources.resources.resourceState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_shadowResources.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        // Mark that we've successfully initialized for editor mode
        m_shadowResources.resources.initializedForEditor = true;
    }

    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &localSnapshot;
    }

    // Root signature + descriptor heap for optional alpha-tested shadow draws.
    // When bindless is enabled the root signature is HEAP_DIRECTLY_INDEXED, so
    // the CBV/SRV/UAV heap must be bound before setting the root signature.
    if (m_services.descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
        m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    }
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    DX12Pipeline* currentPipeline = m_pipelineState.shadow.get();
    if (currentPipeline) {
        m_commandResources.graphicsList->SetPipelineState(currentPipeline->GetPipelineState());
    }

    auto drawShadowCasters = [&]() {
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

            const bool alphaTest = IsAlphaTestedDepthClass(depthClass);
            const bool doubleSided = IsDoubleSidedDepthClass(depthClass);

            DX12Pipeline* desired = m_pipelineState.shadow.get();
            if (alphaTest) {
                desired = doubleSided ? m_pipelineState.shadowAlphaDoubleSided.get() : m_pipelineState.shadowAlpha.get();
            } else {
                desired = doubleSided ? m_pipelineState.shadowDoubleSided.get() : m_pipelineState.shadow.get();
            }
            if (!desired) {
                desired = m_pipelineState.shadow.get();
            }
            if (desired && desired != currentPipeline) {
                currentPipeline = desired;
                m_commandResources.graphicsList->SetPipelineState(currentPipeline->GetPipelineState());
            }

            ObjectConstants objectData = {};
            glm::mat4 modelMatrix = sceneEntry.worldMatrix;
            const uint32_t stableKey = static_cast<uint32_t>(sceneEntry.entity);
            if (renderable.mesh && !renderable.mesh->hasBounds) {
                renderable.mesh->UpdateBounds();
            }
            const AutoDepthSeparation sep =
                ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
            ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);
            objectData.modelMatrix = modelMatrix;
            objectData.normalMatrix = sceneEntry.normalMatrix;

            D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
            m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);

            if (alphaTest && (m_pipelineState.shadowAlpha || m_pipelineState.shadowAlphaDoubleSided)) {
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

                // Descriptor tables are warmed via PrewarmMaterialDescriptors().
                if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
                    m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);
                }
            }

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
            ++m_frameDiagnostics.contract.drawCounts.shadowDraws;
        }
    };

    for (uint32_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
        // Update shadow constants with current cascade index. Use a
        // per-cascade slice in the constant buffer so each cascade
        // sees the correct index even though all draws share a single
        // command list and execution happens later on the GPU.
        ShadowConstants shadowData{};
        shadowData.cascadeIndex = glm::uvec4(cascadeIndex, 0u, 0u, 0u);
        D3D12_GPU_VIRTUAL_ADDRESS shadowCB = m_constantBuffers.shadow.AllocateAndWrite(shadowData);

        // Bind frame constants
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
        // Bind shadow constants (b3)
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(5, shadowCB);

        // Bind DSV for this cascade
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowResources.resources.dsvs[cascadeIndex].cpu;
        m_commandResources.graphicsList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

        // Clear shadow depth
        m_commandResources.graphicsList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Set viewport and scissor for shadow map
        m_commandResources.graphicsList->RSSetViewports(1, &m_shadowResources.raster.viewport);
        m_commandResources.graphicsList->RSSetScissorRects(1, &m_shadowResources.raster.scissor);

        drawShadowCasters();
    }

    // Optional local light shadows rendered into atlas slices after the
    // cascades, using the view-projection matrices prepared in
    // UpdateFrameConstants.
    if (m_localShadowState.hasShadow && m_localShadowState.count > 0) {
        uint32_t maxLocal = std::min(m_localShadowState.count, kMaxShadowedLocalLights);
        for (uint32_t i = 0; i < maxLocal; ++i) {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice >= kShadowArraySize) {
                break;
            }

            ShadowConstants shadowData{};
            shadowData.cascadeIndex = glm::uvec4(slice, 0u, 0u, 0u);
            D3D12_GPU_VIRTUAL_ADDRESS shadowCB = m_constantBuffers.shadow.AllocateAndWrite(shadowData);

            // Bind frame constants
            m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
            // Bind shadow constants (b3)
            m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(5, shadowCB);

            // Bind DSV for this local light slice
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowResources.resources.dsvs[slice].cpu;
            m_commandResources.graphicsList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

            // Clear shadow depth
            m_commandResources.graphicsList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            // Set viewport and scissor for shadow map
            m_commandResources.graphicsList->RSSetViewports(1, &m_shadowResources.raster.viewport);
            m_commandResources.graphicsList->RSSetScissorRects(1, &m_shadowResources.raster.scissor);

            drawShadowCasters();
        }
    }

    // Transition shadow map for sampling
    if (!m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions) {
        if (m_shadowResources.resources.resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_shadowResources.resources.map.Get();
            barrier.Transition.StateBefore = m_shadowResources.resources.resourceState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_shadowResources.resources.resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }
}

} // namespace Cortex::Graphics
