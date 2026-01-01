// WeatherSystem.cpp
// Dynamic weather system implementation.

#include "WeatherSystem.h"
#include "BiomeTypes.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Scene {

WeatherSystem::WeatherSystem()
    : m_rng(std::random_device{}())
{
    m_state = GetWeatherParameters(WeatherType::Clear);
    m_startState = m_state;
    m_targetState = m_state;
}

void WeatherSystem::Initialize() {
    // Register default biome configs
    m_biomeConfigs.push_back(GetDefaultForestWeather());
    m_biomeConfigs.push_back(GetDefaultDesertWeather());
    m_biomeConfigs.push_back(GetDefaultSwampWeather());
    m_biomeConfigs.push_back(GetDefaultTundraWeather());
    m_biomeConfigs.push_back(GetDefaultMountainWeather());
    m_biomeConfigs.push_back(GetDefaultGrasslandWeather());
    m_biomeConfigs.push_back(GetDefaultOceanWeather());

    // Register default presets
    WeatherPreset clearPreset;
    clearPreset.name = "Clear";
    clearPreset.state = GetWeatherParameters(WeatherType::Clear);
    m_presets.push_back(clearPreset);

    WeatherPreset stormPreset;
    stormPreset.name = "Storm";
    stormPreset.state = GetWeatherParameters(WeatherType::Thunderstorm);
    m_presets.push_back(stormPreset);

    PackConstantBuffer();
}

void WeatherSystem::Update(float deltaTime) {
    m_totalTime += deltaTime;

    // Update transition
    if (m_isTransitioning) {
        UpdateTransition(deltaTime);
    }

    // Update lightning
    UpdateLightning(deltaTime);

    // Update surface wetness
    UpdateWetness(deltaTime);

    // Auto weather changes
    if (m_autoWeather) {
        UpdateAutoWeather(deltaTime);
    }

    // Pack GPU buffer
    PackConstantBuffer();
}

void WeatherSystem::SetWeather(WeatherType type, float transitionTime) {
    if (type == m_state.target && !m_isTransitioning) {
        return;  // Already at this weather
    }

    WeatherType oldWeather = m_state.current;

    m_startState = m_state;
    m_targetState = GetWeatherParameters(type);
    m_state.target = type;

    m_transition.from = m_state.current;
    m_transition.to = type;
    m_transition.duration = transitionTime;
    m_transition.elapsed = 0.0f;
    m_state.transitionProgress = 0.0f;

    m_isTransitioning = true;

    if (m_onWeatherChange) {
        m_onWeatherChange(oldWeather, type);
    }
}

void WeatherSystem::SetWeatherInstant(WeatherType type) {
    WeatherType oldWeather = m_state.current;

    m_state = GetWeatherParameters(type);
    m_state.current = type;
    m_state.target = type;
    m_state.transitionProgress = 1.0f;

    m_startState = m_state;
    m_targetState = m_state;
    m_isTransitioning = false;

    if (m_onWeatherChange && oldWeather != type) {
        m_onWeatherChange(oldWeather, type);
    }
}

void WeatherSystem::SetCurrentBiome(BiomeType biome) {
    m_currentBiome = biome;

    // Adjust temperature based on biome
    const BiomeWeatherConfig* config = GetBiomeConfig(biome);
    if (config) {
        std::uniform_real_distribution<float> tempDist(config->minTemperature, config->maxTemperature);
        m_state.temperature = tempDist(m_rng);
    }
}

void WeatherSystem::SetBiomeConfig(const BiomeWeatherConfig& config) {
    // Update or add
    for (auto& existing : m_biomeConfigs) {
        if (existing.biome == config.biome) {
            existing = config;
            return;
        }
    }
    m_biomeConfigs.push_back(config);
}

const BiomeWeatherConfig* WeatherSystem::GetBiomeConfig(BiomeType biome) const {
    for (const auto& config : m_biomeConfigs) {
        if (config.biome == biome) {
            return &config;
        }
    }
    return nullptr;
}

void WeatherSystem::SetWindDirection(const glm::vec2& direction) {
    m_state.windDirection = glm::normalize(direction);
}

void WeatherSystem::SetWindSpeed(float speed) {
    m_state.windSpeed = speed;
}

