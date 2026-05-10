#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

namespace Cortex::Graphics {

bool Renderer::IsGPUCullingEnabled() const {
    return m_gpuCullingState.enabled && m_services.gpuCulling != nullptr;
}

void Renderer::SetGPUCullingFreeze(bool enabled) {
    m_gpuCullingState.freeze = enabled;
}

void Renderer::ToggleGPUCullingFreeze() {
    m_gpuCullingState.freeze = !m_gpuCullingState.freeze;
}

bool Renderer::IsGPUCullingFreezeEnabled() const {
    return m_gpuCullingState.freeze;
}

bool Renderer::IsIndirectDrawEnabled() const {
    return m_gpuCullingState.indirectDrawEnabled;
}

void Renderer::SetGPUCullingEnabled(bool enabled) {
    if (enabled && m_services.gpuCulling) {
        m_gpuCullingState.enabled = true;
        m_gpuCullingState.indirectDrawEnabled = true;
        spdlog::info("GPU culling enabled (indirect draw active)");
    } else {
        m_gpuCullingState.enabled = false;
        m_gpuCullingState.indirectDrawEnabled = false;
        if (enabled && !m_services.gpuCulling) {
            spdlog::warn("Cannot enable GPU culling: pipeline not initialized");
        }
    }
}

uint32_t Renderer::GetGPUCulledCount() const {
    return m_services.gpuCulling ? m_services.gpuCulling->GetVisibleCount() : 0;
}

uint32_t Renderer::GetGPUTotalInstances() const {
    return m_services.gpuCulling ? m_services.gpuCulling->GetTotalInstances() : 0;
}

GPUCullingPipeline::DebugStats Renderer::GetGPUCullingDebugStats() const {
    return m_services.gpuCulling ? m_services.gpuCulling->GetDebugStats() : GPUCullingPipeline::DebugStats{};
}

void Renderer::CollectInstancesForGPUCulling(Scene::ECS_Registry* registry) {
    if (!m_services.gpuCulling) return;

    m_gpuCullingState.instances.clear();
    m_gpuCullingState.meshInfos.clear();

    RendererSceneSnapshot fallbackSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &fallbackSnapshot;
    }

    for (uint32_t entryIndex : snapshot->depthWritingIndices) {
        if (entryIndex >= snapshot->entries.size()) continue;
        const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
        auto* renderablePtr = entry.renderable;
        if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !entry.hasGpuBuffers || !renderablePtr) continue;
        auto& renderable = *renderablePtr;

        GPUInstanceData inst{};
        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entry.entity);
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);
        inst.modelMatrix = modelMatrix;

        // Compute bounding sphere in object space
        if (renderable.mesh->hasBounds) {
            inst.boundingSphere = glm::vec4(
                renderable.mesh->boundsCenter,
                renderable.mesh->boundsRadius
            );
        } else {
            // Default bounding sphere
            inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
        }

        inst.meshIndex = static_cast<uint32_t>(m_gpuCullingState.meshInfos.size());
        inst.materialIndex = 0; // Could map to material ID if needed
        inst.flags = 1; // Visible by default

        m_gpuCullingState.instances.push_back(inst);

        // Store mesh info for indirect draw
        MeshInfo meshInfo{};
        meshInfo.indexCount = static_cast<uint32_t>(renderable.mesh->indices.size());
        meshInfo.startIndex = 0;
        meshInfo.baseVertex = 0;
        meshInfo.materialIndex = 0;
        m_gpuCullingState.meshInfos.push_back(meshInfo);
    }

    // Debug logging for GPU culling collection
    static uint32_t s_frameCounter = 0;
    if (++s_frameCounter % 300 == 1) {  // Log every ~5 seconds at 60fps
        spdlog::debug("GPU Culling: Collected {} instances for culling", m_gpuCullingState.instances.size());
    }
}

void Renderer::DispatchGPUCulling() {
    if (!m_services.gpuCulling || m_gpuCullingState.instances.empty()) return;

    // Upload instances to GPU
    auto uploadResult = m_services.gpuCulling->UpdateInstances(m_commandResources.graphicsList.Get(), m_gpuCullingState.instances);
    if (uploadResult.IsErr()) {
        spdlog::warn("GPU culling upload failed: {}", uploadResult.Error());
        return;
    }

    // Dispatch culling compute shader
    if (m_services.descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
        m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    }
    auto cullResult = m_services.gpuCulling->DispatchCulling(
        m_commandResources.graphicsList.Get(),
        m_constantBuffers.frameCPU.viewProjectionNoJitter,
        glm::vec3(m_constantBuffers.frameCPU.cameraPosition)
    );

    if (cullResult.IsErr()) {
        spdlog::warn("GPU culling dispatch failed: {}", cullResult.Error());
    }
}

} // namespace Cortex::Graphics
