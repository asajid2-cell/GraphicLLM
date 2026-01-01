#pragma once

// AmbientZone.h
// Biome-specific ambient audio system.
// Handles environmental sounds, day/night variations, and zone blending.

#include "AudioEngine.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Cortex::Audio {

// Forward declarations
class AudioEngine;

// Time of day for ambient variation
enum class TimeOfDay : uint8_t {
    Dawn = 0,       // 5:00 - 7:00
    Morning = 1,    // 7:00 - 12:00
    Afternoon = 2,  // 12:00 - 17:00
    Dusk = 3,       // 17:00 - 19:00
    Night = 4,      // 19:00 - 5:00
    COUNT
};

// Weather condition for ambient variation
enum class WeatherCondition : uint8_t {
    Clear = 0,
    Cloudy = 1,
    Rain = 2,
    Storm = 3,
    Snow = 4,
    Fog = 5,
    COUNT
};

// Ambient sound layer (multiple can play simultaneously)
struct AmbientLayer {
    std::string soundName;          // Sound asset name
    float baseVolume = 1.0f;        // Base volume
    float minVolume = 0.0f;         // Minimum during fade
    float maxVolume = 1.0f;         // Maximum volume
    float fadeInTime = 2.0f;        // Seconds to fade in
    float fadeOutTime = 2.0f;       // Seconds to fade out

    bool loop = true;               // Loop sound
    float randomDelayMin = 0.0f;    // Random delay between loops (for non-looping)
    float randomDelayMax = 5.0f;

    // Time variation
    bool useTimeVariation = false;
    std::array<float, static_cast<size_t>(TimeOfDay::COUNT)> timeVolumes = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // Weather variation
    bool useWeatherVariation = false;
    std::array<float, static_cast<size_t>(WeatherCondition::COUNT)> weatherVolumes = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // 3D positioning (optional)
    bool spatial = false;
    float spatialRadius = 50.0f;

    // Runtime state
    AudioHandle activeHandle;
    float currentVolume = 0.0f;
    float targetVolume = 0.0f;
    float nextPlayTime = 0.0f;
    bool isActive = false;
};

// Biome ambient definition
struct BiomeAmbient {
    std::string biomeName;
    std::vector<AmbientLayer> layers;

    // Blend settings
    float transitionTime = 3.0f;    // Seconds to blend between biomes
};

// Ambient zone shape
enum class ZoneShape {
    Sphere,
    Box,
    Cylinder
};

// Ambient zone definition
class AmbientZone {
public:
    AmbientZone();
    ~AmbientZone() = default;

    // Configuration
    void SetPosition(const glm::vec3& position) { m_position = position; }
    void SetRadius(float radius) { m_radius = radius; }
    void SetShape(ZoneShape shape) { m_shape = shape; }
    void SetBoxExtents(const glm::vec3& extents) { m_boxExtents = extents; }
    void SetPriority(float priority) { m_priority = priority; }
    void SetBiomeName(const std::string& name) { m_biomeName = name; }

    // Add ambient layers
    void AddLayer(const AmbientLayer& layer);
    void RemoveLayer(const std::string& soundName);
    void ClearLayers();

    // Time/weather
    void SetTimeOfDay(TimeOfDay time) { m_timeOfDay = time; }
    void SetWeather(WeatherCondition weather) { m_weather = weather; }

    // Update (called by AudioEngine)
    void Update(const glm::vec3& listenerPos, AudioEngine& engine);

    // Query
    bool IsListenerInside(const glm::vec3& listenerPos) const;
    float GetBlendFactor(const glm::vec3& listenerPos) const;
    float GetPriority() const { return m_priority; }
    const std::string& GetBiomeName() const { return m_biomeName; }

    // State
    bool IsActive() const { return m_isActive; }
    void Activate(AudioEngine& engine);
    void Deactivate(AudioEngine& engine);

private:
    // Calculate distance to zone
    float DistanceToZone(const glm::vec3& pos) const;

    // Update layer volumes
    void UpdateLayerVolumes(float blendFactor, AudioEngine& engine);

    // Get effective volume for layer
    float GetEffectiveVolume(const AmbientLayer& layer, float blendFactor) const;

private:
    // Zone definition
    glm::vec3 m_position = glm::vec3(0.0f);
    float m_radius = 50.0f;
    ZoneShape m_shape = ZoneShape::Sphere;
    glm::vec3 m_boxExtents = glm::vec3(50.0f);
    float m_priority = 0.0f;
    std::string m_biomeName;

    // Layers
    std::vector<AmbientLayer> m_layers;

    // Blend settings
    float m_fadeDistance = 10.0f;   // Distance over which to blend

    // State
    TimeOfDay m_timeOfDay = TimeOfDay::Morning;
    WeatherCondition m_weather = WeatherCondition::Clear;
    bool m_isActive = false;
    float m_currentBlend = 0.0f;
    float m_accumulatedTime = 0.0f;
};

// Ambient zone manager
class AmbientZoneManager {
public:
    AmbientZoneManager();
    ~AmbientZoneManager() = default;

    // Zone management
    void AddZone(std::unique_ptr<AmbientZone> zone);
    void RemoveZone(const std::string& biomeName);
    void ClearZones();

    // Update
    void Update(const glm::vec3& listenerPos, AudioEngine& engine, float deltaTime);

    // Global settings
    void SetTimeOfDay(TimeOfDay time);
    void SetWeather(WeatherCondition weather);
    void SetMasterVolume(float volume) { m_masterVolume = volume; }

    // Create zone from biome definition
    std::unique_ptr<AmbientZone> CreateZoneFromBiome(
        const BiomeAmbient& biome,
        const glm::vec3& position,
        float radius
    );

    // Default biome ambients
    static BiomeAmbient GetForestAmbient();
    static BiomeAmbient GetDesertAmbient();
    static BiomeAmbient GetSwampAmbient();
    static BiomeAmbient GetTundraAmbient();
    static BiomeAmbient GetMountainAmbient();
    static BiomeAmbient GetOceanAmbient();

private:
    std::vector<std::unique_ptr<AmbientZone>> m_zones;
    TimeOfDay m_globalTime = TimeOfDay::Morning;
    WeatherCondition m_globalWeather = WeatherCondition::Clear;
    float m_masterVolume = 1.0f;

    // Active zones (sorted by priority)
    std::vector<AmbientZone*> m_activeZones;
};

// One-shot ambient sound emitter (birds, crickets, etc.)
struct AmbientEmitter {
    glm::vec3 position;
    float radius;
    std::vector<std::string> sounds;    // Random selection
    float minInterval = 5.0f;
    float maxInterval = 15.0f;
    float volume = 1.0f;
    bool useTimeVariation = false;
    std::array<float, static_cast<size_t>(TimeOfDay::COUNT)> timeChances = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // Runtime
    float nextPlayTime = 0.0f;
    bool isActive = false;
};

// Random ambient emitter manager
class AmbientEmitterManager {
public:
    AmbientEmitterManager() = default;
    ~AmbientEmitterManager() = default;

    void AddEmitter(const AmbientEmitter& emitter);
    void RemoveEmittersInRadius(const glm::vec3& center, float radius);
    void Clear();

    void Update(const glm::vec3& listenerPos, AudioEngine& engine, float deltaTime, TimeOfDay time);

    void SetActivationRadius(float radius) { m_activationRadius = radius; }

private:
    std::vector<AmbientEmitter> m_emitters;
    float m_activationRadius = 100.0f;
    float m_accumulatedTime = 0.0f;
};

} // namespace Cortex::Audio
