#pragma once

// PrecipitationSystem.h
// GPU-based precipitation particle system for rain and snow.
// Uses compute shaders for simulation and instanced billboards for rendering.
//
// Reference: "GPU-Based Rain Rendering" - NVIDIA
// Reference: "Rendering Raindrops" - Tatarchuk, GPU Gems 2

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Cortex::Scene {

// Forward declarations
class WeatherSystem;

// Precipitation type
enum class PrecipitationType : uint8_t {
    Rain = 0,
    Snow = 1,
    Hail = 2,
    Sleet = 3
};

// Single precipitation particle
struct PrecipitationParticle {
    glm::vec3 position;
    float size;
    glm::vec3 velocity;
    float lifetime;
    glm::vec4 color;
    float rotation;
    float rotationSpeed;
    uint32_t type;
    float distanceToCamera;
};

// GPU particle for compute shader
struct alignas(16) PrecipitationParticleGPU {
    glm::vec4 positionSize;      // xyz=position, w=size
    glm::vec4 velocityLife;      // xyz=velocity, w=lifetime
    glm::vec4 color;             // rgba
    glm::vec4 params;            // x=rotation, y=rotSpeed, z=type, w=distCam
};

// Precipitation system configuration
struct PrecipitationConfig {
    PrecipitationType type = PrecipitationType::Rain;

    // Particle counts
    uint32_t maxParticles = 50000;
    float spawnRate = 5000.0f;       // Particles per second

    // Spawn volume (follows camera)
    float spawnRadius = 30.0f;       // Horizontal radius around camera
    float spawnHeight = 40.0f;       // Height above camera to spawn
    float killHeight = -10.0f;       // Height below camera to kill

    // Particle properties
    float baseSize = 0.02f;          // Meters
    float sizeVariation = 0.3f;      // +/- percentage
    glm::vec4 baseColor = glm::vec4(0.8f, 0.85f, 0.9f, 0.6f);
    float colorVariation = 0.1f;

    // Physics
    float gravity = -9.81f;          // Acceleration
    float terminalVelocity = 9.0f;   // Max fall speed (rain ~9 m/s)
    float windInfluence = 1.0f;      // How much wind affects particles
    float turbulence = 0.2f;         // Random velocity variation

    // Rain specific
    float rainStreakLength = 0.3f;   // Length of motion blur
    float rainAngle = 0.0f;          // Slant angle from wind

    // Snow specific
    float snowSwayAmplitude = 0.5f;  // Side-to-side sway
    float snowSwayFrequency = 2.0f;  // Sway speed
    float snowRotationSpeed = 1.0f;  // Rotation rate

    // Collision
    bool enableCollision = true;
    float collisionBounce = 0.3f;    // Bounce factor
    float splashChance = 0.5f;       // Chance to create splash on hit

    // LOD
    float lodNearDistance = 10.0f;   // Full detail within this
    float lodFarDistance = 50.0f;    // Reduced count beyond this
    float lodFarRatio = 0.3f;        // Particle count at far distance
};

// Splash effect
struct PrecipitationSplash {
    glm::vec3 position;
    float size;
    float lifetime;
    float maxLifetime;
    glm::vec4 color;
    float alpha;
};

// GPU constant buffer for precipitation
struct alignas(16) PrecipitationCB {
    glm::vec4 cameraPosition;        // xyz=pos, w=unused
    glm::vec4 spawnParams;           // x=radius, y=height, z=killHeight, w=spawnRate
    glm::vec4 particleParams;        // x=baseSize, y=sizeVar, z=gravity, w=termVel
    glm::vec4 windParams;            // xy=direction, z=speed, w=influence
    glm::vec4 baseColor;
    glm::vec4 physicsParams;         // x=turbulence, y=bounce, z=type, w=deltaTime
    glm::vec4 rainParams;            // x=streakLength, y=angle, z=unused, w=unused
    glm::vec4 snowParams;            // x=swayAmp, y=swayFreq, z=rotSpeed, w=unused
    float time;
    float deltaTime;
    uint32_t maxParticles;
    uint32_t activeParticles;
};

// Statistics
struct PrecipitationStats {
    uint32_t activeParticles = 0;
    uint32_t particlesSpawned = 0;
    uint32_t particlesKilled = 0;
    uint32_t splashesActive = 0;
    float gpuTimeMs = 0.0f;
};

class PrecipitationSystem {
public:
    PrecipitationSystem();
    ~PrecipitationSystem();

    // Initialize with configuration
    bool Initialize(const PrecipitationConfig& config);
    void Shutdown();

    // Update (call each frame)
    void Update(float deltaTime, const glm::vec3& cameraPos, const glm::vec3& cameraForward);

    // Render particles
    void Render();

    // Configuration
    void SetConfig(const PrecipitationConfig& config);
    const PrecipitationConfig& GetConfig() const { return m_config; }

    // Control
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void SetIntensity(float intensity);  // 0-1, scales spawn rate
    float GetIntensity() const { return m_intensity; }

    // Weather integration
    void SetWind(const glm::vec2& direction, float speed);
    void SetPrecipitationType(PrecipitationType type);

    // Terrain collision
    using HeightQueryFunc = std::function<float(float x, float z)>;
    void SetHeightQuery(HeightQueryFunc func) { m_heightQuery = func; }

    // Splash effects
    void SpawnSplash(const glm::vec3& position, float size);
    const std::vector<PrecipitationSplash>& GetSplashes() const { return m_splashes; }

    // Query
    const PrecipitationStats& GetStats() const { return m_stats; }
    uint32_t GetActiveCount() const { return m_activeParticles; }

    // GPU resources (for renderer integration)
    const PrecipitationCB& GetConstantBuffer() const { return m_cbData; }
    const std::vector<PrecipitationParticleGPU>& GetParticleData() const { return m_particlesCPU; }

private:
    // CPU simulation (fallback when compute unavailable)
    void SimulateCPU(float deltaTime);
    void SpawnParticlesCPU(float deltaTime);
    void UpdateParticlesCPU(float deltaTime);
    void KillDeadParticlesCPU();

    // Particle spawning
    glm::vec3 GetSpawnPosition() const;
    glm::vec3 GetInitialVelocity() const;
    float GetParticleSize() const;
    glm::vec4 GetParticleColor() const;

    // Collision
    bool CheckTerrainCollision(const glm::vec3& pos, float& groundHeight);
    void HandleCollision(PrecipitationParticleGPU& particle, float groundHeight);

    // Pack constant buffer
    void PackConstantBuffer();

private:
    PrecipitationConfig m_config;
    bool m_initialized = false;
    bool m_enabled = true;
    float m_intensity = 1.0f;

    // Particles
    std::vector<PrecipitationParticleGPU> m_particlesCPU;
    uint32_t m_activeParticles = 0;
    float m_spawnAccumulator = 0.0f;

    // Splashes
    std::vector<PrecipitationSplash> m_splashes;
    static constexpr size_t MAX_SPLASHES = 200;

    // Camera tracking
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    glm::vec3 m_cameraForward = glm::vec3(0.0f, 0.0f, 1.0f);

    // Wind
    glm::vec2 m_windDirection = glm::vec2(1.0f, 0.0f);
    float m_windSpeed = 0.0f;

    // Time
    float m_time = 0.0f;

    // Height query
    HeightQueryFunc m_heightQuery;

    // Random
    mutable std::mt19937 m_rng;

    // GPU buffer
    PrecipitationCB m_cbData;

    // Stats
    PrecipitationStats m_stats;
};

// Precipitation render data for shader
struct PrecipitationVertex {
    glm::vec3 position;
    glm::vec2 texCoord;
    glm::vec4 color;
    float size;
    float rotation;
};

// Get default config for rain
PrecipitationConfig GetDefaultRainConfig();

// Get default config for snow
PrecipitationConfig GetDefaultSnowConfig();

} // namespace Cortex::Scene