glm::vec3 WeatherSystem::GetWindVector() const {
    return glm::vec3(
        m_state.windDirection.x * m_state.windSpeed,
        0.0f,
        m_state.windDirection.y * m_state.windSpeed
    );
}

void WeatherSystem::SetTimeOfDay(float hours) {
    m_timeOfDay = std::fmod(hours, 24.0f);
    if (m_timeOfDay < 0.0f) m_timeOfDay += 24.0f;

    // Adjust sun intensity based on time
    float sunAngle = (m_timeOfDay - 6.0f) / 12.0f * 3.14159f;
    if (m_timeOfDay < 6.0f || m_timeOfDay > 18.0f) {
        m_state.sunIntensity = 0.0f;
    } else {
        m_state.sunIntensity = std::sin(sunAngle);
    }
}

void WeatherSystem::ApplyPreset(const std::string& name, float transitionTime) {
    for (const auto& preset : m_presets) {
        if (preset.name == name) {
            SetWeather(preset.state.current, transitionTime);
            return;
        }
    }
}

void WeatherSystem::RegisterPreset(const WeatherPreset& preset) {
    // Replace if exists
    for (auto& existing : m_presets) {
        if (existing.name == preset.name) {
            existing = preset;
            return;
        }
    }
    m_presets.push_back(preset);
}

void WeatherSystem::SetWeatherChangeCallback(WeatherChangeCallback callback) {
    m_onWeatherChange = std::move(callback);
}

void WeatherSystem::SetLightningCallback(LightningCallback callback) {
    m_onLightning = std::move(callback);
}

std::string WeatherSystem::GetWeatherName(WeatherType type) const {
    switch (type) {
        case WeatherType::Clear: return "Clear";
        case WeatherType::PartlyCloudy: return "Partly Cloudy";
        case WeatherType::Cloudy: return "Cloudy";
        case WeatherType::Overcast: return "Overcast";
        case WeatherType::LightRain: return "Light Rain";
        case WeatherType::Rain: return "Rain";
        case WeatherType::HeavyRain: return "Heavy Rain";
        case WeatherType::Thunderstorm: return "Thunderstorm";
        case WeatherType::LightSnow: return "Light Snow";
        case WeatherType::Snow: return "Snow";
        case WeatherType::Blizzard: return "Blizzard";
        case WeatherType::Fog: return "Fog";
        case WeatherType::DenseFog: return "Dense Fog";
        case WeatherType::Sandstorm: return "Sandstorm";
        default: return "Unknown";
    }
}

WeatherSeverity WeatherSystem::GetSeverity() const {
    switch (m_state.current) {
        case WeatherType::Clear:
        case WeatherType::PartlyCloudy:
            return WeatherSeverity::None;

        case WeatherType::Cloudy:
        case WeatherType::LightRain:
        case WeatherType::LightSnow:
        case WeatherType::Fog:
            return WeatherSeverity::Light;

        case WeatherType::Overcast:
        case WeatherType::Rain:
        case WeatherType::Snow:
        case WeatherType::DenseFog:
            return WeatherSeverity::Moderate;

        case WeatherType::HeavyRain:
        case WeatherType::Sandstorm:
            return WeatherSeverity::Heavy;

        case WeatherType::Thunderstorm:
        case WeatherType::Blizzard:
            return WeatherSeverity::Extreme;

        default:
            return WeatherSeverity::None;
    }
}

void WeatherSystem::UpdateTransition(float deltaTime) {
    m_transition.elapsed += deltaTime;
    float t = std::min(m_transition.elapsed / m_transition.duration, 1.0f);
    m_state.transitionProgress = t;

    // Smooth interpolation
    float smoothT = t * t * (3.0f - 2.0f * t);  // Smoothstep

    // Interpolate state
    m_state = InterpolateWeather(m_startState, m_targetState, smoothT);
    m_state.current = t < 0.5f ? m_transition.from : m_transition.to;
    m_state.target = m_transition.to;
    m_state.transitionProgress = t;

    if (t >= 1.0f) {
        m_state = m_targetState;
        m_state.current = m_transition.to;
        m_state.target = m_transition.to;
        m_state.transitionProgress = 1.0f;
        m_isTransitioning = false;
    }
}

