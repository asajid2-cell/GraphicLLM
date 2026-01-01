#pragma once

// WindSystem.h
// Global wind simulation for vegetation, particles, and audio.
// Provides consistent wind behavior across all systems.

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cstdint>

namespace Cortex::Scene {

// Wind zone types
enum class WindZoneType : uint8_t {
    Global = 0,         // Affects entire world
    Directional = 1,    // Local directional force
    Spherical = 2,      // Radial from center
    Cylindrical = 3,    // Vortex/tornado
    Box = 4             // Confined to box volume
};

// Wind gust pattern
enum class GustPattern : uint8_t {
    None = 0,
    Sine = 1,
    Random = 2,
    Burst = 3,
    Storm = 4
};

// Wind zone definition
struct WindZone {
    WindZoneType type = WindZoneType::Global;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);

    // Size/influence
    float radius = 100.0f;              // For spherical/cylindrical
    glm::vec3 boxExtents = glm::vec3(50.0f);  // For box

    // Wind properties
    float baseSpeed = 5.0f;             // m/s
    float turbulence = 0.2f;            // 0-1, random variation
    float gustStrength = 0.3f;          // 0-1, gust intensity
    float gustFrequency = 0.5f;         // Gusts per second

    GustPattern gustPattern = GustPattern::Sine;

    // Falloff
    float falloffStart = 0.7f;          // Start falloff at this fraction of radius
    float falloffExponent = 2.0f;       // Falloff curve

    // Vertical influence
    float verticalFactor = 0.0f;        // How much wind affects Y axis
    float liftFactor = 0.0f;            // Upward force component

    // Priority (higher = takes precedence in overlap)
    float priority = 0.0f;

    // Active state
    bool enabled = true;
};

// Wind sample result
struct WindSample {
    glm::vec3 direction = glm::vec3(0.0f);
    float speed = 0.0f;
    float gustFactor = 1.0f;            // Current gust multiplier
    float turbulenceOffset = 0.0f;      // Random variation

    // Combined wind vector
    glm::vec3 GetWindVector() const {
        return direction * speed * gustFactor;
    }
};

// GPU constant buffer for wind
struct alignas(16) WindCB {
    glm::vec4 globalWindDir;            // xyz=direction, w=speed
    glm::vec4 gustParams;               // x=strength, y=frequency, z=time, w=turbulence
    glm::vec4 noiseParams;              // x=scale, y=speed, z=amplitude, w=unused
    float time;
    float deltaTime;
    float globalSpeed;
    float globalGust;
};

// Wind event callback
using WindChangeCallback = std::function<void(const glm::vec3& newDirection, float newSpeed)>;

class WindSystem {
public:
    WindSystem();
    ~WindSystem() = default;

    // Initialize
    void Initialize();

    // Update (call each frame)
    void Update(float deltaTime);

    // Global wind control
    void SetGlobalWind(const glm::vec3& direction, float speed);
    void SetGlobalWindSmooth(const glm::vec3& direction, float speed, float transitionTime);
    glm::vec3 GetGlobalWindDirection() const { return m_globalDirection; }
    float GetGlobalWindSpeed() const { return m_globalSpeed; }
    glm::vec3 GetGlobalWindVector() const { return m_globalDirection * m_globalSpeed; }

    // Gust control
    void SetGustParameters(float strength, float frequency);
    void TriggerGust(float strength, float duration);
    float GetCurrentGustFactor() const { return m_currentGust; }

    // Turbulence
    void SetTurbulence(float amount) { m_turbulence = amount; }
    float GetTurbulence() const { return m_turbulence; }

    // Wind zones
    uint32_t AddZone(const WindZone& zone);
    void RemoveZone(uint32_t id);
    void UpdateZone(uint32_t id, const WindZone& zone);
    void ClearZones();
    WindZone* GetZone(uint32_t id);

    // Sample wind at position
    WindSample SampleWind(const glm::vec3& position) const;

    // Sample wind for vegetation (adds noise variation)
    glm::vec3 SampleVegetationWind(const glm::vec3& position, float phase) const;

    // Sample wind for particles
    glm::vec3 SampleParticleWind(const glm::vec3& position) const;

    // Callback
    void SetWindChangeCallback(WindChangeCallback callback);

    // GPU buffer
    const WindCB& GetConstantBuffer() const { return m_cbData; }

    // Noise texture data (for shader)
    const std::vector<float>& GetNoiseTexture() const { return m_noiseTexture; }

private:
    // Update gust animation
    void UpdateGusts(float deltaTime);

    // Update smooth transition
    void UpdateTransition(float deltaTime);

    // Sample wind zone contribution
    WindSample SampleZone(const WindZone& zone, const glm::vec3& position) const;

    // Calculate falloff for zone
    float CalculateFalloff(const WindZone& zone, const glm::vec3& position) const;

    // Generate noise texture
    void GenerateNoiseTexture();

    // Pack constant buffer
    void PackConstantBuffer();

    // Procedural noise
    float PerlinNoise2D(float x, float y) const;
    float FBMNoise(float x, float y, int octaves) const;

private:
    // Global wind
    glm::vec3 m_globalDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    float m_globalSpeed = 0.0f;

    // Transition
    bool m_isTransitioning = false;
    glm::vec3 m_targetDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    float m_targetSpeed = 0.0f;
    float m_transitionTime = 0.0f;
    float m_transitionDuration = 0.0f;
    glm::vec3 m_startDirection;
    float m_startSpeed;

    // Gusts
    float m_gustStrength = 0.3f;
    float m_gustFrequency = 0.5f;
    float m_currentGust = 1.0f;
    float m_gustTimer = 0.0f;
    GustPattern m_gustPattern = GustPattern::Sine;

    // Manual gust
    bool m_manualGustActive = false;
    float m_manualGustStrength = 0.0f;
    float m_manualGustDuration = 0.0f;
    float m_manualGustTimer = 0.0f;

    // Turbulence
    float m_turbulence = 0.2f;

    // Time
    float m_time = 0.0f;

    // Wind zones
    std::vector<WindZone> m_zones;
    std::vector<uint32_t> m_zoneIds;
    uint32_t m_nextZoneId = 1;

    // Noise texture
    std::vector<float> m_noiseTexture;
    static constexpr int NOISE_SIZE = 128;

    // Callback
    WindChangeCallback m_onWindChange;

    // GPU buffer
    WindCB m_cbData;
};

// Get wind system singleton
WindSystem& GetWindSystem();

} // namespace Cortex::Scene
