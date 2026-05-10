#pragma once

#include <type_traits>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "Graphics/GPUCulling.h"

namespace Cortex::Graphics {

struct GpuCullingEntityHash {
    size_t operator()(entt::entity e) const noexcept {
        using Underlying = std::underlying_type_t<entt::entity>;
        return std::hash<Underlying>{}(static_cast<Underlying>(e));
    }
};

struct GpuCullingRuntimeState {
    bool enabled = false;
    bool indirectDrawEnabled = false;
    bool freeze = false;
    bool freezeCaptured = false;
    glm::mat4 frozenViewProj{1.0f};
    glm::vec3 frozenCameraPos{0.0f};

    // Cached instance data rebuilt from the scene for GPU culling.
    std::vector<GPUInstanceData> instances;
    // Mesh info for indirect draws, indexed by GPUInstanceData::meshIndex.
    std::vector<MeshInfo> meshInfos;

    bool hzbOcclusionUsedThisFrame = false;

    // Stable IDs for occlusion history indexing (maps entity -> cullingId).
    std::unordered_map<entt::entity, uint32_t, GpuCullingEntityHash> idByEntity;
    std::vector<uint32_t> idFreeList;
    // Per-slot generation counters prevent history smear when cullingId slots
    // are recycled (packed into cullingId as gen<<16 | slot).
    std::vector<uint16_t> idGeneration;
    uint32_t nextId = 0;

    // Previous frame world-space centers for motion-inflated occlusion culling.
    std::unordered_map<entt::entity, glm::vec3, GpuCullingEntityHash> previousCenterByEntity;
    // Previous frame world matrices for visibility-buffer per-object motion
    // vectors. This renderer-owned history is invalidated on scene changes and
    // entity lifetime changes.
    std::unordered_map<entt::entity, glm::mat4, GpuCullingEntityHash> previousWorldByEntity;
    bool previousTransformHistoryResetPending = false;
};

} // namespace Cortex::Graphics
