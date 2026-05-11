#include "Renderer.h"

#include "Graphics/Passes/ForwardTargetBindingPass.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <vector>
namespace Cortex::Graphics {

void Renderer::RenderTransparent(Scene::ECS_Registry* registry) {
    if (!m_pipelineState.transparent) {
        return;
    }

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }
    if (snapshot->transparentIndices.empty()) {
        return;
    }

    struct TransparentDraw {
        uint32_t entryIndex;
        float depth;
    };

    std::vector<TransparentDraw> drawList;
    drawList.reserve(snapshot->transparentIndices.size());

    const glm::vec3 cameraPos = glm::vec3(m_constantBuffers.frameCPU.cameraPosition);
    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(m_constantBuffers.frameCPU.viewProjectionNoJitter);

    // Collect transparent entities and compute a simple distance-based depth
    // for back-to-front sorting.
    for (uint32_t entryIndex : snapshot->transparentIndices) {
        if (entryIndex >= snapshot->entries.size()) {
            continue;
        }
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !renderablePtr) {
            continue;
        }
        auto& renderable = *renderablePtr;

        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }

        glm::vec3 centerWS = glm::vec3(entry.worldMatrix[3]);
        float radiusWS = 1.0f;
        if (renderable.mesh->hasBounds) {
            centerWS =
                glm::vec3(entry.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            const float maxScale = GetMaxWorldScale(entry.worldMatrix);
            radiusWS = renderable.mesh->boundsRadius * maxScale;
        }

        if (!SphereIntersectsFrustumCPU(frustum, centerWS, radiusWS)) {
            continue;
        }

        glm::vec3 worldPos = glm::vec3(entry.worldMatrix[3]);
        const glm::vec3 toCamera = worldPos - cameraPos;
        float depth = glm::dot(toCamera, toCamera);
        drawList.push_back(TransparentDraw{entryIndex, depth});
    }

    if (drawList.empty()) {
        return;
    }

    std::sort(drawList.begin(), drawList.end(),
              [](const TransparentDraw& a, const TransparentDraw& b) {
                  // Draw far-to-near for correct alpha blending. Tie-break on
                  // entity ID for determinism to avoid flicker when depths are
                  // extremely close.
                  if (a.depth != b.depth) {
                      return a.depth > b.depth;
                  }
                  return a.entryIndex < b.entryIndex;
              });

    // Bind HDR + depth explicitly for the transparent pass. Render HDR only
    // (no normal/roughness writes) so post-processing continues to consume the
    // opaque/VB normal buffer.
    if (!m_mainTargets.hdr.resources.color || !m_depthResources.resources.buffer) {
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

    // Root signature, pipeline, descriptor heap, and primitive topology for
    // main geometry were already set in PrepareMainPass. We rebind the
    // transparent pipeline and frame constants to be explicit.
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.transparent->GetPipelineState());
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& draw : drawList) {
        const RendererSceneRenderable& entry = snapshot->entries[draw.entryIndex];
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

        ObjectConstants objectData = {};
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

        D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB =
            m_constantBuffers.material.AllocateAndWrite(materialData);

        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(2, materialCB);

        // Descriptor tables are warmed via PrewarmMaterialDescriptors().
        if (!renderable.textures.gpuState ||
            !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }

        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(
            3, renderable.textures.gpuState->descriptors[0].gpu);

        const auto drawResult =
            MeshDrawPass::DrawIndexedMesh(m_commandResources.graphicsList.Get(), *renderable.mesh);
        if (drawResult.submitted) {
            ++m_frameDiagnostics.contract.drawCounts.transparentDraws;
        }
    }
}

} // namespace Cortex::Graphics
