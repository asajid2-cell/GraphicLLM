#pragma once

// GrassInteraction.h
// CPU-side management of grass interaction system.
// Tracks characters, vehicles, and effects that bend grass.

#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <cstdint>

namespace Cortex::Scene {

static constexpr uint32_t MAX_GRASS_INTERACTORS = 16;

// Interactor type (must match shader defines)
enum class GrassInteractorType : uint32_t {
    None = 0,
    Character = 1,
    Vehicle = 2,
    Projectile = 3,
    Explosion = 4
};

// CPU-side interactor data
struct GrassInteractor {
    glm::vec3 position = glm::vec3(0.0f);
    float radius = 1.0f;

    glm::vec3 velocity = glm::vec3(0.0f);
    float strength = 1.0f;

    glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
    GrassInteractorType type = GrassInteractorType::None;

    float height = 2.0f;        // Effect height (for explosions)
    float falloff = 2.0f;       // Falloff exponent
    float recovery = 0.0f;      // Recovery state / wave progress
    float lifetime = -1.0f;     // Negative = permanent, positive = remaining time

    // Entity association (optional)
    uint32_t entityId = 0;
    bool active = false;

    // Update velocity from position change
    void UpdateVelocity(const glm::vec3& newPosition, float deltaTime) {
        if (deltaTime > 0.001f) {
            velocity = (newPosition - position) / deltaTime;
        }
        position = newPosition;
    }
};

// GPU-friendly packed interactor (must match shader struct exactly)
struct alignas(16) GrassInteractorGPU {
    glm::vec3 position;
    float radius;

    glm::vec3 velocity;
    float strength;

    glm::vec3 forward;
    uint32_t type;

    float height;
    float falloff;
    float recovery;
    float padding;
};

// GPU constant buffer layout (must match shader cbuffer)
struct alignas(16) GrassBendCB {
    GrassInteractorGPU interactors[MAX_GRASS_INTERACTORS];

    uint32_t activeInteractors;
    float globalBendStrength;
    float windBendScale;
    float recoverySpeed;

    glm::vec3 windDirection;
    float windStrength;

    float time;
    float grassHeight;
    glm::vec2 padding;
};

// Interactor handle for external reference
struct InteractorHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool IsValid() const { return index != UINT32_MAX; }
    void Invalidate() { index = UINT32_MAX; }
};

// Grass interaction manager
class GrassInteractionManager {
public:
    GrassInteractionManager();
    ~GrassInteractionManager() = default;

    // Initialize system
    void Initialize();

    // Update all interactors (call each frame)
    void Update(float deltaTime);

    // Register interactors
    InteractorHandle RegisterCharacter(uint32_t entityId, const glm::vec3& position, float radius = 0.5f);
    InteractorHandle RegisterVehicle(uint32_t entityId, const glm::vec3& position, const glm::vec3& forward, float radius = 2.0f);
    InteractorHandle RegisterProjectile(const glm::vec3& position, const glm::vec3& velocity, float radius = 0.3f);

    // Create temporary effects
    void CreateExplosion(const glm::vec3& position, float radius, float strength = 2.0f, float duration = 1.0f);
    void CreateImpact(const glm::vec3& position, float radius, float strength = 0.5f, float duration = 0.5f);

    // Update interactor position
    void UpdateInteractor(InteractorHandle handle, const glm::vec3& position);
    void UpdateInteractor(InteractorHandle handle, const glm::vec3& position, const glm::vec3& forward);

    // Remove interactor
    void RemoveInteractor(InteractorHandle handle);
    void RemoveByEntity(uint32_t entityId);

    // Configuration
    void SetGlobalStrength(float strength) { m_globalStrength = strength; }
    void SetRecoverySpeed(float speed) { m_recoverySpeed = speed; }
    void SetWindParameters(const glm::vec3& direction, float strength);
    void SetAverageGrassHeight(float height) { m_averageGrassHeight = height; }

    // Get constant buffer data for GPU upload
    const GrassBendCB& GetConstantBufferData() const { return m_cbData; }

    // Query interactors
    uint32_t GetActiveCount() const { return m_activeCount; }
    const GrassInteractor* GetInteractor(InteractorHandle handle) const;

    // Priority-based sorting (closest/strongest get slots first)
    void SetCameraPosition(const glm::vec3& camPos) { m_cameraPosition = camPos; }

private:
    // Find free slot
    int32_t FindFreeSlot() const;

    // Pack interactors for GPU
    void PackConstantBuffer();

    // Sort by priority (distance to camera, strength)
    void SortByPriority();

    // Remove expired temporary effects
    void CleanupExpired(float deltaTime);

private:
    std::array<GrassInteractor, MAX_GRASS_INTERACTORS * 2> m_interactors;  // Double for overflow
    std::array<uint32_t, MAX_GRASS_INTERACTORS * 2> m_generations;
    uint32_t m_activeCount = 0;

    // Constant buffer data
    GrassBendCB m_cbData;

    // Configuration
    float m_globalStrength = 1.0f;
    float m_recoverySpeed = 2.0f;
    float m_windBendScale = 0.5f;
    float m_averageGrassHeight = 0.5f;
    glm::vec3 m_windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    float m_windStrength = 0.3f;
    float m_time = 0.0f;

    glm::vec3 m_cameraPosition = glm::vec3(0.0f);
};

// Convenience function to get grass bend offset (CPU simulation for LOD)
glm::vec3 CalculateGrassBendCPU(
    const glm::vec3& grassWorldPos,
    float vertexHeight,             // 0-1, base to tip
    const GrassInteractor* interactors,
    uint32_t interactorCount,
    float globalStrength = 1.0f
);

} // namespace Cortex::Scene
