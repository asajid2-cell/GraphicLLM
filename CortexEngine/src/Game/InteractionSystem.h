#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"

namespace Cortex::Game {

// Manages player interaction with objects in the world.
class InteractionSystem {
public:
    InteractionSystem() = default;
    ~InteractionSystem() = default;

    void Initialize(Scene::ECS_Registry* registry);

    // Update interaction state each frame.
    void Update(const glm::vec3& cameraPos,
                const glm::vec3& cameraForward,
                float deltaTime);

    // Input handlers.
    void OnInteractPressed();
    void OnDropPressed();
    void OnThrowPressed();

    // State queries.
    [[nodiscard]] bool IsHoldingObject() const { return m_heldEntity != entt::null; }
    [[nodiscard]] entt::entity GetHeldEntity() const { return m_heldEntity; }
    [[nodiscard]] entt::entity GetHoveredEntity() const { return m_hoveredEntity; }

    // Terrain collision for physics.
    void SetTerrainParams(const Scene::TerrainNoiseParams& params, bool enabled);

private:
    entt::entity RaycastInteractable(const glm::vec3& origin,
                                      const glm::vec3& direction,
                                      float maxDistance);

    bool RaySphereIntersect(const glm::vec3& rayOrigin,
                            const glm::vec3& rayDir,
                            const glm::vec3& sphereCenter,
                            float sphereRadius,
                            float& outT) const;

    void PickupObject(entt::entity entity);
    void DropObject(const glm::vec3& cameraPos, const glm::vec3& velocity);
    void UpdatePhysics(float deltaTime);
    void UpdateHeldObject(const glm::vec3& cameraPos, const glm::vec3& cameraForward);

    Scene::ECS_Registry* m_registry = nullptr;
    entt::entity m_heldEntity = entt::null;
    entt::entity m_hoveredEntity = entt::null;

    float m_interactionRange = 3.0f;
    float m_holdDistance = 1.5f;
    float m_throwForce = 15.0f;

    bool m_terrainEnabled = false;
    Scene::TerrainNoiseParams m_terrainParams;
};

} // namespace Cortex::Game
