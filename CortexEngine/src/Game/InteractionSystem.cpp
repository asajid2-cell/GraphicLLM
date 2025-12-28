#include "InteractionSystem.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Game {

void InteractionSystem::Initialize(Scene::ECS_Registry* registry) {
    m_registry = registry;
    m_heldEntity = entt::null;
    m_hoveredEntity = entt::null;
}

void InteractionSystem::SetTerrainParams(const Scene::TerrainNoiseParams& params, bool enabled) {
    m_terrainParams = params;
    m_terrainEnabled = enabled;
}

void InteractionSystem::Update(const glm::vec3& cameraPos,
                                const glm::vec3& cameraForward,
                                float deltaTime) {
    if (!m_registry) return;

    UpdatePhysics(deltaTime);

    if (m_heldEntity != entt::null) {
        UpdateHeldObject(cameraPos, cameraForward);
    }

    entt::entity newHovered = RaycastInteractable(cameraPos, cameraForward, m_interactionRange);

    if (newHovered != m_hoveredEntity) {
        if (m_hoveredEntity != entt::null &&
            m_registry->HasComponent<Scene::InteractableComponent>(m_hoveredEntity)) {
            auto& interactable = m_registry->GetComponent<Scene::InteractableComponent>(m_hoveredEntity);
            interactable.isHighlighted = false;
        }

        if (newHovered != entt::null &&
            m_registry->HasComponent<Scene::InteractableComponent>(newHovered)) {
            auto& interactable = m_registry->GetComponent<Scene::InteractableComponent>(newHovered);
            interactable.isHighlighted = true;
        }

        m_hoveredEntity = newHovered;
    }
}

void InteractionSystem::OnInteractPressed() {
    if (!m_registry) return;

    if (m_heldEntity != entt::null) return;

    if (m_hoveredEntity != entt::null &&
        m_registry->HasComponent<Scene::InteractableComponent>(m_hoveredEntity)) {
        auto& interactable = m_registry->GetComponent<Scene::InteractableComponent>(m_hoveredEntity);

        switch (interactable.type) {
        case Scene::InteractionType::Pickup:
            PickupObject(m_hoveredEntity);
            break;
        case Scene::InteractionType::Activate:
            spdlog::info("Activated object");
            break;
        case Scene::InteractionType::Examine:
            spdlog::info("Examining object");
            break;
        }
    }
}

void InteractionSystem::OnDropPressed() {
    if (!m_registry || m_heldEntity == entt::null) return;

    auto cameras = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    glm::vec3 cameraPos(0.0f);
    for (auto entity : cameras) {
        auto& transform = cameras.get<Scene::TransformComponent>(entity);
        cameraPos = transform.position;
        break;
    }

    DropObject(cameraPos, glm::vec3(0.0f));
}

void InteractionSystem::OnThrowPressed() {
    if (!m_registry || m_heldEntity == entt::null) return;

    auto cameras = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    glm::vec3 cameraPos(0.0f);
    glm::vec3 cameraForward(0.0f, 0.0f, 1.0f);
    for (auto entity : cameras) {
        auto& transform = cameras.get<Scene::TransformComponent>(entity);
        cameraPos = transform.position;
        cameraForward = transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    }

    DropObject(cameraPos, cameraForward * m_throwForce);
}

entt::entity InteractionSystem::RaycastInteractable(const glm::vec3& origin,
                                                     const glm::vec3& direction,
                                                     float maxDistance) {
    if (!m_registry) return entt::null;

    entt::entity closestEntity = entt::null;
    float closestT = maxDistance;

    auto view = m_registry->View<Scene::InteractableComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        if (entity == m_heldEntity) continue;

        auto& interactable = view.get<Scene::InteractableComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        float radius = interactable.interactionRadius;
        if (radius <= 0.0f) radius = 0.5f;

        float t;
        if (RaySphereIntersect(origin, direction, transform.position, radius, t)) {
            if (t > 0.0f && t < closestT) {
                closestT = t;
                closestEntity = entity;
            }
        }
    }

    return closestEntity;
}

bool InteractionSystem::RaySphereIntersect(const glm::vec3& rayOrigin,
                                            const glm::vec3& rayDir,
                                            const glm::vec3& sphereCenter,
                                            float sphereRadius,
                                            float& outT) const {
    glm::vec3 oc = rayOrigin - sphereCenter;
    float a = glm::dot(rayDir, rayDir);
    float b = 2.0f * glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return false;

    float sqrtD = std::sqrt(discriminant);
    float t1 = (-b - sqrtD) / (2.0f * a);
    float t2 = (-b + sqrtD) / (2.0f * a);

    if (t1 > 0.0f) { outT = t1; return true; }
    if (t2 > 0.0f) { outT = t2; return true; }

    return false;
}

