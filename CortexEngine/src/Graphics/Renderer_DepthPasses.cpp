#include "Renderer.h"
#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/Passes/DepthPrepassTargetPass.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

namespace Cortex::Graphics {

void Renderer::RenderDepthPrepass(Scene::ECS_Registry* registry) {
    if (!registry || !m_depthResources.resources.buffer || !m_pipelineState.depthOnly) {
        return;
    }

    DepthPrepassTargetPass::BindContext targetContext{};
    targetContext.commandList = m_commandResources.graphicsList.Get();
    targetContext.depthBuffer = m_depthResources.resources.buffer.Get();
    targetContext.depthState = &m_depthResources.resources.resourceState;
    targetContext.depthDsv = m_depthResources.descriptors.dsv;
    targetContext.skipTransitions = m_frameDiagnostics.renderGraph.transitions.depthPrepassSkipTransitions;
    if (!DepthPrepassTargetPass::BindAndClear(targetContext)) {
        return;
    }

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

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
        if (drawResult.submitted) {
            ++m_frameDiagnostics.contract.drawCounts.depthPrepassDraws;
        }
    }
}

} // namespace Cortex::Graphics
