#include "Graphics/Subsystems/VisibilityBufferSubsystem.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/Renderer_VisibilityBufferMaterialKey.h"
#include "Graphics/SurfaceClassification.h"
#include "Graphics/FrameContract.h"
#include "Graphics/RendererSceneSnapshot.h"
#include "Graphics/RendererUploadState.h"
#include "Graphics/GPUCulling.h"
#include "Graphics/RendererGPUCullingState.h"
#include "Graphics/Renderer_DiagnosticsTypes.h"
#include "Graphics/RendererMaterialTextureState.h"
#include "Graphics/RendererDepthState.h"
#include "Graphics/RendererMainTargetState.h"
#include "Graphics/RendererConstantBufferState.h"
#include "Graphics/RendererFramePlanningState.h"
#include "Graphics/RendererEnvironmentState.h"
#include "Graphics/Subsystems/HZBSubsystem.h"
#include "Graphics/Subsystems/ShadowSubsystem.h"
#include "Graphics/Passes/VisibilityBufferResourcePass.h"
#include "Graphics/RHI/DX12Device.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr uint32_t kVBDebugNone = 0;
constexpr uint32_t kVBDebugVisibility = 1;
constexpr uint32_t kVBDebugDepth = 2;
constexpr uint32_t kVBDebugGBufferAlbedo = 3;
constexpr uint32_t kVBDebugGBufferNormal = 4;
constexpr uint32_t kVBDebugGBufferEmissive = 5;
constexpr uint32_t kVBDebugGBufferExt0 = 6;
constexpr uint32_t kVBDebugGBufferExt1 = 7;
constexpr uint32_t kVBDebugGBufferExt2 = 8;
constexpr uint32_t kVBDebugMaterialId = 9;
constexpr uint32_t kVBDebugStableObjectId = 10;
constexpr uint32_t kVBDebugMaterialFamily = 11;
constexpr uint32_t kVBDebugReflectionPolicy = 12;
constexpr uint32_t kVBDebugTemporalPolicy = 13;
constexpr uint32_t kVBDebugPostSensitivity = 14;
constexpr uint32_t kVBDebugMaterialMissingChannelMask = 20;
constexpr uint32_t kVBDebugLightingV3Direct = 15;
constexpr uint32_t kVBDebugLightingV3DirectUnshadowed = 16;
constexpr uint32_t kVBDebugLightingV3ShadowVisibility = 17;
constexpr uint32_t kVBDebugLightingV3ShadowLoss = 18;
constexpr uint32_t kVBDebugLightingV3Indirect = 19;
constexpr uint32_t kVBDebugLightingV3EnergyBudget = 21;
constexpr uint32_t kVBDebugLightingV3ShadowAttribution = 22;

bool IsVisibilityBufferDebugView(uint32_t debugView) {
    return debugView != kVBDebugNone;
}

bool IsVisibilityBufferUnculledDebugView(uint32_t debugView) {
    return debugView == kVBDebugVisibility ||
           debugView == kVBDebugDepth ||
           debugView == kVBDebugMaterialId ||
           debugView == kVBDebugStableObjectId ||
           debugView == kVBDebugMaterialFamily ||
           debugView == kVBDebugReflectionPolicy ||
           debugView == kVBDebugTemporalPolicy ||
           debugView == kVBDebugPostSensitivity ||
           debugView == kVBDebugMaterialMissingChannelMask;
}

bool IsVisibilityBufferGBufferDebugView(uint32_t debugView) {
    return debugView >= kVBDebugGBufferAlbedo && debugView <= kVBDebugGBufferExt2;
}

bool DisableVisibleBackgroundFromEnv() {
    const char* value = std::getenv("CORTEX_DISABLE_VISIBLE_BACKGROUND");
    return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

} // namespace

