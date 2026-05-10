#pragma once

#include "Graphics/FrameContract.h"
#include "Scene/Components.h"

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Cortex::Scene {
class ECS_Registry;
}

namespace Cortex::Graphics {

struct RendererSceneRenderable {
    entt::entity entity = entt::null;
    Scene::RenderableComponent* renderable = nullptr;
    Scene::TransformComponent* transform = nullptr;
    RenderableDepthClass depthClass = RenderableDepthClass::Invalid;
    glm::mat4 worldMatrix{1.0f};
    glm::mat4 normalMatrix{1.0f};
    bool visible = false;
    bool hasMesh = false;
    bool hasTransform = false;
    bool hasGpuBuffers = false;
    bool hasRawMeshSrvs = false;
};

struct RendererSceneSnapshot {
    uint64_t frameNumber = 0;
    uint32_t renderableEntityCount = 0;
    FrameContract::RenderableClasses renderables{};
    FrameContract::MaterialStats materials{};
    std::vector<RendererSceneRenderable> entries;
    std::vector<uint32_t> depthWritingIndices;
    std::vector<uint32_t> overlayIndices;
    std::vector<uint32_t> waterIndices;
    std::vector<uint32_t> transparentIndices;
    std::vector<uint32_t> rtCandidateIndices;

    void Clear();
    [[nodiscard]] bool IsValidForFrame(uint64_t frame) const;
};

[[nodiscard]] RendererSceneSnapshot BuildRendererSceneSnapshot(Scene::ECS_Registry* registry,
                                                               uint64_t frameNumber);

} // namespace Cortex::Graphics
