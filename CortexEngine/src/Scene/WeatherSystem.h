#pragma once

// WeatherSystem.h
// Dynamic weather state machine with biome integration.
// Controls precipitation, clouds, fog, and atmospheric effects.
//
// Reference: "Real-Time Volumetric Cloudscapes" - Horizon Zero Dawn GDC
// Reference: "Creating the Atmospheric World of Ghost of Tsushima" - GDC 2021

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <random>

namespace Cortex::Scene {

// Forward declarations
enum class BiomeType : uint8_t;

// Weather types
enum class WeatherType : uint8_t {
    Clear = 0,
    PartlyCloudy = 1,
    Cloudy = 2,
    Overcast = 3,
    LightRain = 4,
    Rain = 5,
    HeavyRain = 6,
    Thunderstorm = 7,
    LightSnow = 8,
    Snow = 9,
    Blizzard = 10,
    Fog = 11,
    DenseFog = 12,
    Sandstorm = 13,
    COUNT
};

// Weather severity level
enum class WeatherSeverity : uint8_t {
    None = 0,
    Light = 1,
    Moderate = 2,
    Heavy = 3,
    Extreme = 4
};

// Weather state for rendering and gameplay
struct WeatherState {
    WeatherType current = WeatherType::Clear;
    WeatherType target = WeatherType::Clear;
    float transitionProgress = 1.0f;     // 0-1, how far into transition

    // Cloud parameters
    float cloudCoverage = 0.0f;          // 0-1
    float cloudDensity = 0.5f;           // 0-1
    float cloudHeight = 2000.0f;         // Meters
    float cloudSpeed = 0.01f;            // UV scroll speed
    glm::vec4 cloudColor = glm::vec4(1.0f);

    // Precipitation
    float precipitationIntensity = 0.0f; // 0-1
    float precipitationSize = 1.0f;      // Particle size multiplier
    bool isRain = true;                  // Rain vs snow
    float wetness = 0.0f;                // Surface wetness 0-1

    // Wind
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);
    float windSpeed = 0.0f;              // m/s
    float gustStrength = 0.0f;           // 0-1

    // Fog
    float fogDensity = 0.0f;             // 0-1
    float fogHeight = 100.0f;            // Height where fog fades
    glm::vec3 fogColor = glm::vec3(0.7f, 0.75f, 0.8f);

    // Lightning
    float lightningChance = 0.0f;        // Per-second chance
    float lightningIntensity = 0.0f;     // Current flash 0-1

    // Atmosphere
    float ambientBrightness = 1.0f;      // Multiplier for ambient
    float sunIntensity = 1.0f;           // Multiplier for sun
    glm::vec3 atmosphereTint = glm::vec3(1.0f);

    // Temperature (affects biome behavior)
    float temperature = 20.0f;           // Celsius
};

// Weather transition
struct WeatherTransition {
    WeatherType from;
    WeatherType to;
    float duration = 60.0f;              // Seconds
    float elapsed = 0.0f;
};

// Per-biome weather probabilities
struct BiomeWeatherConfig {
    BiomeType biome;
    std::string biomeName;

    // Base probabilities (should sum to ~1)
    float clearChance = 0.4f;
    float cloudyChance = 0.3f;
    float rainChance = 0.2f;
    float stormChance = 0.05f;
    float fogChance = 0.05f;
    float snowChance = 0.0f;
    float sandstormChance = 0.0f;

    // Temperature range
    float minTemperature = 10.0f;
    float maxTemperature = 25.0f;

    // Weather duration range (seconds)
    float minWeatherDuration = 120.0f;
    float maxWeatherDuration = 600.0f;

    // Transition time range
    float minTransitionTime = 30.0f;
    float maxTransitionTime = 120.0f;
};

// Weather preset for quick setup
struct WeatherPreset {
    std::string name;
    WeatherState state;
};

// GPU constant buffer for weather rendering
struct alignas(16) WeatherCB {
    glm::vec4 cloudParams;       // x=coverage, y=density, z=height, w=speed
    glm::vec4 cloudColor;
    glm::vec4 precipParams;      // x=intensity, y=size, z=isRain, w=wetness
    glm::vec4 windParams;        // xy=direction, z=speed, w=gustStrength
    glm::vec4 fogParams;         // x=density, y=height, z=unused, w=unused
    glm::vec4 fogColor;          // rgb=color, a=unused
    glm::vec4 atmosphereParams;  // x=ambientBright, y=sunIntensity, z=lightning, w=unused
    glm::vec4 atmosphereTint;
    float time;
    float deltaTime;
    float temperature;
    float padding;
};

