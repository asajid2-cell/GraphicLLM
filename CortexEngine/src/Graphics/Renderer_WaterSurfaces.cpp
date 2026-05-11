#include "Renderer.h"

#include "Graphics/Passes/ForwardTargetBindingPass.h"
#include "Graphics/Passes/FullscreenPass.h"
#include "Graphics/Passes/MeshDrawPass.h"
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

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
        if (drawResult.submitted) {
            ++m_frameDiagnostics.contract.drawCounts.waterDraws;
        }
    }
}

} // namespace Cortex::Graphics
