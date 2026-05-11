#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/Passes/ShadowTargetPass.h"
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

    ShadowTargetPass::TransitionContext shadowTarget{};
    shadowTarget.commandList = m_commandResources.graphicsList.Get();
    shadowTarget.shadowMap = m_shadowResources.resources.map.Get();
    shadowTarget.resourceState = &m_shadowResources.resources.resourceState;
    shadowTarget.initializedForEditor = &m_shadowResources.resources.initializedForEditor;
    shadowTarget.skipTransitions = m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions;
    if (!ShadowTargetPass::TransitionToDepthWrite(shadowTarget)) {
        return;
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

            const auto drawResult =
                MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
            if (drawResult.submitted) {
                ++m_frameDiagnostics.contract.drawCounts.shadowDraws;
            }
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

        ShadowTargetPass::SliceContext sliceContext{};
        sliceContext.commandList = m_commandResources.graphicsList.Get();
        sliceContext.dsv = m_shadowResources.resources.dsvs[cascadeIndex];
        sliceContext.viewport = m_shadowResources.raster.viewport;
        sliceContext.scissor = m_shadowResources.raster.scissor;
        if (!ShadowTargetPass::BindAndClearSlice(sliceContext)) {
            continue;
        }

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

            ShadowTargetPass::SliceContext sliceContext{};
            sliceContext.commandList = m_commandResources.graphicsList.Get();
            sliceContext.dsv = m_shadowResources.resources.dsvs[slice];
            sliceContext.viewport = m_shadowResources.raster.viewport;
            sliceContext.scissor = m_shadowResources.raster.scissor;
            if (!ShadowTargetPass::BindAndClearSlice(sliceContext)) {
                continue;
            }

            drawShadowCasters();
        }
    }

    (void)ShadowTargetPass::TransitionToShaderResource(shadowTarget);
}

} // namespace Cortex::Graphics