// Weather event callback
using WeatherChangeCallback = std::function<void(WeatherType oldWeather, WeatherType newWeather)>;
using LightningCallback = std::function<void(const glm::vec3& strikePosition)>;

class WeatherSystem {
public:
    WeatherSystem();
    ~WeatherSystem() = default;

    // Initialize
    void Initialize();

    // Update (call each frame)
    void Update(float deltaTime);

    // Force weather change
    void SetWeather(WeatherType type, float transitionTime = 60.0f);
    void SetWeatherInstant(WeatherType type);

    // Get current state
    const WeatherState& GetState() const { return m_state; }
    WeatherType GetCurrentWeather() const { return m_state.current; }
    WeatherType GetTargetWeather() const { return m_state.target; }
    bool IsTransitioning() const { return m_state.transitionProgress < 1.0f; }

    // Biome integration
    void SetCurrentBiome(BiomeType biome);
    void SetBiomeConfig(const BiomeWeatherConfig& config);
    const BiomeWeatherConfig* GetBiomeConfig(BiomeType biome) const;

    // Wind
    void SetWindDirection(const glm::vec2& direction);
    void SetWindSpeed(float speed);
    glm::vec3 GetWindVector() const;

    // Temperature
    void SetTemperature(float celsius) { m_state.temperature = celsius; }
    float GetTemperature() const { return m_state.temperature; }

    // Time of day integration
    void SetTimeOfDay(float hours);  // 0-24
    float GetTimeOfDay() const { return m_timeOfDay; }

    // Presets
    void ApplyPreset(const std::string& name, float transitionTime = 60.0f);
    void RegisterPreset(const WeatherPreset& preset);

    // Callbacks
    void SetWeatherChangeCallback(WeatherChangeCallback callback);
    void SetLightningCallback(LightningCallback callback);

    // GPU data
    const WeatherCB& GetConstantBuffer() const { return m_cbData; }

    // Auto weather (random transitions based on biome)
    void SetAutoWeather(bool enabled) { m_autoWeather = enabled; }
    bool IsAutoWeatherEnabled() const { return m_autoWeather; }

    // Debug
    std::string GetWeatherName(WeatherType type) const;
    WeatherSeverity GetSeverity() const;

private:
    // Internal state update
    void UpdateTransition(float deltaTime);
    void UpdateLightning(float deltaTime);
    void UpdateWetness(float deltaTime);
    void UpdateAutoWeather(float deltaTime);
    void PackConstantBuffer();

    // Weather interpolation
    WeatherState InterpolateWeather(const WeatherState& from, const WeatherState& to, float t) const;

    // Get target parameters for weather type
    WeatherState GetWeatherParameters(WeatherType type) const;

    // Random weather selection
    WeatherType SelectRandomWeather() const;

    // Lightning strike
    void TriggerLightning();

private:
    WeatherState m_state;
    WeatherState m_startState;           // State at transition start
    WeatherState m_targetState;          // Target state

    WeatherTransition m_transition;
    bool m_isTransitioning = false;

    // Biome configs
    std::vector<BiomeWeatherConfig> m_biomeConfigs;
    BiomeType m_currentBiome;

    // Presets
    std::vector<WeatherPreset> m_presets;

    // Time
    float m_timeOfDay = 12.0f;           // Hours (0-24)
    float m_totalTime = 0.0f;

    // Auto weather
    bool m_autoWeather = false;
    float m_nextWeatherChange = 0.0f;
    std::mt19937 m_rng;

    // Lightning
    float m_lightningTimer = 0.0f;
    float m_lightningFlashTime = 0.0f;
    glm::vec3 m_lastStrikePos = glm::vec3(0.0f);

    // Callbacks
    WeatherChangeCallback m_onWeatherChange;
    LightningCallback m_onLightning;

    // GPU buffer
    WeatherCB m_cbData;
};

// Default biome weather configurations
BiomeWeatherConfig GetDefaultForestWeather();
BiomeWeatherConfig GetDefaultDesertWeather();
BiomeWeatherConfig GetDefaultSwampWeather();
BiomeWeatherConfig GetDefaultTundraWeather();
BiomeWeatherConfig GetDefaultMountainWeather();
BiomeWeatherConfig GetDefaultGrasslandWeather();
BiomeWeatherConfig GetDefaultOceanWeather();

} // namespace Cortex::Scene