void WeatherSystem::UpdateLightning(float deltaTime) {
    // Decay lightning flash
    if (m_state.lightningIntensity > 0.0f) {
        m_state.lightningIntensity -= deltaTime * 5.0f;  // Fast decay
        m_state.lightningIntensity = std::max(0.0f, m_state.lightningIntensity);
    }

    // Check for new lightning
    if (m_state.lightningChance > 0.0f) {
        m_lightningTimer += deltaTime;

        float interval = 1.0f / m_state.lightningChance;  // Average interval
        if (m_lightningTimer >= interval) {
            m_lightningTimer = 0.0f;

            std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
            if (chanceDist(m_rng) < m_state.lightningChance * deltaTime) {
                TriggerLightning();
            }
        }
    }
}

void WeatherSystem::UpdateWetness(float deltaTime) {
    float targetWetness = 0.0f;

    if (m_state.precipitationIntensity > 0.0f && m_state.isRain) {
        targetWetness = m_state.precipitationIntensity;
    }

    // Wetness accumulates during rain, decays after
    if (targetWetness > m_state.wetness) {
        m_state.wetness += deltaTime * 0.1f * m_state.precipitationIntensity;
    } else {
        m_state.wetness -= deltaTime * 0.02f;  // Slow dry
    }

    m_state.wetness = std::clamp(m_state.wetness, 0.0f, 1.0f);
}

void WeatherSystem::UpdateAutoWeather(float deltaTime) {
    if (!m_isTransitioning) {
        m_nextWeatherChange -= deltaTime;

        if (m_nextWeatherChange <= 0.0f) {
            WeatherType newWeather = SelectRandomWeather();
            if (newWeather != m_state.current) {
                const BiomeWeatherConfig* config = GetBiomeConfig(m_currentBiome);
                float transitionTime = 60.0f;
                if (config) {
                    std::uniform_real_distribution<float> transDist(
                        config->minTransitionTime, config->maxTransitionTime);
                    transitionTime = transDist(m_rng);
                }

                SetWeather(newWeather, transitionTime);
            }

            // Schedule next change
            if (config) {
                std::uniform_real_distribution<float> durDist(
                    config->minWeatherDuration, config->maxWeatherDuration);
                m_nextWeatherChange = durDist(m_rng);
            } else {
                m_nextWeatherChange = 300.0f;  // Default 5 minutes
            }
        }
    }
}

void WeatherSystem::PackConstantBuffer() {
    m_cbData.cloudParams = glm::vec4(
        m_state.cloudCoverage,
        m_state.cloudDensity,
        m_state.cloudHeight,
        m_state.cloudSpeed
    );
    m_cbData.cloudColor = m_state.cloudColor;

    m_cbData.precipParams = glm::vec4(
        m_state.precipitationIntensity,
        m_state.precipitationSize,
        m_state.isRain ? 1.0f : 0.0f,
        m_state.wetness
    );

    m_cbData.windParams = glm::vec4(
        m_state.windDirection.x,
        m_state.windDirection.y,
        m_state.windSpeed,
        m_state.gustStrength
    );

    m_cbData.fogParams = glm::vec4(
        m_state.fogDensity,
        m_state.fogHeight,
        0.0f,
        0.0f
    );
    m_cbData.fogColor = glm::vec4(m_state.fogColor, 1.0f);

    m_cbData.atmosphereParams = glm::vec4(
        m_state.ambientBrightness,
        m_state.sunIntensity,
        m_state.lightningIntensity,
        0.0f
    );
    m_cbData.atmosphereTint = glm::vec4(m_state.atmosphereTint, 1.0f);

    m_cbData.time = m_totalTime;
    m_cbData.deltaTime = 0.016f;  // Approximate
    m_cbData.temperature = m_state.temperature;
}