void InteractionSystem::PickupObject(entt::entity entity) {
    if (!m_registry) return;

    if (!m_registry->HasComponent<Scene::HeldObjectComponent>(entity)) {
        m_registry->AddComponent<Scene::HeldObjectComponent>(entity);
    }

    auto& held = m_registry->GetComponent<Scene::HeldObjectComponent>(entity);
    held.holdOffset = glm::vec3(0.0f, -0.2f, m_holdDistance);

    if (m_registry->HasComponent<Scene::PhysicsBodyComponent>(entity)) {
        auto& physics = m_registry->GetComponent<Scene::PhysicsBodyComponent>(entity);
        physics.isKinematic = true;
        physics.velocity = glm::vec3(0.0f);
        physics.angularVelocity = glm::vec3(0.0f);
    }

    if (m_registry->HasComponent<Scene::InteractableComponent>(entity)) {
        auto& interactable = m_registry->GetComponent<Scene::InteractableComponent>(entity);
        interactable.isHighlighted = false;
    }

    m_heldEntity = entity;
    m_hoveredEntity = entt::null;

    spdlog::info("Picked up object");
}

void InteractionSystem::DropObject(const glm::vec3& cameraPos, const glm::vec3& velocity) {
    if (!m_registry || m_heldEntity == entt::null) return;

    if (m_registry->HasComponent<Scene::HeldObjectComponent>(m_heldEntity)) {
        m_registry->RemoveComponent<Scene::HeldObjectComponent>(m_heldEntity);
    }

    if (m_registry->HasComponent<Scene::PhysicsBodyComponent>(m_heldEntity)) {
        auto& physics = m_registry->GetComponent<Scene::PhysicsBodyComponent>(m_heldEntity);
        physics.isKinematic = false;
        physics.velocity = velocity;
    }

    spdlog::info("Dropped object");
    m_heldEntity = entt::null;
}

void InteractionSystem::UpdatePhysics(float deltaTime) {
    if (!m_registry) return;

    const float gravity = 20.0f;

    auto view = m_registry->View<Scene::PhysicsBodyComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        auto& physics = view.get<Scene::PhysicsBodyComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        if (physics.isKinematic) continue;

        if (physics.useGravity) {
            physics.velocity.y -= gravity * deltaTime;
        }

        glm::vec3 newPos = transform.position + physics.velocity * deltaTime;

        if (m_terrainEnabled) {
            float groundY = Scene::SampleTerrainHeight(
                static_cast<double>(newPos.x),
                static_cast<double>(newPos.z),
                m_terrainParams);

            float objectRadius = 0.5f;
            if (newPos.y - objectRadius < groundY) {
                newPos.y = groundY + objectRadius;

                if (physics.velocity.y < 0.0f) {
                    physics.velocity.y = -physics.velocity.y * physics.restitution;
                    physics.velocity.x *= (1.0f - physics.friction);
                    physics.velocity.z *= (1.0f - physics.friction);

                    if (std::abs(physics.velocity.y) < 0.5f) {
                        physics.velocity.y = 0.0f;
                    }
                }
            }
        }

        if (glm::length(physics.angularVelocity) > 0.001f) {
            float angle = glm::length(physics.angularVelocity) * deltaTime;
            glm::vec3 axis = glm::normalize(physics.angularVelocity);
            transform.rotation = glm::angleAxis(angle, axis) * transform.rotation;
            physics.angularVelocity *= 0.98f;
        }

        transform.position = newPos;
    }
}

void InteractionSystem::UpdateHeldObject(const glm::vec3& cameraPos,
                                          const glm::vec3& cameraForward) {
    if (!m_registry || m_heldEntity == entt::null) return;

    if (!m_registry->HasComponent<Scene::TransformComponent>(m_heldEntity)) return;

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_heldEntity);

    glm::vec3 holdOffset(0.0f, -0.2f, m_holdDistance);
    if (m_registry->HasComponent<Scene::HeldObjectComponent>(m_heldEntity)) {
        holdOffset = m_registry->GetComponent<Scene::HeldObjectComponent>(m_heldEntity).holdOffset;
    }

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(cameraForward, up));
    glm::vec3 camUp = glm::cross(right, cameraForward);

    transform.position = cameraPos +
                         cameraForward * holdOffset.z +
                         right * holdOffset.x +
                         camUp * holdOffset.y;
}

} // namespace Cortex::Game