void VisibilityBufferSubsystem::CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry,
                                                                    const VisibilityBufferContext& ctx) {
    if (!registry || !ctx.visibilityBuffer) return;

    m_state.ClearDrawInputs();

    RendererSceneSnapshot localSnapshot{};
    const RendererSceneSnapshot* snapshot = &ctx.framePlanning->sceneSnapshot;
    if (!snapshot->IsValidForFrame(ctx.renderFrameCounter)) {
        localSnapshot = BuildRendererSceneSnapshot(registry, ctx.renderFrameCounter);
        snapshot = &localSnapshot;
    }

    // Map mesh pointers to their draw info index (to avoid duplicates)
    std::unordered_map<const Scene::MeshData*, uint32_t> meshToDrawIndex;
    // Per-mesh instance buckets to guarantee each mesh draws only its own instances.
    std::vector<std::vector<VBInstanceData>> opaqueInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> opaqueDoubleSidedInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> alphaMaskedInstancesPerMesh;
    std::vector<std::vector<VBInstanceData>> alphaMaskedDoubleSidedInstancesPerMesh;

    auto ensureMeshBindlessSrvs = [&](const std::shared_ptr<Scene::MeshData>& mesh) {
        if (!mesh || !mesh->gpuBuffers || !ctx.descriptorManager || !ctx.device) {
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
            ctx.device->GetDevice(),
            ctx.descriptorManager,
            gpu);
        if (srvResult.IsErr()) {
            spdlog::warn("VB: {}", srvResult.Error());
            return;
        }
    };

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
    // PRE-PASS: Collect unique valid meshes and sort by stable mesh properties.
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
            ensureMeshBindlessSrvs(renderable.mesh);
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

        // Pre-build meshToDrawIndex and m_state.meshDraws in sorted order
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

            m_state.meshDraws.push_back(drawInfo);
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
    const uint32_t maxCullingIds = (ctx.gpuCulling ? ctx.gpuCulling->GetMaxInstances() : 65536u);
    {
        std::unordered_set<entt::entity, GpuCullingEntityHash> alive;
        alive.reserve(stableEntryIndices.size());
        for (uint32_t entryIndex : stableEntryIndices) {
            alive.insert(snapshot->entries[entryIndex].entity);
        }

        for (auto it = ctx.gpuCullingState->idByEntity.begin(); it != ctx.gpuCullingState->idByEntity.end();) {
            if (alive.find(it->first) == alive.end()) {
                const uint32_t packedId = it->second;
                const uint32_t slot = (packedId & 0xFFFFu);
                if (slot < ctx.gpuCullingState->idGeneration.size()) {
                    ctx.gpuCullingState->idGeneration[slot] = static_cast<uint16_t>(ctx.gpuCullingState->idGeneration[slot] + 1u);
                }
                ctx.gpuCullingState->idFreeList.push_back(slot);
                ctx.gpuCullingState->previousCenterByEntity.erase(it->first);
                if (ctx.gpuCullingState->previousWorldByEntity.erase(it->first) > 0) {
                    ++ctx.frameDiagnostics->contract.motionVectors.prunedPreviousWorldMatrices;
                }
                it = ctx.gpuCullingState->idByEntity.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto getOrAllocateCullingId = [&](entt::entity e) -> uint32_t {
        auto it = ctx.gpuCullingState->idByEntity.find(e);
        if (it != ctx.gpuCullingState->idByEntity.end()) {
            return it->second;
        }

        uint32_t slot = UINT32_MAX;
        if (!ctx.gpuCullingState->idFreeList.empty()) {
            slot = ctx.gpuCullingState->idFreeList.back();
            ctx.gpuCullingState->idFreeList.pop_back();
        } else {
            slot = ctx.gpuCullingState->nextId++;
        }

        if (slot >= maxCullingIds || slot >= 65536u) {
            return UINT32_MAX;
        }

        if (ctx.gpuCullingState->idGeneration.size() <= slot) {
            ctx.gpuCullingState->idGeneration.resize(static_cast<size_t>(slot) + 1u, 0u);
        }
        const uint16_t gen = ctx.gpuCullingState->idGeneration[slot];
        const uint32_t packedId = (static_cast<uint32_t>(gen) << 16u) | (slot & 0xFFFFu);
        ctx.gpuCullingState->idByEntity.emplace(e, packedId);
        return packedId;
    };

    // Build a per-frame material table (milestone: constant + bindless texture indices).
    std::unordered_map<VisibilityBufferMaterialKey, uint32_t, VisibilityBufferMaterialKeyHasher> materialToIndex;
    std::vector<VBMaterialConstants> vbMaterials;
    vbMaterials.reserve(stableEntryIndices.size());

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
                if (ctx.frameIndex != s_lastFrameIndex) {
                    s_lastFrameIndex = ctx.frameIndex;
                    // Clear meshes that have been trying for too long (stale entries)
                    for (auto it = s_uploadAttempts.begin(); it != s_uploadAttempts.end(); ) {
                        if (ctx.frameIndex - it->second > 60) { // Allow 60 frames of retry
                            it = s_uploadAttempts.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                auto [it, inserted] = s_uploadAttempts.try_emplace(renderable.mesh.get(), ctx.frameIndex);
                if (inserted || (ctx.frameIndex - it->second) > 5) { // Retry every 5 frames
                    it->second = ctx.frameIndex;
                    auto enqueue = ctx.enqueueMeshUpload(renderable.mesh, "AutoMeshUpload");
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

        ctx.prepareMaterialResources(renderable);

        const MaterialTextureFallbacks materialFallbacks{
            ctx.materialFallbacks->albedo.get(),
            ctx.materialFallbacks->normal.get(),
            ctx.materialFallbacks->metallic.get(),
            ctx.materialFallbacks->roughness.get()
        };
        const MaterialModel materialModel = MaterialResolver::ResolveRenderable(renderable, materialFallbacks);
        const uint32_t materialClass = ClassifyMaterialSurface(materialModel);

        MaterialConstants materialTextureState = MaterialResolver::BuildMaterialConstants(materialModel);
        MaterialResolver::FillMaterialTextureIndices(renderable, materialTextureState);
        const glm::uvec4 textureIndices = materialTextureState.textureIndices;
        const glm::uvec4 textureIndices2 = materialTextureState.textureIndices2;
        const glm::uvec4 textureIndices3 = materialTextureState.textureIndices3;
        const glm::uvec4 textureIndices4 = materialTextureState.textureIndices4;

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
        auto prevIt = ctx.gpuCullingState->previousWorldByEntity.find(entity);
        const bool hasPreviousWorld = prevIt != ctx.gpuCullingState->previousWorldByEntity.end();
        const glm::mat4 prevWorld = hasPreviousWorld ? prevIt->second : currWorld;
        if (hasPreviousWorld) {
            ++ctx.frameDiagnostics->contract.motionVectors.previousWorldMatrices;
            const glm::vec3 currTranslation = glm::vec3(currWorld[3]);
            const glm::vec3 prevTranslation = glm::vec3(prevWorld[3]);
            ctx.frameDiagnostics->contract.motionVectors.maxObjectMotionWorld =
                std::max(ctx.frameDiagnostics->contract.motionVectors.maxObjectMotionWorld,
                         glm::length(currTranslation - prevTranslation));
        } else {
            ++ctx.frameDiagnostics->contract.motionVectors.seededPreviousWorldMatrices;
        }
        ctx.gpuCullingState->previousWorldByEntity[entity] = currWorld;

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
        auto prevCenterIt = ctx.gpuCullingState->previousCenterByEntity.find(entity);
        if (prevCenterIt != ctx.gpuCullingState->previousCenterByEntity.end()) {
            prevCenterWS = prevCenterIt->second;
        }
        ctx.gpuCullingState->previousCenterByEntity[entity] = currCenterWS;
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
        m_state.instances.reserve(total);

        uint32_t start = 0;
        for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(m_state.meshDraws.size()); ++meshIdx) {
            auto& draw = m_state.meshDraws[meshIdx];
            const auto& opaqueBucket = opaqueInstancesPerMesh[meshIdx];
            const auto& opaqueDsBucket = opaqueDoubleSidedInstancesPerMesh[meshIdx];
            const auto& alphaBucket = alphaMaskedInstancesPerMesh[meshIdx];
            const auto& alphaDsBucket = alphaMaskedDoubleSidedInstancesPerMesh[meshIdx];

            draw.startInstance = start;
            draw.instanceCount = static_cast<uint32_t>(opaqueBucket.size());

            m_state.instances.insert(m_state.instances.end(), opaqueBucket.begin(), opaqueBucket.end());
            start += draw.instanceCount;

            draw.startInstanceDoubleSided = start;
            draw.instanceCountDoubleSided = static_cast<uint32_t>(opaqueDsBucket.size());

            m_state.instances.insert(m_state.instances.end(), opaqueDsBucket.begin(), opaqueDsBucket.end());
            start += draw.instanceCountDoubleSided;

            draw.startInstanceAlpha = start;
            draw.instanceCountAlpha = static_cast<uint32_t>(alphaBucket.size());

            m_state.instances.insert(m_state.instances.end(), alphaBucket.begin(), alphaBucket.end());
            start += draw.instanceCountAlpha;

            draw.startInstanceAlphaDoubleSided = start;
            draw.instanceCountAlphaDoubleSided = static_cast<uint32_t>(alphaDsBucket.size());

            m_state.instances.insert(m_state.instances.end(), alphaDsBucket.begin(), alphaDsBucket.end());
            start += draw.instanceCountAlphaDoubleSided;
        }
    }

    // Upload per-frame material table (used by MaterialResolve.hlsl).
    auto matResult = ctx.visibilityBuffer->UpdateMaterials(ctx.commandList, vbMaterials);
    if (matResult.IsErr()) {
        spdlog::warn("Failed to update VB material table: {}", matResult.Error());
    }

    // Upload instance data to visibility buffer
    auto uploadResult = ctx.visibilityBuffer->UpdateInstances(ctx.commandList, m_state.instances);
    if (uploadResult.IsErr()) {
        spdlog::warn("Failed to update visibility buffer instances: {}", uploadResult.Error());
    }
    const uint32_t invalidStableIds = static_cast<uint32_t>(std::count_if(
        m_state.instances.begin(),
        m_state.instances.end(),
        [](const VBInstanceData& instance) {
            return instance.cullingId == std::numeric_limits<uint32_t>::max();
        }));
    ctx.frameDiagnostics->contract.drawCounts.visibilityBufferMaterials = static_cast<uint32_t>(vbMaterials.size());
    ctx.frameDiagnostics->contract.drawCounts.visibilityBufferInvalidStableIds = invalidStableIds;
    ctx.frameDiagnostics->contract.motionVectors.instanceCount = static_cast<uint32_t>(m_state.instances.size());
    ctx.frameDiagnostics->contract.motionVectors.meshCount = static_cast<uint32_t>(m_state.meshDraws.size());

    // Log collection stats on first frame and whenever scene might have changed (significantly different total)
    static uint32_t s_lastLoggedTotal = 0;
    if ((!s_loggedCounts || countTotal != s_lastLoggedTotal) && countTotal > 0) {
        s_loggedCounts = true;
        s_lastLoggedTotal = countTotal;
        spdlog::info("VB Collect Stats: Total={} Skipped[Vis={} Mesh={} Layer={} Transp={} Buf={} SRV={}] Collected={}",
            countTotal, countSkippedVisible, countSkippedMesh, countSkippedLayer, countSkippedTransparent, countSkippedBuffers, countSkippedSRV, m_state.instances.size());

        // If objects are being skipped, log a warning so it's obvious
        if (countSkippedBuffers > 0 || countSkippedSRV > 0) {
            spdlog::warn("VB: {} objects skipped (Buf={} SRV={}) - some geometry may not render until mesh uploads complete",
                countSkippedBuffers + countSkippedSRV, countSkippedBuffers, countSkippedSRV);
        }
    }
}

uint32_t VisibilityBufferSubsystem::GetVisibilityBufferDebugView(uint32_t rendererDebugMode) const {
    switch (rendererDebugMode) {
        case 33u: return kVBDebugVisibility;
        case 34u: return kVBDebugDepth;
        case 35u: return kVBDebugGBufferAlbedo;
        case 36u: return kVBDebugGBufferNormal;
        case 37u: return kVBDebugGBufferEmissive;
        case 38u: return kVBDebugGBufferExt0;
        case 39u: return kVBDebugGBufferExt1;
        case 40u: return kVBDebugGBufferExt2;
        case 48u: return kVBDebugMaterialId;
        case 49u: return kVBDebugStableObjectId;
        case 50u: return kVBDebugMaterialFamily;
        case 51u: return kVBDebugReflectionPolicy;
        case 52u: return kVBDebugTemporalPolicy;
        case 53u: return kVBDebugPostSensitivity;
        case 82u: return kVBDebugMaterialMissingChannelMask;
        case 62u: return kVBDebugLightingV3Direct;
        case 63u: return kVBDebugLightingV3DirectUnshadowed;
        case 64u: return kVBDebugLightingV3ShadowVisibility;
        case 65u: return kVBDebugLightingV3ShadowLoss;
        case 66u: return kVBDebugLightingV3Indirect;
        case 90u: return kVBDebugLightingV3EnergyBudget;
        case 91u: return kVBDebugLightingV3ShadowAttribution;
        default: return kVBDebugNone;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS VisibilityBufferSubsystem::ResolveVisibilityBufferCullMask(uint32_t debugView,
                                                                                     const VisibilityBufferContext& ctx) {
    D3D12_GPU_VIRTUAL_ADDRESS vbCullMaskAddress = 0;
    if (!ctx.gpuCullingState->enabled || !ctx.gpuCulling || IsVisibilityBufferUnculledDebugView(debugView)) {
        return 0;
    }

    const bool forceVisible = (std::getenv("CORTEX_GPUCULL_FORCE_VISIBLE") != nullptr);
    ctx.gpuCulling->SetForceVisible(forceVisible);

    const uint32_t maxInstances = ctx.gpuCulling->GetMaxInstances();
    if (m_state.instances.size() > maxInstances) {
        spdlog::warn("VB: instance count {} exceeds GPU culling capacity {}; disabling VB cull mask this frame",
                     m_state.instances.size(), maxInstances);
        return 0;
    }

    std::vector<GPUInstanceData> cullInstances;
    cullInstances.reserve(m_state.instances.size());
    for (const auto& vbInst : m_state.instances) {
        GPUInstanceData inst{};
        inst.modelMatrix = vbInst.worldMatrix;
        inst.boundingSphere = vbInst.boundingSphere;
        inst.prevCenterWS = vbInst.prevCenterWS;
        inst.meshIndex = vbInst.meshIndex;
        inst.materialIndex = vbInst.materialIndex;
        inst.flags = vbInst.flags;
        inst.cullingId = vbInst.cullingId;
        cullInstances.push_back(inst);
    }

    auto uploadResult = ctx.gpuCulling->UpdateInstances(ctx.commandList, cullInstances);
    if (uploadResult.IsErr()) {
        spdlog::warn("VB: GPU culling upload failed: {}", uploadResult.Error());
        return 0;
    }

    const bool freezeCullingEnv = (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr);
    const bool freezeCulling = freezeCullingEnv || ctx.gpuCullingState->freeze;

    glm::mat4 viewProjForCulling = ctx.constantBuffers->frameCPU.viewProjectionNoJitter;
    glm::vec3 cameraPosForCulling = glm::vec3(ctx.constantBuffers->frameCPU.cameraPosition);
    if (!freezeCulling) {
        ctx.gpuCullingState->freezeCaptured = false;
    } else {
        if (!ctx.gpuCullingState->freezeCaptured) {
            ctx.gpuCullingState->freezeCaptured = true;
            ctx.gpuCullingState->frozenViewProj = viewProjForCulling;
            ctx.gpuCullingState->frozenCameraPos = cameraPosForCulling;
            spdlog::warn("GPU culling freeze enabled ({}): capturing view on frame {}",
                         freezeCullingEnv ? "env CORTEX_GPUCULL_FREEZE=1" : "K toggle",
                         ctx.renderFrameCounter);
        }
        viewProjForCulling = ctx.gpuCullingState->frozenViewProj;
        cameraPosForCulling = ctx.gpuCullingState->frozenCameraPos;
    }

    static bool s_checkedEnv = false;
    static bool s_disableHzb = false;
    if (!s_checkedEnv) {
        s_checkedEnv = true;
        s_disableHzb = (std::getenv("CORTEX_DISABLE_VB_HZB") != nullptr);
        if (s_disableHzb) {
            spdlog::info("VB: HZB occlusion disabled (CORTEX_DISABLE_VB_HZB=1)");
        }
    }

    bool useHzbOcclusion = false;
    if (!s_disableHzb &&
        ctx.hzb->State().resources.valid && ctx.hzb->State().capture.captureValid && ctx.hzb->State().resources.texture && ctx.hzb->State().resources.mipCount > 0 &&
        (ctx.hzb->State().capture.captureFrameCounter + 1u == ctx.renderFrameCounter)) {
        useHzbOcclusion = true;
    }
    if (freezeCulling) {
        useHzbOcclusion = false;
    }
    if (useHzbOcclusion && ctx.hzb->State().resources.texture) {
        const bool hzbReady = VisibilityBufferResourcePass::PrepareHZBForCulling(
            ctx.commandList,
            {ctx.hzb->State().resources.texture.Get(), &ctx.hzb->State().resources.resourceState});
        if (!hzbReady) {
            useHzbOcclusion = false;
        }
    }
    m_state.hzbOcclusionUsedThisFrame = useHzbOcclusion;

    ctx.gpuCulling->SetHZBForOcclusion(
        useHzbOcclusion ? ctx.hzb->State().resources.texture.Get() : nullptr,
        ctx.hzb->State().resources.width,
        ctx.hzb->State().resources.height,
        ctx.hzb->State().resources.mipCount,
        ctx.hzb->State().capture.captureViewMatrix,
        ctx.hzb->State().capture.captureViewProjMatrix,
        ctx.hzb->State().capture.captureCameraPosWS,
        ctx.hzb->State().capture.captureNearPlane,
        ctx.hzb->State().capture.captureFarPlane,
        useHzbOcclusion);

    static bool s_debugCullingEnv = (std::getenv("CORTEX_DEBUG_CULLING") != nullptr);
    ctx.gpuCulling->SetDebugEnabled(s_debugCullingEnv);

    auto cullResult = ctx.gpuCulling->DispatchCulling(ctx.commandList, viewProjForCulling, cameraPosForCulling);
    if (cullResult.IsErr()) {
        spdlog::warn("VB: GPU culling dispatch failed: {}", cullResult.Error());
    } else if (auto* mask = ctx.gpuCulling->GetVisibilityMaskBuffer()) {
        vbCullMaskAddress = mask->GetGPUVirtualAddress();
    }

    if (s_debugCullingEnv) {
        static uint32_t s_cullLogCounter = 0;
        if ((s_cullLogCounter++ % 60) == 0) {
            auto stats = ctx.gpuCulling->GetDebugStats();
            if (stats.valid) {
                spdlog::info("GPU Cull Stats: tested={} frustumCulled={} occluded={} visible={} (HZB: near={:.2f} hzb={:.2f} mip={} flags={})",
                    stats.tested, stats.frustumCulled, stats.occluded, stats.visible,
                    stats.sampleNearDepth, stats.sampleHzbDepth, stats.sampleMip, stats.sampleFlags);
            }
        }
    }

    if (vbCullMaskAddress != 0 && std::getenv("CORTEX_DISABLE_VB_CULL_MASK") != nullptr) {
        vbCullMaskAddress = 0;
    }
    return vbCullMaskAddress;
}

bool VisibilityBufferSubsystem::RenderVisibilityBufferVisibilityStage(D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress,
                                                                     uint32_t debugView,
                                                                     bool& completedPath,
                                                                     const VisibilityBufferContext& ctx) {
    completedPath = false;
    const bool depthReady = VisibilityBufferResourcePass::PrepareDepthForVisibility(
        ctx.commandList,
        {ctx.depthResources->resources.buffer.Get(), &ctx.depthResources->resources.resourceState});
    if (!depthReady) {
        return false;
    }

    auto visResult = ctx.visibilityBuffer->RenderVisibilityPass(
        ctx.commandList,
        ctx.depthResources->resources.buffer.Get(),
        ctx.depthResources->descriptors.dsv.cpu,
        ctx.constantBuffers->frameCPU.viewProjectionMatrix,
        m_state.meshDraws,
        cullMaskAddress);

    if (visResult.IsErr()) {
        spdlog::error("Visibility pass failed: {}", visResult.Error());
        return false;
    }

    uint32_t vbDrawBatches = 0;
    for (const auto& draw : m_state.meshDraws) {
        vbDrawBatches += (draw.instanceCount > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountDoubleSided > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlpha > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlphaDoubleSided > 0) ? 1u : 0u;
    }
    ctx.frameDiagnostics->contract.drawCounts.visibilityBufferInstances = static_cast<uint32_t>(m_state.instances.size());
    ctx.frameDiagnostics->contract.drawCounts.visibilityBufferMeshes = static_cast<uint32_t>(m_state.meshDraws.size());
    ctx.frameDiagnostics->contract.drawCounts.visibilityBufferDrawBatches = vbDrawBatches;

    if (debugView == kVBDebugVisibility ||
        debugView == kVBDebugMaterialId ||
        debugView == kVBDebugStableObjectId ||
        debugView == kVBDebugMaterialFamily ||
        debugView == kVBDebugReflectionPolicy ||
        debugView == kVBDebugTemporalPolicy ||
        debugView == kVBDebugPostSensitivity ||
        debugView == kVBDebugMaterialMissingChannelMask) {
        auto mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::PayloadInstance;
        if (debugView == kVBDebugMaterialId) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialId;
        } else if (debugView == kVBDebugStableObjectId) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::StableObjectId;
        } else if (debugView == kVBDebugMaterialFamily) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialFamily;
        } else if (debugView == kVBDebugReflectionPolicy) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::ReflectionPolicy;
        } else if (debugView == kVBDebugTemporalPolicy) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::TemporalPolicy;
        } else if (debugView == kVBDebugPostSensitivity) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::PostSensitivity;
        } else if (debugView == kVBDebugMaterialMissingChannelMask) {
            mode = VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialMissingChannelMask;
        }
        auto dbg = ctx.visibilityBuffer->DebugBlitVisibilityToHDR(
            ctx.commandList,
            ctx.mainTargets->hdr.resources.color.Get(),
            ctx.mainTargets->hdr.descriptors.rtv.cpu,
            mode);
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (visibility) failed: {}", dbg.Error());
        }
        m_state.renderedThisFrame = true;
        completedPath = true;
    } else if (debugView == kVBDebugDepth) {
        const bool debugDepthReady = VisibilityBufferResourcePass::PrepareDepthForSampling(
            ctx.commandList,
            {ctx.depthResources->resources.buffer.Get(), &ctx.depthResources->resources.resourceState});
        if (!debugDepthReady) {
            return false;
        }
        auto dbg = ctx.visibilityBuffer->DebugBlitDepthToHDR(
            ctx.commandList, ctx.mainTargets->hdr.resources.color.Get(), ctx.mainTargets->hdr.descriptors.rtv.cpu, ctx.depthResources->resources.buffer.Get());
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (depth) failed: {}", dbg.Error());
        }
        m_state.renderedThisFrame = true;
        completedPath = true;
    }

    return true;
}

