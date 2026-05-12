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

    MeshDrawPass::PipelineStateContext pipelineContext{};
    pipelineContext.commandList = m_commandResources.graphicsList.Get();
    pipelineContext.rootSignature = m_pipelineState.rootSignature->GetRootSignature();
    pipelineContext.pipelineState = m_pipelineState.waterOverlay->GetPipelineState();
    pipelineContext.cbvSrvUavHeap = m_services.descriptorManager
        ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap()
        : nullptr;
    pipelineContext.frameConstants = m_constantBuffers.currentFrameGPU;
    pipelineContext.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    if (!MeshDrawPass::BindPipelineState(pipelineContext)) {
        return;
    }

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

        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entry.entity);
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);

        ObjectConstants objectData{};
        objectData.modelMatrix  = modelMatrix;
        objectData.normalMatrix = entry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);

        DescriptorHandle materialTable{};
        if (renderable.textures.gpuState && renderable.textures.gpuState->descriptors[0].IsValid()) {
            materialTable = renderable.textures.gpuState->descriptors[0];
        } else if (m_materialFallbacks.descriptorTable[0].IsValid()) {
            materialTable = m_materialFallbacks.descriptorTable[0];
        }

        MeshDrawPass::ObjectMaterialContext materialContext{};
        materialContext.commandList = m_commandResources.graphicsList.Get();
        materialContext.objectConstants = objectCB;
        materialContext.materialConstants = materialCB;
        materialContext.materialTable = materialTable;
        if (!MeshDrawPass::BindObjectMaterial(materialContext)) {
            continue;
        }

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
        if (drawResult.submitted) {
            ++m_frameDiagnostics.contract.drawCounts.waterDraws;
        }
    }
}

} // namespace Cortex::Graphics
