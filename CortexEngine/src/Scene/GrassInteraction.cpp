// GrassInteraction.cpp
// CPU-side grass interaction management implementation.

#include "GrassInteraction.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Scene {

GrassInteractionManager::GrassInteractionManager() {
    // Initialize all slots as inactive
    for (auto& interactor : m_interactors) {
        interactor.active = false;
        interactor.type = GrassInteractorType::None;
    }

    for (auto& gen : m_generations) {
        gen = 0;
    }

    // Initialize constant buffer
    m_cbData = {};
    m_cbData.globalBendStrength = 1.0f;
    m_cbData.windBendScale = 0.5f;
    m_cbData.recoverySpeed = 2.0f;
    m_cbData.grassHeight = 0.5f;
    m_cbData.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    m_cbData.windStrength = 0.3f;
}

void GrassInteractionManager::Initialize() {
    // Reset state
    m_activeCount = 0;
    m_time = 0.0f;

    for (auto& interactor : m_interactors) {
        interactor.active = false;
    }
}

void GrassInteractionManager::Update(float deltaTime) {
    m_time += deltaTime;

    // Update temporary effects (explosions, impacts)
    CleanupExpired(deltaTime);

    // Update explosion wave progress
    for (auto& interactor : m_interactors) {
        if (!interactor.active) continue;

        if (interactor.type == GrassInteractorType::Explosion) {
            // Advance wave front
            interactor.recovery += deltaTime * 2.0f;  // Wave speed

            // Expire when wave passes full radius
            if (interactor.recovery > 1.5f) {
                interactor.active = false;
            }
        }
    }

    // Sort by priority for GPU slot allocation
    SortByPriority();

    // Pack data for GPU
    PackConstantBuffer();
}

InteractorHandle GrassInteractionManager::RegisterCharacter(
    uint32_t entityId,
    const glm::vec3& position,
    float radius
) {
    int32_t slot = FindFreeSlot();
    if (slot < 0) {
        return InteractorHandle{};  // No free slots
    }

    GrassInteractor& interactor = m_interactors[slot];
    interactor.position = position;
    interactor.radius = radius;
    interactor.velocity = glm::vec3(0.0f);
    interactor.strength = 1.0f;
    interactor.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    interactor.type = GrassInteractorType::Character;
    interactor.height = 2.0f;
    interactor.falloff = 2.0f;
    interactor.recovery = 0.0f;
    interactor.lifetime = -1.0f;  // Permanent until removed
    interactor.entityId = entityId;
    interactor.active = true;

    m_generations[slot]++;
    m_activeCount++;

    return InteractorHandle{ static_cast<uint32_t>(slot), m_generations[slot] };
}

InteractorHandle GrassInteractionManager::RegisterVehicle(
    uint32_t entityId,
    const glm::vec3& position,
    const glm::vec3& forward,
    float radius
) {
    int32_t slot = FindFreeSlot();
    if (slot < 0) {
        return InteractorHandle{};
    }

    GrassInteractor& interactor = m_interactors[slot];
    interactor.position = position;
    interactor.radius = radius;
    interactor.velocity = glm::vec3(0.0f);
    interactor.strength = 1.5f;  // Vehicles bend more
    interactor.forward = glm::normalize(forward);
    interactor.type = GrassInteractorType::Vehicle;
    interactor.height = 1.0f;
    interactor.falloff = 1.5f;
    interactor.recovery = 0.0f;
    interactor.lifetime = -1.0f;
    interactor.entityId = entityId;
    interactor.active = true;

    m_generations[slot]++;
    m_activeCount++;

    return InteractorHandle{ static_cast<uint32_t>(slot), m_generations[slot] };
}

InteractorHandle GrassInteractionManager::RegisterProjectile(
    const glm::vec3& position,
    const glm::vec3& velocity,
    float radius
) {
    int32_t slot = FindFreeSlot();
    if (slot < 0) {
        return InteractorHandle{};
    }

    GrassInteractor& interactor = m_interactors[slot];
    interactor.position = position;
    interactor.radius = radius;
    interactor.velocity = velocity;
    interactor.strength = 0.5f;
    interactor.forward = glm::length(velocity) > 0.001f ? glm::normalize(velocity) : glm::vec3(0, 0, 1);
    interactor.type = GrassInteractorType::Projectile;
    interactor.height = 0.5f;
    interactor.falloff = 3.0f;  // Sharp falloff
    interactor.recovery = 0.0f;
    interactor.lifetime = -1.0f;  // Manual removal when projectile destroyed
    interactor.entityId = 0;
    interactor.active = true;

    m_generations[slot]++;
    m_activeCount++;

    return InteractorHandle{ static_cast<uint32_t>(slot), m_generations[slot] };
}