bool VisibilityBufferSubsystem::RenderVisibilityBufferMaterialResolveStage(uint32_t debugView,
                                                                          bool& completedPath,
                                                                          const VisibilityBufferContext& ctx) {
    completedPath = false;
    const bool depthSampleReady = VisibilityBufferResourcePass::PrepareDepthForSampling(
        ctx.commandList,
        {ctx.depthResources->resources.buffer.Get(), &ctx.depthResources->resources.resourceState});
    if (!depthSampleReady) {
        return false;
    }

    auto resolveResult = ctx.visibilityBuffer->ResolveMaterials(
        ctx.commandList,
        ctx.depthResources->resources.buffer.Get(),
        ctx.depthResources->descriptors.srv.cpu,
        m_state.meshDraws,
        ctx.constantBuffers->frameCPU.viewProjectionMatrix,
        glm::vec3(ctx.constantBuffers->frameCPU.cameraPosition),
        ctx.constantBuffers->biomeMaterials.gpuAddress);

    if (resolveResult.IsErr()) {
        spdlog::error("Material resolve failed: {}", resolveResult.Error());
        return false;
    }

    static bool firstResolve = true;
    if (firstResolve) {
        spdlog::info("VB: Material resolve completed successfully");
        firstResolve = false;
    }

    if (IsVisibilityBufferGBufferDebugView(debugView)) {
        VisibilityBufferRenderer::DebugBlitBuffer which = VisibilityBufferRenderer::DebugBlitBuffer::Albedo;
        if (debugView == kVBDebugGBufferNormal) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::NormalRoughness;
        } else if (debugView == kVBDebugGBufferEmissive) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::EmissiveMetallic;
        } else if (debugView == kVBDebugGBufferExt0) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt0;
        } else if (debugView == kVBDebugGBufferExt1) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt1;
        } else if (debugView == kVBDebugGBufferExt2) {
            which = VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt2;
        }

        auto dbg = ctx.visibilityBuffer->DebugBlitGBufferToHDR(ctx.commandList, ctx.mainTargets->hdr.resources.color.Get(), ctx.mainTargets->hdr.descriptors.rtv.cpu, which);
        if (dbg.IsErr()) {
            spdlog::warn("VB debug blit (gbuffer) failed: {}", dbg.Error());
        }
        m_state.renderedThisFrame = true;
        completedPath = true;
    }

    return true;
}