WeatherState WeatherSystem::InterpolateWeather(
    const WeatherState& from,
    const WeatherState& to,
    float t
) const {
    WeatherState result;

    result.cloudCoverage = glm::mix(from.cloudCoverage, to.cloudCoverage, t);
    result.cloudDensity = glm::mix(from.cloudDensity, to.cloudDensity, t);
    result.cloudHeight = glm::mix(from.cloudHeight, to.cloudHeight, t);
    result.cloudSpeed = glm::mix(from.cloudSpeed, to.cloudSpeed, t);
    result.cloudColor = glm::mix(from.cloudColor, to.cloudColor, t);

    result.precipitationIntensity = glm::mix(from.precipitationIntensity, to.precipitationIntensity, t);
    result.precipitationSize = glm::mix(from.precipitationSize, to.precipitationSize, t);
    result.isRain = t < 0.5f ? from.isRain : to.isRain;

    result.windDirection = glm::normalize(glm::mix(from.windDirection, to.windDirection, t));
    result.windSpeed = glm::mix(from.windSpeed, to.windSpeed, t);
    result.gustStrength = glm::mix(from.gustStrength, to.gustStrength, t);

    result.fogDensity = glm::mix(from.fogDensity, to.fogDensity, t);
    result.fogHeight = glm::mix(from.fogHeight, to.fogHeight, t);
    result.fogColor = glm::mix(from.fogColor, to.fogColor, t);

    result.lightningChance = glm::mix(from.lightningChance, to.lightningChance, t);

    result.ambientBrightness = glm::mix(from.ambientBrightness, to.ambientBrightness, t);
    result.sunIntensity = glm::mix(from.sunIntensity, to.sunIntensity, t);
    result.atmosphereTint = glm::mix(from.atmosphereTint, to.atmosphereTint, t);

    result.temperature = glm::mix(from.temperature, to.temperature, t);

    return result;
}