void GrassInteractionManager::CreateExplosion(
    const glm::vec3& position,
    float radius,
    float strength,
    float duration
) {
    int32_t slot = FindFreeSlot();
    if (slot < 0) {
        return;
    }

    GrassInteractor& interactor = m_interactors[slot];
    interactor.position = position;
    interactor.radius = radius;
    interactor.velocity = glm::vec3(0.0f);
    interactor.strength = strength;
    interactor.forward = glm::vec3(0.0f, 1.0f, 0.0f);
    interactor.type = GrassInteractorType::Explosion;
    interactor.height = radius * 0.5f;
    interactor.falloff = 1.0f;
    interactor.recovery = 0.0f;  // Wave starts at center
    interactor.lifetime = duration;
    interactor.entityId = 0;
    interactor.active = true;

    m_generations[slot]++;
    m_activeCount++;
}

void GrassInteractionManager::CreateImpact(
    const glm::vec3& position,
    float radius,
    float strength,
    float duration
) {
    int32_t slot = FindFreeSlot();
    if (slot < 0) {
        return;
    }

    GrassInteractor& interactor = m_interactors[slot];
    interactor.position = position;
    interactor.radius = radius;
    interactor.velocity = glm::vec3(0.0f);
    interactor.strength = strength;
    interactor.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    interactor.type = GrassInteractorType::Character;  // Same behavior as character
    interactor.height = 0.5f;
    interactor.falloff = 2.5f;
    interactor.recovery = 0.0f;
    interactor.lifetime = duration;
    interactor.entityId = 0;
    interactor.active = true;

    m_generations[slot]++;
    m_activeCount++;
}

void GrassInteractionManager::UpdateInteractor(InteractorHandle handle, const glm::vec3& position) {
    if (!handle.IsValid() || handle.index >= m_interactors.size()) {
        return;
    }

    GrassInteractor& interactor = m_interactors[handle.index];
    if (!interactor.active || m_generations[handle.index] != handle.generation) {
        return;
    }

    // Calculate velocity from position delta (approximate, uses last frame)
    glm::vec3 delta = position - interactor.position;
    interactor.velocity = delta * 60.0f;  // Assume ~60 FPS for velocity estimation
    interactor.position = position;
}

void GrassInteractionManager::UpdateInteractor(
    InteractorHandle handle,
    const glm::vec3& position,
    const glm::vec3& forward
) {
    if (!handle.IsValid() || handle.index >= m_interactors.size()) {
        return;
    }

    GrassInteractor& interactor = m_interactors[handle.index];
    if (!interactor.active || m_generations[handle.index] != handle.generation) {
        return;
    }

    glm::vec3 delta = position - interactor.position;
    interactor.velocity = delta * 60.0f;
    interactor.position = position;
    interactor.forward = glm::normalize(forward);
}

void GrassInteractionManager::RemoveInteractor(InteractorHandle handle) {
    if (!handle.IsValid() || handle.index >= m_interactors.size()) {
        return;
    }

    GrassInteractor& interactor = m_interactors[handle.index];
    if (interactor.active && m_generations[handle.index] == handle.generation) {
        interactor.active = false;
        interactor.type = GrassInteractorType::None;
        m_activeCount = std::max(0u, m_activeCount - 1);
    }
}

void GrassInteractionManager::RemoveByEntity(uint32_t entityId) {
    for (auto& interactor : m_interactors) {
        if (interactor.active && interactor.entityId == entityId) {
            interactor.active = false;
            interactor.type = GrassInteractorType::None;
            m_activeCount = std::max(0u, m_activeCount - 1);
        }
    }
}

void GrassInteractionManager::SetWindParameters(const glm::vec3& direction, float strength) {
    m_windDirection = glm::length(direction) > 0.001f ? glm::normalize(direction) : glm::vec3(1, 0, 0);
    m_windStrength = strength;
}

const GrassInteractor* GrassInteractionManager::GetInteractor(InteractorHandle handle) const {
    if (!handle.IsValid() || handle.index >= m_interactors.size()) {
        return nullptr;
    }

    const GrassInteractor& interactor = m_interactors[handle.index];
    if (!interactor.active || m_generations[handle.index] != handle.generation) {
        return nullptr;
    }

    return &interactor;
}