VisibilityBufferSubsystem::DeferredLightingInputs
VisibilityBufferSubsystem::PrepareVisibilityBufferDeferredLighting(Scene::ECS_Registry* registry,
                                                                   const VisibilityBufferContext& ctx) {
    DeferredLightingInputs inputs{};
    inputs.rtGIResource = ctx.rtGIResource;
    inputs.ssaoResource = ctx.ssaoResource;
    {
        auto lightView = registry->View<Scene::LightComponent, Scene::TransformComponent>();
        for (auto entity : lightView) {
            auto& lc = lightView.get<Scene::LightComponent>(entity);
            auto& tc = lightView.get<Scene::TransformComponent>(entity);
            if (lc.type == Scene::LightType::Directional) continue;

            Light light{};
            light.position_type = glm::vec4(tc.position, static_cast<float>(lc.type));
            glm::vec3 forward = tc.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const float semanticClassId = static_cast<float>(lc.semanticClassId);
            const bool isSpot = lc.type == Scene::LightType::Spot;
            const bool isAreaRect = lc.type == Scene::LightType::AreaRect;
            light.direction_cosInner = glm::vec4(
                forward,
                isSpot ? std::cos(glm::radians(lc.innerConeDegrees)) : semanticClassId);
            light.color_range = glm::vec4(lc.color * lc.intensity, lc.range);
            float outerCos = std::cos(glm::radians(lc.outerConeDegrees));
            const glm::vec2 areaHalfSize = isAreaRect ? 0.5f * glm::max(lc.areaSize, glm::vec2(0.0f)) : glm::vec2(0.0f);
            light.params = glm::vec4(outerCos,
                                      -1.0f,
                                      isAreaRect ? areaHalfSize.x : semanticClassId,
                                      areaHalfSize.y);
            inputs.localLights.push_back(light);
        }
    }

    auto lightsResult = ctx.visibilityBuffer->UpdateLocalLights(ctx.commandList, inputs.localLights);
    if (lightsResult.IsErr()) {
        spdlog::warn("VB local lights update failed: {}", lightsResult.Error());
    }

    {
        auto probeView = registry->View<Scene::ReflectionProbeComponent, Scene::TransformComponent>();
        for (auto entity : probeView) {
            const auto& probe = probeView.get<Scene::ReflectionProbeComponent>(entity);
            const auto& transform = probeView.get<Scene::TransformComponent>(entity);
            if (probe.enabled == 0u) {
                continue;
            }
            if (probe.environmentIndex >= ctx.environment->maps.size()) {
                ++inputs.skippedReflectionProbes;
                continue;
            }

            EnvironmentMaps& env = ctx.environment->maps[probe.environmentIndex];
            ctx.ensureEnvironmentBindlessSRVs(env);
            if (!env.diffuseIrradianceSRV.IsValid() || !env.specularPrefilteredSRV.IsValid()) {
                ++inputs.skippedReflectionProbes;
                continue;
            }

            VBReflectionProbe vbProbe{};
            vbProbe.centerBlend = glm::vec4(glm::vec3(transform.worldMatrix[3]), std::max(0.0f, probe.blendDistance));
            vbProbe.extents = glm::vec4(glm::max(glm::vec3(0.01f), glm::abs(probe.extents * transform.scale)), 0.0f);
            vbProbe.envIndices = glm::uvec4(env.diffuseIrradianceSRV.index, env.specularPrefilteredSRV.index, 0u, 0u);
            inputs.reflectionProbes.push_back(vbProbe);
        }
    }

    m_state.reflectionProbes = inputs.reflectionProbes;
    m_state.reflectionProbeSkipped = inputs.skippedReflectionProbes;
    m_state.reflectionProbeTableValid =
        !inputs.reflectionProbes.empty() && ctx.visibilityBuffer->HasReflectionProbeTable();

    auto probesResult = ctx.visibilityBuffer->UpdateReflectionProbes(
        ctx.commandList, inputs.reflectionProbes);
    if (probesResult.IsErr()) {
        spdlog::warn("VB reflection probe update failed: {}", probesResult.Error());
        m_state.reflectionProbeTableValid = false;
    }

    auto& deferredParams = inputs.params;
    deferredParams.invViewProj = glm::inverse(ctx.constantBuffers->frameCPU.viewProjectionMatrix);
    deferredParams.viewMatrix = ctx.constantBuffers->frameCPU.viewMatrix;
    for (int i = 0; i < 6; ++i) {
        deferredParams.lightViewProjection[i] = ctx.constantBuffers->frameCPU.lightViewProjection[i];
    }
    deferredParams.cameraPosition = ctx.constantBuffers->frameCPU.cameraPosition;
    deferredParams.sunDirection = glm::vec4(ctx.lightingState->directionalDirection, 0.0f);
    deferredParams.sunRadiance =
        glm::vec4(ctx.lightingState->directionalColor * ctx.lightingState->directionalIntensity, 0.0f);
    deferredParams.ambientColor =
        glm::vec4(ctx.lightingState->ambientColor * ctx.lightingState->ambientIntensity,
                  ctx.environment->backgroundBlur);
    deferredParams.cascadeSplits = ctx.constantBuffers->frameCPU.cascadeSplits;
    deferredParams.shadowParams = glm::vec4(
        ctx.shadows->Resources().controls.bias,
        ctx.shadows->Resources().controls.pcfRadius,
        ctx.shadows->Resources().controls.enabled ? 1.0f : 0.0f,
        ctx.shadows->Resources().controls.pcssEnabled ? 1.0f : 0.0f);
    const float backgroundExposure = (DisableVisibleBackgroundFromEnv() || !ctx.environment->backgroundVisible)
        ? 0.0f
        : ctx.environment->backgroundExposure;
    deferredParams.envParams = glm::vec4(
        ctx.environment->diffuseIntensity,
        ctx.environment->specularIntensity,
        ctx.environment->enabled ? 1.0f : 0.0f,
        backgroundExposure);
    float invShadowDim = 1.0f / static_cast<float>(ctx.shadows->Resources().controls.mapSize);
    deferredParams.shadowInvSizeAndSpecMaxMip =
        glm::vec4(invShadowDim, invShadowDim, 8.0f, glm::radians(ctx.environment->rotationDegrees));
    float nearZ = 0.1f, farZ = 1000.0f;
    deferredParams.projectionParams = glm::vec4(
        ctx.constantBuffers->frameCPU.projectionMatrix[0][0], ctx.constantBuffers->frameCPU.projectionMatrix[1][1], nearZ, farZ);
    uint32_t screenW = ctx.window ? ctx.internalRenderWidth : 1280;
    uint32_t screenH = ctx.window ? ctx.internalRenderHeight : 720;
    deferredParams.screenAndCluster = glm::uvec4(screenW, screenH, 16, 9);
    deferredParams.clusterParams = glm::uvec4(24, 128, static_cast<uint32_t>(inputs.localLights.size()), 0);
    deferredParams.reflectionProbeParams = glm::uvec4(
        m_state.reflectionProbeTableValid ? ctx.visibilityBuffer->GetReflectionProbeTableIndex() : 0u,
        static_cast<uint32_t>(inputs.reflectionProbes.size()),
        ctx.debugViewMode,
        inputs.skippedReflectionProbes);
    deferredParams.localProbeParams = glm::vec4(
        ctx.environment->localProbeDiffuseIntensity,
        ctx.environment->localProbeSpecularIntensity,
        ctx.environment->localProbeRadianceEnabled ? 1.0f : 0.0f,
        0.0f);
    deferredParams.sceneLocalPayloadParams = ctx.buildSceneLocalEnvironmentV3PayloadParams();
    deferredParams.cinematicStabilityParams = ctx.buildCinematicStabilityParams();

    if (ctx.environment->ShouldBindImageBasedLightingTextures()) {
        if (auto* env = ctx.environment->ActiveEnvironment()) {
            if (env->diffuseIrradiance) {
                inputs.envDiffuseResource = env->diffuseIrradiance->GetResource();
                inputs.envFormat = env->diffuseIrradiance->GetFormat();
            }
            if (env->specularPrefiltered) {
                inputs.envSpecularResource = env->specularPrefiltered->GetResource();
            }
        }
        if (!inputs.envDiffuseResource && ctx.materialFallbacks->albedo) {
            inputs.envDiffuseResource = ctx.materialFallbacks->albedo->GetResource();
        }
        if (!inputs.envSpecularResource && ctx.materialFallbacks->albedo) {
            inputs.envSpecularResource = ctx.materialFallbacks->albedo->GetResource();
        }
    }

    return inputs;
}