WeatherState WeatherSystem::GetWeatherParameters(WeatherType type) const {
    WeatherState state;
    state.current = type;
    state.target = type;
    state.transitionProgress = 1.0f;

    switch (type) {
        case WeatherType::Clear:
            state.cloudCoverage = 0.1f;
            state.cloudDensity = 0.3f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.0f;
            state.windSpeed = 2.0f;
            state.ambientBrightness = 1.0f;
            state.sunIntensity = 1.0f;
            break;

        case WeatherType::PartlyCloudy:
            state.cloudCoverage = 0.3f;
            state.cloudDensity = 0.4f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.0f;
            state.windSpeed = 3.0f;
            state.ambientBrightness = 0.9f;
            state.sunIntensity = 0.85f;
            break;

        case WeatherType::Cloudy:
            state.cloudCoverage = 0.6f;
            state.cloudDensity = 0.5f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.0f;
            state.windSpeed = 4.0f;
            state.ambientBrightness = 0.7f;
            state.sunIntensity = 0.5f;
            break;

        case WeatherType::Overcast:
            state.cloudCoverage = 0.95f;
            state.cloudDensity = 0.7f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.1f;
            state.windSpeed = 5.0f;
            state.ambientBrightness = 0.5f;
            state.sunIntensity = 0.2f;
            state.atmosphereTint = glm::vec3(0.85f, 0.85f, 0.9f);
            break;

        case WeatherType::LightRain:
            state.cloudCoverage = 0.8f;
            state.cloudDensity = 0.6f;
            state.precipitationIntensity = 0.3f;
            state.isRain = true;
            state.fogDensity = 0.15f;
            state.windSpeed = 6.0f;
            state.ambientBrightness = 0.55f;
            state.sunIntensity = 0.3f;
            break;

        case WeatherType::Rain:
            state.cloudCoverage = 0.9f;
            state.cloudDensity = 0.7f;
            state.precipitationIntensity = 0.6f;
            state.isRain = true;
            state.fogDensity = 0.2f;
            state.windSpeed = 8.0f;
            state.gustStrength = 0.3f;
            state.ambientBrightness = 0.4f;
            state.sunIntensity = 0.15f;
            state.atmosphereTint = glm::vec3(0.8f, 0.82f, 0.9f);
            break;

        case WeatherType::HeavyRain:
            state.cloudCoverage = 1.0f;
            state.cloudDensity = 0.9f;
            state.precipitationIntensity = 1.0f;
            state.precipitationSize = 1.2f;
            state.isRain = true;
            state.fogDensity = 0.4f;
            state.windSpeed = 12.0f;
            state.gustStrength = 0.5f;
            state.ambientBrightness = 0.3f;
            state.sunIntensity = 0.05f;
            state.atmosphereTint = glm::vec3(0.7f, 0.72f, 0.8f);
            break;

        case WeatherType::Thunderstorm:
            state.cloudCoverage = 1.0f;
            state.cloudDensity = 1.0f;
            state.cloudColor = glm::vec4(0.3f, 0.32f, 0.4f, 1.0f);
            state.precipitationIntensity = 0.9f;
            state.precipitationSize = 1.3f;
            state.isRain = true;
            state.fogDensity = 0.3f;
            state.windSpeed = 15.0f;
            state.gustStrength = 0.8f;
            state.lightningChance = 0.1f;
            state.ambientBrightness = 0.25f;
            state.sunIntensity = 0.0f;
            state.atmosphereTint = glm::vec3(0.6f, 0.62f, 0.75f);
            break;

        case WeatherType::LightSnow:
            state.cloudCoverage = 0.7f;
            state.cloudDensity = 0.5f;
            state.precipitationIntensity = 0.2f;
            state.precipitationSize = 0.8f;
            state.isRain = false;
            state.fogDensity = 0.1f;
            state.windSpeed = 3.0f;
            state.ambientBrightness = 0.8f;
            state.sunIntensity = 0.5f;
            state.temperature = -5.0f;
            break;

        case WeatherType::Snow:
            state.cloudCoverage = 0.85f;
            state.cloudDensity = 0.6f;
            state.precipitationIntensity = 0.5f;
            state.precipitationSize = 1.0f;
            state.isRain = false;
            state.fogDensity = 0.2f;
            state.windSpeed = 5.0f;
            state.ambientBrightness = 0.6f;
            state.sunIntensity = 0.3f;
            state.temperature = -10.0f;
            state.atmosphereTint = glm::vec3(0.9f, 0.92f, 1.0f);
            break;

        case WeatherType::Blizzard:
            state.cloudCoverage = 1.0f;
            state.cloudDensity = 1.0f;
            state.precipitationIntensity = 1.0f;
            state.precipitationSize = 0.6f;
            state.isRain = false;
            state.fogDensity = 0.7f;
            state.fogColor = glm::vec3(0.9f, 0.92f, 0.95f);
            state.windSpeed = 20.0f;
            state.gustStrength = 0.9f;
            state.ambientBrightness = 0.35f;
            state.sunIntensity = 0.0f;
            state.temperature = -20.0f;
            break;

        case WeatherType::Fog:
            state.cloudCoverage = 0.3f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.5f;
            state.fogHeight = 50.0f;
            state.fogColor = glm::vec3(0.8f, 0.82f, 0.85f);
            state.windSpeed = 1.0f;
            state.ambientBrightness = 0.6f;
            state.sunIntensity = 0.3f;
            break;

        case WeatherType::DenseFog:
            state.cloudCoverage = 0.5f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.9f;
            state.fogHeight = 30.0f;
            state.fogColor = glm::vec3(0.75f, 0.77f, 0.8f);
            state.windSpeed = 0.5f;
            state.ambientBrightness = 0.4f;
            state.sunIntensity = 0.1f;
            break;

        case WeatherType::Sandstorm:
            state.cloudCoverage = 0.2f;
            state.precipitationIntensity = 0.0f;
            state.fogDensity = 0.8f;
            state.fogHeight = 200.0f;
            state.fogColor = glm::vec3(0.8f, 0.7f, 0.5f);
            state.windSpeed = 25.0f;
            state.gustStrength = 0.7f;
            state.ambientBrightness = 0.5f;
            state.sunIntensity = 0.4f;
            state.atmosphereTint = glm::vec3(1.0f, 0.9f, 0.7f);
            state.temperature = 35.0f;
            break;

        default:
            break;
    }

    return state;
}

WeatherType WeatherSystem::SelectRandomWeather() const {
    const BiomeWeatherConfig* config = GetBiomeConfig(m_currentBiome);
    if (!config) {
        return WeatherType::Clear;
    }

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(const_cast<std::mt19937&>(m_rng));

    float cumulative = 0.0f;

    cumulative += config->clearChance;
    if (roll < cumulative) return WeatherType::Clear;

    cumulative += config->cloudyChance;
    if (roll < cumulative) {
        std::uniform_int_distribution<int> cloudyDist(0, 2);
        int cloudLevel = cloudyDist(const_cast<std::mt19937&>(m_rng));
        return static_cast<WeatherType>(static_cast<int>(WeatherType::PartlyCloudy) + cloudLevel);
    }

    cumulative += config->rainChance;
    if (roll < cumulative) {
        std::uniform_int_distribution<int> rainDist(0, 2);
        int rainLevel = rainDist(const_cast<std::mt19937&>(m_rng));
        return static_cast<WeatherType>(static_cast<int>(WeatherType::LightRain) + rainLevel);
    }

    cumulative += config->stormChance;
    if (roll < cumulative) return WeatherType::Thunderstorm;

    cumulative += config->snowChance;
    if (roll < cumulative) {
        std::uniform_int_distribution<int> snowDist(0, 2);
        int snowLevel = snowDist(const_cast<std::mt19937&>(m_rng));
        return static_cast<WeatherType>(static_cast<int>(WeatherType::LightSnow) + snowLevel);
    }

    cumulative += config->fogChance;
    if (roll < cumulative) {
        return dist(const_cast<std::mt19937&>(m_rng)) < 0.7f ? WeatherType::Fog : WeatherType::DenseFog;
    }

    cumulative += config->sandstormChance;
    if (roll < cumulative) return WeatherType::Sandstorm;

    return WeatherType::Clear;
}