int32_t GrassInteractionManager::FindFreeSlot() const {
    for (size_t i = 0; i < m_interactors.size(); ++i) {
        if (!m_interactors[i].active) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void GrassInteractionManager::PackConstantBuffer() {
    // Reset constant buffer
    m_cbData.activeInteractors = 0;
    m_cbData.globalBendStrength = m_globalStrength;
    m_cbData.windBendScale = m_windBendScale;
    m_cbData.recoverySpeed = m_recoverySpeed;
    m_cbData.windDirection = m_windDirection;
    m_cbData.windStrength = m_windStrength;
    m_cbData.time = m_time;
    m_cbData.grassHeight = m_averageGrassHeight;

    // Pack active interactors into first N slots
    uint32_t packedCount = 0;

    for (const auto& interactor : m_interactors) {
        if (!interactor.active || packedCount >= MAX_GRASS_INTERACTORS) {
            continue;
        }

        GrassInteractorGPU& gpu = m_cbData.interactors[packedCount];
        gpu.position = interactor.position;
        gpu.radius = interactor.radius;
        gpu.velocity = interactor.velocity;
        gpu.strength = interactor.strength;
        gpu.forward = interactor.forward;
        gpu.type = static_cast<uint32_t>(interactor.type);
        gpu.height = interactor.height;
        gpu.falloff = interactor.falloff;
        gpu.recovery = interactor.recovery;
        gpu.padding = 0.0f;

        packedCount++;
    }

    m_cbData.activeInteractors = packedCount;

    // Clear remaining slots
    for (uint32_t i = packedCount; i < MAX_GRASS_INTERACTORS; ++i) {
        m_cbData.interactors[i] = {};
    }
}

void GrassInteractionManager::SortByPriority() {
    // Build list of active indices with priorities
    struct PriorityEntry {
        size_t index;
        float priority;
    };

    std::vector<PriorityEntry> entries;
    entries.reserve(m_activeCount);

    for (size_t i = 0; i < m_interactors.size(); ++i) {
        if (!m_interactors[i].active) continue;

        const GrassInteractor& interactor = m_interactors[i];

        // Priority based on distance to camera and strength
        float distance = glm::length(interactor.position - m_cameraPosition);
        float distancePriority = 1.0f / (1.0f + distance * 0.01f);

        // Explosions and vehicles get priority
        float typePriority = 1.0f;
        if (interactor.type == GrassInteractorType::Explosion) {
            typePriority = 2.0f;
        } else if (interactor.type == GrassInteractorType::Vehicle) {
            typePriority = 1.5f;
        }

        float priority = distancePriority * interactor.strength * typePriority;
        entries.push_back({ i, priority });
    }

    // Sort by priority (highest first)
    std::sort(entries.begin(), entries.end(),
        [](const PriorityEntry& a, const PriorityEntry& b) {
            return a.priority > b.priority;
        });

    // No need to reorder - PackConstantBuffer will take first MAX_GRASS_INTERACTORS
    // In future, could swap array elements to prioritize slots
}

void GrassInteractionManager::CleanupExpired(float deltaTime) {
    for (auto& interactor : m_interactors) {
        if (!interactor.active) continue;

        if (interactor.lifetime > 0.0f) {
            interactor.lifetime -= deltaTime;
            if (interactor.lifetime <= 0.0f) {
                interactor.active = false;
                interactor.type = GrassInteractorType::None;
                m_activeCount = std::max(0u, m_activeCount - 1);
            }
        }
    }
}

// CPU grass bend calculation (for LOD billboards or debug)
glm::vec3 CalculateGrassBendCPU(
    const glm::vec3& grassWorldPos,
    float vertexHeight,
    const GrassInteractor* interactors,
    uint32_t interactorCount,
    float globalStrength
) {
    glm::vec3 totalOffset(0.0f);
    float maxBend = 0.0f;

    for (uint32_t i = 0; i < interactorCount; ++i) {
        const GrassInteractor& interactor = interactors[i];
        if (interactor.type == GrassInteractorType::None) continue;

        // 2D distance
        glm::vec2 toGrass(
            grassWorldPos.x - interactor.position.x,
            grassWorldPos.z - interactor.position.z
        );
        float distance = glm::length(toGrass);

        if (distance > interactor.radius) continue;

        // Falloff
        float t = distance / interactor.radius;
        float falloff = std::pow(1.0f - t, interactor.falloff);

        // Height factor
        float heightFactor = std::pow(vertexHeight, 1.5f);

        // Bend amount
        float bendAmount = falloff * interactor.strength * heightFactor;
        bendAmount = std::min(bendAmount, 1.0f);

        if (bendAmount > maxBend) {
            maxBend = bendAmount;

            // Bend direction
            glm::vec2 bendDir = distance > 0.001f
                ? glm::normalize(toGrass)
                : glm::vec2(1.0f, 0.0f);

            // Add velocity influence
            bendDir += glm::vec2(interactor.velocity.x, interactor.velocity.z) * 0.1f;
            if (glm::length(bendDir) > 0.001f) {
                bendDir = glm::normalize(bendDir);
            }

            float bendDist = bendAmount * 0.4f;  // 40% of grass height max
            totalOffset = glm::vec3(
                bendDir.x * bendDist,
                -bendAmount * 0.1f,  // Push down
                bendDir.y * bendDist
            );
        }
    }

    return totalOffset * globalStrength;
}

} // namespace Cortex::Scene