void VisibilityBufferSubsystem::ApplyVisibilityBufferDeferredLighting(const DeferredLightingInputs& inputs,
                                                                      const VisibilityBufferContext& ctx) {
    auto lightingResult = ctx.visibilityBuffer->ApplyDeferredLighting(
        ctx.commandList,
        ctx.mainTargets->hdr.resources.color.Get(),
        ctx.mainTargets->hdr.descriptors.rtv.cpu,
        ctx.depthResources->resources.buffer.Get(),
        ctx.depthResources->descriptors.srv,
        inputs.envDiffuseResource,
        inputs.envSpecularResource,
        inputs.envFormat,
        ctx.shadows->Resources().resources.srv,
        inputs.rtGIResource,
        inputs.ssaoResource,
        inputs.params);
    if (lightingResult.IsErr()) {
        spdlog::warn("VB deferred lighting failed: {}", lightingResult.Error());
    }

    m_state.renderedThisFrame = true;
}

void VisibilityBufferSubsystem::RenderVisibilityBufferDeferredLightingStage(Scene::ECS_Registry* registry,
                                                                            const VisibilityBufferContext& ctx) {
    const auto inputs = PrepareVisibilityBufferDeferredLighting(registry, ctx);
    ApplyVisibilityBufferDeferredLighting(inputs, ctx);
}