void WeatherSystem::TriggerLightning() {
    m_state.lightningIntensity = 1.0f;

    // Random strike position (would be based on camera position in real implementation)
    std::uniform_real_distribution<float> posDist(-500.0f, 500.0f);
    m_lastStrikePos = glm::vec3(
        posDist(m_rng),
        m_state.cloudHeight * 0.8f,
        posDist(m_rng)
    );

    if (m_onLightning) {
        m_onLightning(m_lastStrikePos);
    }
}

// Default biome configurations
BiomeWeatherConfig GetDefaultForestWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Forest;
    config.biomeName = "Forest";
    config.clearChance = 0.35f;
    config.cloudyChance = 0.30f;
    config.rainChance = 0.25f;
    config.stormChance = 0.05f;
    config.fogChance = 0.05f;
    config.snowChance = 0.0f;
    config.minTemperature = 10.0f;
    config.maxTemperature = 25.0f;
    return config;
}

BiomeWeatherConfig GetDefaultDesertWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Desert;
    config.biomeName = "Desert";
    config.clearChance = 0.70f;
    config.cloudyChance = 0.15f;
    config.rainChance = 0.02f;
    config.stormChance = 0.01f;
    config.fogChance = 0.02f;
    config.sandstormChance = 0.10f;
    config.minTemperature = 25.0f;
    config.maxTemperature = 45.0f;
    return config;
}

BiomeWeatherConfig GetDefaultSwampWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Swamp;
    config.biomeName = "Swamp";
    config.clearChance = 0.15f;
    config.cloudyChance = 0.25f;
    config.rainChance = 0.30f;
    config.stormChance = 0.10f;
    config.fogChance = 0.20f;
    config.minTemperature = 15.0f;
    config.maxTemperature = 30.0f;
    return config;
}

BiomeWeatherConfig GetDefaultTundraWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Tundra;
    config.biomeName = "Tundra";
    config.clearChance = 0.30f;
    config.cloudyChance = 0.25f;
    config.rainChance = 0.05f;
    config.snowChance = 0.30f;
    config.fogChance = 0.10f;
    config.minTemperature = -20.0f;
    config.maxTemperature = 5.0f;
    return config;
}

BiomeWeatherConfig GetDefaultMountainWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Mountain;
    config.biomeName = "Mountain";
    config.clearChance = 0.30f;
    config.cloudyChance = 0.30f;
    config.rainChance = 0.15f;
    config.stormChance = 0.05f;
    config.snowChance = 0.15f;
    config.fogChance = 0.05f;
    config.minTemperature = -10.0f;
    config.maxTemperature = 15.0f;
    return config;
}

BiomeWeatherConfig GetDefaultGrasslandWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Grassland;
    config.biomeName = "Grassland";
    config.clearChance = 0.45f;
    config.cloudyChance = 0.30f;
    config.rainChance = 0.15f;
    config.stormChance = 0.05f;
    config.fogChance = 0.05f;
    config.minTemperature = 10.0f;
    config.maxTemperature = 28.0f;
    return config;
}

BiomeWeatherConfig GetDefaultOceanWeather() {
    BiomeWeatherConfig config;
    config.biome = BiomeType::Ocean;
    config.biomeName = "Ocean";
    config.clearChance = 0.30f;
    config.cloudyChance = 0.30f;
    config.rainChance = 0.20f;
    config.stormChance = 0.10f;
    config.fogChance = 0.10f;
    config.minTemperature = 15.0f;
    config.maxTemperature = 25.0f;
    return config;
}

} // namespace Cortex::Scene
