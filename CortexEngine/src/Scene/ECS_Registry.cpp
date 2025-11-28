#include "ECS_Registry.h"
#include <spdlog/spdlog.h>

namespace Cortex::Scene {

entt::entity ECS_Registry::CreateEntity() {
    entt::entity entity = m_registry.create();
    spdlog::debug("Entity created: {}", static_cast<uint32_t>(entity));
    return entity;
}

void ECS_Registry::DestroyEntity(entt::entity entity) {
    m_registry.destroy(entity);
    spdlog::debug("Entity destroyed: {}", static_cast<uint32_t>(entity));
}

entt::entity ECS_Registry::CreateCube(const glm::vec3& position, const std::string& tag) {
    entt::entity entity = CreateEntity();

    // Add transform
    auto& transform = AddComponent<TransformComponent>(entity);
    transform.position = position;

    // Add tag
    AddComponent<TagComponent>(entity, tag);

    // Mesh and Renderable will be added by the renderer after GPU resources are created
    AddComponent<RenderableComponent>(entity);

    // Add rotation for spinning cube demo
    AddComponent<RotationComponent>(entity);

    spdlog::info("Cube entity created at ({}, {}, {}) with tag '{}'",
                 position.x, position.y, position.z, tag);

    return entity;
}

std::string ECS_Registry::DescribeScene() const {
    // Phase 4: This will generate a text description for the LLM context
    // For now, just count entities
    std::string description = "Scene contains:\n";

    auto view = m_registry.view<TagComponent, TransformComponent>();
    for (auto entity : view) {
        const auto& tag = view.get<TagComponent>(entity);
        const auto& transform = view.get<TransformComponent>(entity);

        description += "  - " + tag.tag + " at (" +
                       std::to_string(transform.position.x) + ", " +
                       std::to_string(transform.position.y) + ", " +
                       std::to_string(transform.position.z) + ")\n";
    }

    return description;
}

void ECS_Registry::UpdateTransforms() {
    auto view = m_registry.view<TransformComponent>();

    // First pass: ensure roots (entities with no valid parent) are updated.
    for (auto entity : view) {
        auto& transform = view.get<TransformComponent>(entity);
        const bool hasParent =
            transform.parent != entt::null &&
            m_registry.valid(transform.parent) &&
            m_registry.all_of<TransformComponent>(transform.parent);

        if (!hasParent) {
            UpdateTransformRecursive(entity, glm::mat4(1.0f));
        }
    }
}

void ECS_Registry::UpdateTransformRecursive(entt::entity entity, const glm::mat4& parentWorld) {
    if (!m_registry.all_of<TransformComponent>(entity)) {
        return;
    }

    auto& transform = m_registry.get<TransformComponent>(entity);
    glm::mat4 local = transform.GetLocalMatrix();
    transform.worldMatrix = parentWorld * local;
    glm::mat4 invWorld = glm::inverse(transform.worldMatrix);
    transform.normalMatrix = glm::transpose(invWorld);
    transform.inverseWorldMatrix = invWorld;

    // Propagate to children. For now, perform a simple scan over all transforms;
    // if performance becomes an issue, this can be replaced with an explicit
    // parent->children adjacency structure.
    auto view = m_registry.view<TransformComponent>();
    for (auto child : view) {
        auto& childTransform = view.get<TransformComponent>(child);
        if (childTransform.parent == entity) {
            UpdateTransformRecursive(child, transform.worldMatrix);
        }
    }
}

} // namespace Cortex::Scene