void VisibilityBufferSubsystem::RenderVisibilityBufferPath(Scene::ECS_Registry* registry,
                                                           const VisibilityBufferContext& ctx) {
    if (!ctx.visibilityBuffer || !m_state.enabled) {
        spdlog::warn("VB: Disabled or not initialized");
        return;
    }

    // Collect and upload instance data + mesh draw info
    CollectInstancesForVisibilityBuffer(registry, ctx);

    if (m_state.instances.empty() || m_state.meshDraws.empty()) {
        spdlog::warn("VB: No instances collected (instances={}, meshDraws={})",
                     m_state.instances.size(), m_state.meshDraws.size());
        return;
    }

    const uint32_t vbDebugView = GetVisibilityBufferDebugView(ctx.debugViewMode);
    if (IsVisibilityBufferDebugView(vbDebugView)) {
        m_state.debugOverrideThisFrame = true;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS vbCullMaskAddress = ResolveVisibilityBufferCullMask(vbDebugView, ctx);
    LogVisibilityBufferFirstFrame();

    bool completedPath = false;
    if (!RenderVisibilityBufferVisibilityStage(vbCullMaskAddress, vbDebugView, completedPath, ctx) || completedPath) {
        return;
    }
    if (!RenderVisibilityBufferMaterialResolveStage(vbDebugView, completedPath, ctx) || completedPath) {
        return;
    }
    RenderVisibilityBufferDeferredLightingStage(registry, ctx);
}

void VisibilityBufferSubsystem::LogVisibilityBufferFirstFrame() const {
    static bool firstFrame = true;
    if (!firstFrame) {
        return;
    }

    spdlog::info("VB: First frame - rendering {} instances across {} unique meshes",
                 m_state.instances.size(), m_state.meshDraws.size());
    std::unordered_map<uint32_t, uint32_t> meshIndexCounts;
    for (const auto& inst : m_state.instances) {
        meshIndexCounts[inst.meshIndex]++;
    }
    for (const auto& [meshIdx, count] : meshIndexCounts) {
        spdlog::info("  Mesh {} has {} instances", meshIdx, count);
    }
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(m_state.meshDraws.size()); ++meshIdx) {
        const auto& draw = m_state.meshDraws[meshIdx];
        const uint64_t vbBytes = draw.vertexBuffer ? draw.vertexBuffer->GetDesc().Width : 0ull;
        const uint64_t ibBytes = draw.indexBuffer ? draw.indexBuffer->GetDesc().Width : 0ull;
        spdlog::info("  MeshDraw {}: vtxCount={} idxCount={} stride={} vbBytes={} ibBytes={} opaque={} ds={} alpha={} alphaDs={} start={}/{}/{}/{}",
                     meshIdx,
                     draw.vertexCount,
                     draw.indexCount,
                     draw.vertexStrideBytes,
                     vbBytes,
                     ibBytes,
                     draw.instanceCount,
                     draw.instanceCountDoubleSided,
                     draw.instanceCountAlpha,
                     draw.instanceCountAlphaDoubleSided,
                     draw.startInstance,
                     draw.startInstanceDoubleSided,
                     draw.startInstanceAlpha,
                     draw.startInstanceAlphaDoubleSided);
    }
    firstFrame = false;
}

} // namespace Cortex::Graphics
