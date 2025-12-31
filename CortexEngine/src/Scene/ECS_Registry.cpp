#include "ECS_Registry.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Scene {

entt::entity ECS_Registry::CreateEntity() {
    entt::entity entity = m_registry.create();
    spdlog::debug("Entity created: {}", static_cast<uint32_t>(entity));
    return entity;
}

void ECS_Registry::DestroyEntity(entt::entity entity) {
    // First, clean up parent-child relationships
    if (m_registry.all_of<TransformComponent>(entity)) {
        // Remove this entity from its parent's children list
        RemoveParent(entity);

        // Remove all children entries for this entity
        m_childrenOf.erase(entity);
    }

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

    // Propagate to children using O(1) lookup instead of O(N) scan
    auto it = m_childrenOf.find(entity);
    if (it != m_childrenOf.end()) {
        for (auto child : it->second) {
            UpdateTransformRecursive(child, transform.worldMatrix);
        }
    }
}

void ECS_Registry::SetParent(entt::entity child, entt::entity parent) {
    if (!m_registry.all_of<TransformComponent>(child)) {
        return;
    }

    auto& childTransform = m_registry.get<TransformComponent>(child);

    // Remove from old parent's children list
    if (childTransform.parent != entt::null) {
        auto it = m_childrenOf.find(childTransform.parent);
        if (it != m_childrenOf.end()) {
            auto& children = it->second;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
            // Clean up empty entries
            if (children.empty()) {
                m_childrenOf.erase(it);
            }
        }
    }

    // Set new parent in TransformComponent
    childTransform.parent = parent;

    // Add to new parent's children list
    if (parent != entt::null) {
        m_childrenOf[parent].push_back(child);
    }

    spdlog::debug("Entity {} parent set to {}",
                  static_cast<uint32_t>(child),
                  parent == entt::null ? 0xFFFFFFFF : static_cast<uint32_t>(parent));
}

void ECS_Registry::RemoveParent(entt::entity child) {
    if (!m_registry.all_of<TransformComponent>(child)) {
        return;
    }

    auto& childTransform = m_registry.get<TransformComponent>(child);

    // Remove from old parent's children list
    if (childTransform.parent != entt::null) {
        auto it = m_childrenOf.find(childTransform.parent);
        if (it != m_childrenOf.end()) {
            auto& children = it->second;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
            // Clean up empty entries
            if (children.empty()) {
                m_childrenOf.erase(it);
            }
        }
    }

    // Clear parent in TransformComponent
    childTransform.parent = entt::null;
}

std::vector<entt::entity> ECS_Registry::GetChildren(entt::entity parent) const {
    auto it = m_childrenOf.find(parent);
    if (it != m_childrenOf.end()) {
        return it->second;
    }
    return {};
}

entt::entity ECS_Registry::GetParent(entt::entity child) const {
    if (!m_registry.all_of<TransformComponent>(child)) {
        return entt::null;
    }
    return m_registry.get<TransformComponent>(child).parent;
}

bool ECS_Registry::HasChildren(entt::entity entity) const {
    auto it = m_childrenOf.find(entity);
    return it != m_childrenOf.end() && !it->second.empty();
}

} // namespace Cortex::Scene
