#pragma once

#include <entt/entt.hpp>
#include <memory>
#include "Components.h"

namespace Cortex::Scene {

// Wrapper around EnTT registry
// This will be the thread-safe scene graph that syncs with the three async loops
class ECS_Registry {
public:
    ECS_Registry() = default;
    ~ECS_Registry() = default;

    // Entity creation
    entt::entity CreateEntity();
    void DestroyEntity(entt::entity entity);

    // Component access
    template<typename Component, typename... Args>
    Component& AddComponent(entt::entity entity, Args&&... args) {
        return m_registry.emplace<Component>(entity, std::forward<Args>(args)...);
    }

    template<typename Component>
    Component& GetComponent(entt::entity entity) {
        return m_registry.get<Component>(entity);
    }

    template<typename Component>
    bool HasComponent(entt::entity entity) const {
        return m_registry.all_of<Component>(entity);
    }

    template<typename Component>
    void RemoveComponent(entt::entity entity) {
        m_registry.remove<Component>(entity);
    }

    // View access (for systems)
    template<typename... Components>
    auto View() {
        return m_registry.view<Components...>();
    }

    // Get raw registry (for advanced use)
    entt::registry& GetRegistry() { return m_registry; }
    const entt::registry& GetRegistry() const { return m_registry; }

    // Helper: Create a simple cube entity
    entt::entity CreateCube(const glm::vec3& position, const std::string& tag = "Cube");

    // Phase 4: Scene Description for AI context
    std::string DescribeScene() const;

private:
    entt::registry m_registry;
};

} // namespace Cortex::Scene
