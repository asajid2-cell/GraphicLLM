#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/Renderer_VisibilityBufferMaterialKey.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

void Renderer::CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry) {
    if (!registry || !m_services.visibilityBuffer) return;

    m_visibilityBufferState.ClearDrawInputs();

    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
    if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
        snapshot = &localSnapshot;
    }

    // Map mesh pointers to their draw info index (to avoid duplicates)
    std::unordered_map<const Scene::MeshData*, uint32_t> meshToDrawIndex;
    // Per-mesh instance buckets to guarantee each mesh draws only its own instances.
    std::vector<std::vector<VBInstanceData>> opaqueInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> opaqueDoubleSidedInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> alphaMaskedInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> alphaMaskedDoubleSidedInstancesPerMesh;

    // Stable snapshot order so per-instance/material indices don't thrash frame-to-frame.
    std::vector<uint32_t> stableEntryIndices;
    stableEntryIndices.reserve(snapshot->entries.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(snapshot->entries.size()); ++i) {
        if (snapshot->entries[i].hasTransform) {
            stableEntryIndices.push_back(i);
        }
    }

    // Collect unique valid meshes and assign draw indices from stable mesh
    // properties. Raw pointer order is only a final tie-breaker; relying on it
    // as the primary key makes captures noisy across process launches and can
    // hide real visibility-buffer ordering regressions.
    std::vector<std::shared_ptr<Scene::MeshData>> uniqueValidMeshes;

    // ========================================================================
    // PRE-PASS: Collect unique valid meshes and sort by pointer address.
    // This ensures mesh indices are STABLE regardless of entity iteration order,
    // preventing "random terrain" glitching when chunks load/unload.
    // ========================================================================
    {
        std::unordered_set<const Scene::MeshData*> seenMeshes;
        for (uint32_t entryIndex : stableEntryIndices) {
            const RendererSceneRenderable& sceneEntry = snapshot->entries[entryIndex];
            auto& renderable = *sceneEntry.renderable;
            if (!WritesSceneDepth(sceneEntry.depthClass)) continue;
            if (!sceneEntry.hasGpuBuffers) continue;
            // Also check for valid SRV indices (same criteria as main loop)
            if (renderable.mesh->gpuBuffers->vbRawSRVIndex == MeshBuffers::kInvalidDescriptorIndex ||
                renderable.mesh->gpuBuffers->ibRawSRVIndex == MeshBuffers::kInvalidDescriptorIndex) continue;

            // Only add meshes we haven't seen yet
            if (!renderable.mesh->hasBounds) {
                renderable.mesh->UpdateBounds();
            }

            if (seenMeshes.insert(renderable.mesh.get()).second) {
                uniqueValidMeshes.push_back(renderable.mesh);
            }
        }

        auto compareVec3 = [](const glm::vec3& a, const glm::vec3& b) {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        };

        // Sort meshes by authored geometry characteristics first. Pointer
        // order is retained only for truly identical/generated meshes.
        std::sort(uniqueValidMeshes.begin(), uniqueValidMeshes.end(),
                  [&](const std::shared_ptr<Scene::MeshData>& a, const std::shared_ptr<Scene::MeshData>& b) {
                      if (a->kind != b->kind) {
                          return static_cast<uint32_t>(a->kind) < static_cast<uint32_t>(b->kind);
                      }
                      if (a->positions.size() != b->positions.size()) return a->positions.size() < b->positions.size();
                      if (a->indices.size() != b->indices.size()) return a->indices.size() < b->indices.size();
                      if (a->boundsRadius != b->boundsRadius) return a->boundsRadius < b->boundsRadius;
                      if (a->boundsMin != b->boundsMin) return compareVec3(a->boundsMin, b->boundsMin);
                      if (a->boundsMax != b->boundsMax) return compareVec3(a->boundsMax, b->boundsMax);
                      return reinterpret_cast<uintptr_t>(a.get()) < reinterpret_cast<uintptr_t>(b.get());
                  });

        // Pre-build meshToDrawIndex and m_visibilityBufferState.meshDraws in sorted order
        for (size_t i = 0; i < uniqueValidMeshes.size(); ++i) {
            const auto& mesh = uniqueValidMeshes[i];
            meshToDrawIndex[mesh.get()] = static_cast<uint32_t>(i);

            VisibilityBufferRenderer::VBMeshDrawInfo drawInfo{};
            drawInfo.vertexBuffer = mesh->gpuBuffers->vertexBuffer.Get();
            drawInfo.indexBuffer = mesh->gpuBuffers->indexBuffer.Get();
            drawInfo.vertexCount = static_cast<uint32_t>(mesh->positions.size());
            drawInfo.indexCount = static_cast<uint32_t>(mesh->indices.size());
            drawInfo.firstIndex = 0;
            drawInfo.baseVertex = 0;
            drawInfo.startInstance = 0;
            drawInfo.instanceCount = 0;
            drawInfo.startInstanceDoubleSided = 0;
            drawInfo.instanceCountDoubleSided = 0;
            drawInfo.startInstanceAlpha = 0;
            drawInfo.instanceCountAlpha = 0;
            drawInfo.startInstanceAlphaDoubleSided = 0;
            drawInfo.instanceCountAlphaDoubleSided = 0;
            drawInfo.vertexBufferIndex = mesh->gpuBuffers->vbRawSRVIndex;
            drawInfo.indexBufferIndex = mesh->gpuBuffers->ibRawSRVIndex;
            drawInfo.vertexStrideBytes = mesh->gpuBuffers->vertexStrideBytes;
            drawInfo.indexFormat = mesh->gpuBuffers->indexFormat;

            m_visibilityBufferState.meshDraws.push_back(drawInfo);
        }

        // Pre-size instance buckets to match mesh count
        opaqueInstancesPerMesh.resize(uniqueValidMeshes.size());
        opaqueDoubleSidedInstancesPerMesh.resize(uniqueValidMeshes.size());
        alphaMaskedInstancesPerMesh.resize(uniqueValidMeshes.size());
        alphaMaskedDoubleSidedInstancesPerMesh.resize(uniqueValidMeshes.size());
    }
    // ========================================================================

    // Maintain stable packed culling IDs for occlusion history indexing.
    // IDs are packed as (generation << 16) | slot, where generation increments
    // whenever a slot is recycled to prevent history smear.
    const uint32_t maxCullingIds = (m_services.gpuCulling ? m_services.gpuCulling->GetMaxInstances() : 65536u);
    {
        std::unordered_set<entt::entity, GpuCullingEntityHash> alive;
        alive.reserve(stableEntryIndices.size());
        for (uint32_t entryIndex : stableEntryIndices) {
            alive.insert(snapshot->entries[entryIndex].entity);
        }

        for (auto it = m_gpuCullingState.idByEntity.begin(); it != m_gpuCullingState.idByEntity.end();) {
            if (alive.find(it->first) == alive.end()) {
                const uint32_t packedId = it->second;
                const uint32_t slot = (packedId & 0xFFFFu);
                if (slot < m_gpuCullingState.idGeneration.size()) {
                    m_gpuCullingState.idGeneration[slot] = static_cast<uint16_t>(m_gpuCullingState.idGeneration[slot] + 1u);
                }
                m_gpuCullingState.idFreeList.push_back(slot);
                m_gpuCullingState.previousCenterByEntity.erase(it->first);
                if (m_gpuCullingState.previousWorldByEntity.erase(it->first) > 0) {
                    ++m_frameDiagnostics.contract.motionVectors.prunedPreviousWorldMatrices;
                }
                it = m_gpuCullingState.idByEntity.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto getOrAllocateCullingId = [&](entt::entity e) -> uint32_t {
        auto it = m_gpuCullingState.idByEntity.find(e);
        if (it != m_gpuCullingState.idByEntity.end()) {
            return it->second;
        }

        uint32_t slot = UINT32_MAX;
        if (!m_gpuCullingState.idFreeList.empty()) {
            slot = m_gpuCullingState.idFreeList.back();
            m_gpuCullingState.idFreeList.pop_back();
        } else {
            slot = m_gpuCullingState.nextId++;
        }

        if (slot >= maxCullingIds || slot >= 65536u) {
            return UINT32_MAX;
        }

        if (m_gpuCullingState.idGeneration.size() <= slot) {
            m_gpuCullingState.idGeneration.resize(static_cast<size_t>(slot) + 1u, 0u);
        }
        const uint16_t gen = m_gpuCullingState.idGeneration[slot];
        const uint32_t packedId = (static_cast<uint32_t>(gen) << 16u) | (slot & 0xFFFFu);
        m_gpuCullingState.idByEntity.emplace(e, packedId);
        return packedId;
    };

    // Build a per-frame material table (milestone: constant + bindless texture indices).
    std::unordered_map<VisibilityBufferMaterialKey, uint32_t, VisibilityBufferMaterialKeyHasher> materialToIndex;
    std::vector<VBMaterialConstants> vbMaterials;
    vbMaterials.reserve(stableEntryIndices.size());

    auto ensureMeshBindlessSrvs = [&](const std::shared_ptr<Scene::MeshData>& mesh) {
        if (!mesh || !mesh->gpuBuffers || !m_services.descriptorManager || !m_services.device) {
            return;
        }
        auto& gpu = *mesh->gpuBuffers;
        if (!gpu.vertexBuffer || !gpu.indexBuffer) {
            return;
        }
        if (gpu.vbRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex &&
            gpu.ibRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex) {
            return;
        }

        auto srvResult = MeshUploadResourceState::EnsureRawSRVs(
            m_services.device->GetDevice(),
            m_services.descriptorManager.get(),
            gpu);
        if (srvResult.IsErr()) {
            spdlog::warn("VB: {}", srvResult.Error());
            return;
        }
    };

    // Counters for debugging missing geometry
    static bool s_loggedCounts = false;
    uint32_t countTotal = 0;
    uint32_t countSkippedVisible = 0;
    uint32_t countSkippedMesh = 0;
    uint32_t countSkippedLayer = 0;
    uint32_t countSkippedTransparent = 0;
    uint32_t countSkippedBuffers = 0;
    uint32_t countSkippedSRV = 0;
    for (uint32_t entryIndex : stableEntryIndices) {
        countTotal++;
        const RendererSceneRenderable& sceneEntry = snapshot->entries[entryIndex];
        auto& renderable = *sceneEntry.renderable;
        const entt::entity entity = sceneEntry.entity;

        if (!renderable.visible) { countSkippedVisible++; continue; }
        if (!renderable.mesh) { countSkippedMesh++; continue; }
        const RenderableDepthClass depthClass = sceneEntry.depthClass;
        if (!WritesSceneDepth(depthClass)) {
            if (depthClass == RenderableDepthClass::OverlayDepthTestedNoWrite) {
                countSkippedLayer++;
            } else {
                countSkippedTransparent++;
            }
            continue;
        }
        if (!renderable.mesh->gpuBuffers ||
            !renderable.mesh->gpuBuffers->vertexBuffer ||
            !renderable.mesh->gpuBuffers->indexBuffer) {
            if (!renderable.mesh->positions.empty() && !renderable.mesh->indices.empty()) {
                // Use a per-frame upload tracking set instead of a static one to allow retries
                // on subsequent frames if previous uploads failed or are still pending.
                static std::unordered_map<const Scene::MeshData*, uint32_t> s_uploadAttempts;
                static uint32_t s_lastFrameIndex = 0;

                // Reset retry tracking if this is a new frame
                if (m_frameRuntime.frameIndex != s_lastFrameIndex) {
                    s_lastFrameIndex = m_frameRuntime.frameIndex;
                    // Clear meshes that have been trying for too long (stale entries)
                    for (auto it = s_uploadAttempts.begin(); it != s_uploadAttempts.end(); ) {
                        if (m_frameRuntime.frameIndex - it->second > 60) { // Allow 60 frames of retry
                            it = s_uploadAttempts.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                auto [it, inserted] = s_uploadAttempts.try_emplace(renderable.mesh.get(), m_frameRuntime.frameIndex);
                if (inserted || (m_frameRuntime.frameIndex - it->second) > 5) { // Retry every 5 frames
                    it->second = m_frameRuntime.frameIndex;
                    auto enqueue = EnqueueMeshUpload(renderable.mesh, "AutoMeshUpload");
                    if (enqueue.IsErr()) {
                        spdlog::warn("CollectInstancesForVisibilityBuffer: auto mesh upload enqueue failed for mesh at {:p}: {}",
                            static_cast<const void*>(renderable.mesh.get()), enqueue.Error());
                    }
                }
            }
            countSkippedBuffers++;
            continue;
        }

        ensureMeshBindlessSrvs(renderable.mesh);
        if (renderable.mesh->gpuBuffers &&
            (renderable.mesh->gpuBuffers->vbRawSRVIndex == MeshBuffers::kInvalidDescriptorIndex ||
             renderable.mesh->gpuBuffers->ibRawSRVIndex == MeshBuffers::kInvalidDescriptorIndex)) {
            // VB resolve requires bindless SRV indices for the mesh buffers; skip until available.
            countSkippedSRV++;
            continue;
        }

        // Lookup pre-built mesh draw index (built in sorted pre-pass for stability)
        auto it = meshToDrawIndex.find(renderable.mesh.get());
        if (it == meshToDrawIndex.end()) {
            // Mesh wasn't in pre-pass (shouldn't happen, same criteria used)
            continue;
        }
        const uint32_t meshDrawIndex = it->second;

        // Ensure textures are queued/loaded. Descriptor tables are warmed via
        // PrewarmMaterialDescriptors() early in the frame to avoid mid-frame
        // persistent allocations (which can stall or fail once transient
        // allocations have started).
        EnsureMaterialTextures(renderable);

        const MaterialTextureFallbacks materialFallbacks{
            m_materialFallbacks.albedo.get(),
            m_materialFallbacks.normal.get(),
            m_materialFallbacks.metallic.get(),
            m_materialFallbacks.roughness.get()
        };
        const MaterialModel materialModel = MaterialResolver::ResolveRenderable(renderable, materialFallbacks);
        const uint32_t materialClass = ClassifyMaterialSurface(materialModel);

        glm::uvec4 textureIndices(kInvalidBindlessIndex);
        glm::uvec4 textureIndices2(kInvalidBindlessIndex);
        glm::uvec4 textureIndices3(kInvalidBindlessIndex);
        glm::uvec4 textureIndices4(kInvalidBindlessIndex);
        if (renderable.textures.gpuState) {
            const auto& desc = renderable.textures.gpuState->descriptors;
            textureIndices = glm::uvec4(
                (materialModel.textures.albedo && desc[0].IsValid()) ? desc[0].index : kInvalidBindlessIndex,
                (materialModel.textures.normal && desc[1].IsValid()) ? desc[1].index : kInvalidBindlessIndex,
                (materialModel.textures.metallic && desc[2].IsValid()) ? desc[2].index : kInvalidBindlessIndex,
                (materialModel.textures.roughness && desc[3].IsValid()) ? desc[3].index : kInvalidBindlessIndex
            );
            textureIndices2 = glm::uvec4(
                (materialModel.textures.occlusion && desc[4].IsValid()) ? desc[4].index : kInvalidBindlessIndex,
                (materialModel.textures.emissive && desc[5].IsValid()) ? desc[5].index : kInvalidBindlessIndex,
                kInvalidBindlessIndex,
                kInvalidBindlessIndex
            );
            textureIndices3 = glm::uvec4(
                (materialModel.textures.transmission && desc[6].IsValid()) ? desc[6].index : kInvalidBindlessIndex,
                (materialModel.textures.clearcoat && desc[7].IsValid()) ? desc[7].index : kInvalidBindlessIndex,
                (materialModel.textures.clearcoatRoughness && desc[8].IsValid()) ? desc[8].index : kInvalidBindlessIndex,
                (materialModel.textures.specular && desc[9].IsValid()) ? desc[9].index : kInvalidBindlessIndex
            );
            textureIndices4 = glm::uvec4(
                (materialModel.textures.specularColor && desc[10].IsValid()) ? desc[10].index : kInvalidBindlessIndex,
                kInvalidBindlessIndex,
                kInvalidBindlessIndex,
                kInvalidBindlessIndex
            );
        }

        // Find or create material index for this renderable.
        uint32_t materialIndex = 0;
        {
            VisibilityBufferMaterialKey key = MakeVisibilityBufferMaterialKey(materialModel,
                                                                              textureIndices,
                                                                              textureIndices2,
                                                                              textureIndices3,
                                                                              textureIndices4,
                                                                              materialClass);
            auto mit = materialToIndex.find(key);
            if (mit == materialToIndex.end()) {
                materialIndex = static_cast<uint32_t>(vbMaterials.size());
                materialToIndex.emplace(key, materialIndex);

                VBMaterialConstants mat = MaterialResolver::BuildVBMaterialConstants(
                    materialModel,
                    textureIndices,
                    textureIndices2,
                    textureIndices3,
                    textureIndices4,
                    materialClass);
                vbMaterials.push_back(mat);
            } else {
                materialIndex = mit->second;
            }
        }

        // Build instance data
        VBInstanceData inst{};
        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }

        glm::mat4 currWorld = sceneEntry.worldMatrix;
        const uint32_t entityKey = static_cast<uint32_t>(entity);
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, currWorld, entityKey);
        ApplyAutoDepthOffset(currWorld, sep.worldOffset);
        auto prevIt = m_gpuCullingState.previousWorldByEntity.find(entity);
        const bool hasPreviousWorld = prevIt != m_gpuCullingState.previousWorldByEntity.end();
        const glm::mat4 prevWorld = hasPreviousWorld ? prevIt->second : currWorld;
        if (hasPreviousWorld) {
            ++m_frameDiagnostics.contract.motionVectors.previousWorldMatrices;
            const glm::vec3 currTranslation = glm::vec3(currWorld[3]);
            const glm::vec3 prevTranslation = glm::vec3(prevWorld[3]);
            m_frameDiagnostics.contract.motionVectors.maxObjectMotionWorld =
                std::max(m_frameDiagnostics.contract.motionVectors.maxObjectMotionWorld,
                         glm::length(currTranslation - prevTranslation));
        } else {
            ++m_frameDiagnostics.contract.motionVectors.seededPreviousWorldMatrices;
        }
        m_gpuCullingState.previousWorldByEntity[entity] = currWorld;

        inst.worldMatrix = currWorld;
        inst.prevWorldMatrix = prevWorld;
        inst.normalMatrix = sceneEntry.normalMatrix;
        inst.meshIndex = meshDrawIndex;  // Index into mesh draw array
        inst.materialIndex = materialIndex;
        inst.firstIndex = 0;
        inst.indexCount = static_cast<uint32_t>(renderable.mesh->indices.size());
        inst.baseVertex = 0;
        inst._padAlign[0] = 0; inst._padAlign[1] = 0; inst._padAlign[2] = 0; // Explicitly zero padding
        inst.flags = 0u;
        inst.cullingId = getOrAllocateCullingId(entity);
        inst.depthBiasNdc = sep.depthBiasNdc;
        inst._pad0 = 0u;

        // Bounding sphere in object space (used for GPU occlusion culling).
        if (renderable.mesh->hasBounds) {
            inst.boundingSphere = glm::vec4(renderable.mesh->boundsCenter, renderable.mesh->boundsRadius);
        } else {
            inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
        }

        // Previous center for motion-inflated occlusion tests (stored in world space).
        glm::vec3 currCenterWS = glm::vec3(currWorld[3]);
        if (renderable.mesh->hasBounds) {
            currCenterWS = glm::vec3(currWorld * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
        }
        glm::vec3 prevCenterWS = currCenterWS;
        auto prevCenterIt = m_gpuCullingState.previousCenterByEntity.find(entity);
        if (prevCenterIt != m_gpuCullingState.previousCenterByEntity.end()) {
            prevCenterWS = prevCenterIt->second;
        }
        m_gpuCullingState.previousCenterByEntity[entity] = currCenterWS;
        inst.prevCenterWS = glm::vec4(prevCenterWS, 0.0f);

        if (IsAlphaTestedDepthClass(depthClass)) {
            if (IsDoubleSidedDepthClass(depthClass)) {
                alphaMaskedDoubleSidedInstancesPerMesh[meshDrawIndex].push_back(inst);
            } else {
                alphaMaskedInstancesPerMesh[meshDrawIndex].push_back(inst);
            }
        } else {
            if (IsDoubleSidedDepthClass(depthClass)) {
                opaqueDoubleSidedInstancesPerMesh[meshDrawIndex].push_back(inst);
            } else {
                opaqueInstancesPerMesh[meshDrawIndex].push_back(inst);
            }
        }
    }

    // Flatten per-mesh buckets into a single instance buffer, and record the
    // contiguous range [startInstance, startInstance + instanceCount) for each mesh.
    {
        size_t total = 0;
        for (size_t i = 0; i < opaqueInstancesPerMesh.size(); ++i) {
            total += opaqueInstancesPerMesh[i].size();
            total += opaqueDoubleSidedInstancesPerMesh[i].size();
            total += alphaMaskedInstancesPerMesh[i].size();
            total += alphaMaskedDoubleSidedInstancesPerMesh[i].size();
        }
        m_visibilityBufferState.instances.reserve(total);

        uint32_t start = 0;
        for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(m_visibilityBufferState.meshDraws.size()); ++meshIdx) {
            auto& draw = m_visibilityBufferState.meshDraws[meshIdx];
            const auto& opaqueBucket = opaqueInstancesPerMesh[meshIdx];
            const auto& opaqueDsBucket = opaqueDoubleSidedInstancesPerMesh[meshIdx];
            const auto& alphaBucket = alphaMaskedInstancesPerMesh[meshIdx];
            const auto& alphaDsBucket = alphaMaskedDoubleSidedInstancesPerMesh[meshIdx];

            draw.startInstance = start;
            draw.instanceCount = static_cast<uint32_t>(opaqueBucket.size());

            m_visibilityBufferState.instances.insert(m_visibilityBufferState.instances.end(), opaqueBucket.begin(), opaqueBucket.end());
            start += draw.instanceCount;

            draw.startInstanceDoubleSided = start;
            draw.instanceCountDoubleSided = static_cast<uint32_t>(opaqueDsBucket.size());

            m_visibilityBufferState.instances.insert(m_visibilityBufferState.instances.end(), opaqueDsBucket.begin(), opaqueDsBucket.end());
            start += draw.instanceCountDoubleSided;

            draw.startInstanceAlpha = start;
            draw.instanceCountAlpha = static_cast<uint32_t>(alphaBucket.size());

            m_visibilityBufferState.instances.insert(m_visibilityBufferState.instances.end(), alphaBucket.begin(), alphaBucket.end());
            start += draw.instanceCountAlpha;

            draw.startInstanceAlphaDoubleSided = start;
            draw.instanceCountAlphaDoubleSided = static_cast<uint32_t>(alphaDsBucket.size());

            m_visibilityBufferState.instances.insert(m_visibilityBufferState.instances.end(), alphaDsBucket.begin(), alphaDsBucket.end());
            start += draw.instanceCountAlphaDoubleSided;
        }
    }

    // Upload per-frame material table (used by MaterialResolve.hlsl).


    // Upload per-frame material table (used by MaterialResolve.hlsl).
    auto matResult = m_services.visibilityBuffer->UpdateMaterials(m_commandResources.graphicsList.Get(), vbMaterials);
    if (matResult.IsErr()) {
        spdlog::warn("Failed to update VB material table: {}", matResult.Error());
    }

    // Upload instance data to visibility buffer
    auto uploadResult = m_services.visibilityBuffer->UpdateInstances(m_commandResources.graphicsList.Get(), m_visibilityBufferState.instances);
    if (uploadResult.IsErr()) {
        spdlog::warn("Failed to update visibility buffer instances: {}", uploadResult.Error());
    }
    m_frameDiagnostics.contract.motionVectors.instanceCount = static_cast<uint32_t>(m_visibilityBufferState.instances.size());
    m_frameDiagnostics.contract.motionVectors.meshCount = static_cast<uint32_t>(m_visibilityBufferState.meshDraws.size());

    // Log collection stats on first frame and whenever scene might have changed (significantly different total)
    static uint32_t s_lastLoggedTotal = 0;
    if ((!s_loggedCounts || countTotal != s_lastLoggedTotal) && countTotal > 0) {
        s_loggedCounts = true;
        s_lastLoggedTotal = countTotal;
        spdlog::info("VB Collect Stats: Total={} Skipped[Vis={} Mesh={} Layer={} Transp={} Buf={} SRV={}] Collected={}",
            countTotal, countSkippedVisible, countSkippedMesh, countSkippedLayer, countSkippedTransparent, countSkippedBuffers, countSkippedSRV, m_visibilityBufferState.instances.size());

        // If objects are being skipped, log a warning so it's obvious
        if (countSkippedBuffers > 0 || countSkippedSRV > 0) {
            spdlog::warn("VB: {} objects skipped (Buf={} SRV={}) - some geometry may not render until mesh uploads complete",
                countSkippedBuffers + countSkippedSRV, countSkippedBuffers, countSkippedSRV);
        }
    }
}

} // namespace Cortex::Graphics
