// WindSystem.cpp
// Global wind simulation implementation.

#include "WindSystem.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Cortex::Scene {

// Constants
static constexpr float PI = 3.14159265358979f;

WindSystem::WindSystem() {
    m_noiseTexture.resize(NOISE_SIZE * NOISE_SIZE);
}

void WindSystem::Initialize() {
    GenerateNoiseTexture();
    PackConstantBuffer();
}

void WindSystem::Update(float deltaTime) {
    m_time += deltaTime;

    // Update transition
    if (m_isTransitioning) {
        UpdateTransition(deltaTime);
    }

    // Update gusts
    UpdateGusts(deltaTime);

    // Pack GPU buffer
    PackConstantBuffer();
}

void WindSystem::SetGlobalWind(const glm::vec3& direction, float speed) {
    glm::vec3 oldDir = m_globalDirection;
    float oldSpeed = m_globalSpeed;

    m_globalDirection = glm::length(direction) > 0.001f ? glm::normalize(direction) : glm::vec3(1, 0, 0);
    m_globalSpeed = speed;
    m_isTransitioning = false;

    if (m_onWindChange && (oldDir != m_globalDirection || oldSpeed != m_globalSpeed)) {
        m_onWindChange(m_globalDirection, m_globalSpeed);
    }
}

void WindSystem::SetGlobalWindSmooth(const glm::vec3& direction, float speed, float transitionTime) {
    m_startDirection = m_globalDirection;
    m_startSpeed = m_globalSpeed;
    m_targetDirection = glm::length(direction) > 0.001f ? glm::normalize(direction) : glm::vec3(1, 0, 0);
    m_targetSpeed = speed;
    m_transitionDuration = transitionTime;
    m_transitionTime = 0.0f;
    m_isTransitioning = true;
}

void WindSystem::SetGustParameters(float strength, float frequency) {
    m_gustStrength = std::clamp(strength, 0.0f, 1.0f);
    m_gustFrequency = std::max(0.01f, frequency);
}

void WindSystem::TriggerGust(float strength, float duration) {
    m_manualGustActive = true;
    m_manualGustStrength = strength;
    m_manualGustDuration = duration;
    m_manualGustTimer = 0.0f;
}

uint32_t WindSystem::AddZone(const WindZone& zone) {
    uint32_t id = m_nextZoneId++;
    m_zones.push_back(zone);
    m_zoneIds.push_back(id);
    return id;
}

void WindSystem::RemoveZone(uint32_t id) {
    for (size_t i = 0; i < m_zoneIds.size(); ++i) {
        if (m_zoneIds[i] == id) {
            m_zones.erase(m_zones.begin() + i);
            m_zoneIds.erase(m_zoneIds.begin() + i);
            return;
        }
    }
}

void WindSystem::UpdateZone(uint32_t id, const WindZone& zone) {
    for (size_t i = 0; i < m_zoneIds.size(); ++i) {
        if (m_zoneIds[i] == id) {
            m_zones[i] = zone;
            return;
        }
    }
}

void WindSystem::ClearZones() {
    m_zones.clear();
    m_zoneIds.clear();
}

WindZone* WindSystem::GetZone(uint32_t id) {
    for (size_t i = 0; i < m_zoneIds.size(); ++i) {
        if (m_zoneIds[i] == id) {
            return &m_zones[i];
        }
    }
    return nullptr;
}

WindSample WindSystem::SampleWind(const glm::vec3& position) const {
    WindSample result;

    // Start with global wind
    result.direction = m_globalDirection;
    result.speed = m_globalSpeed;
    result.gustFactor = m_currentGust;

    // Add turbulence
    if (m_turbulence > 0.0f) {
        float noiseX = FBMNoise(position.x * 0.01f + m_time * 0.5f, position.z * 0.01f, 3);
        float noiseZ = FBMNoise(position.x * 0.01f, position.z * 0.01f + m_time * 0.5f, 3);

        result.turbulenceOffset = noiseX * m_turbulence;

        glm::vec3 turbDir = result.direction + glm::vec3(noiseX, 0.0f, noiseZ) * m_turbulence * 0.3f;
        if (glm::length(turbDir) > 0.001f) {
            result.direction = glm::normalize(turbDir);
        }

        result.speed *= 1.0f + (noiseX * 0.5f) * m_turbulence;
    }

    // Sample wind zones
    for (const auto& zone : m_zones) {
        if (!zone.enabled) continue;

        WindSample zoneSample = SampleZone(zone, position);
        if (zoneSample.speed > 0.001f) {
            // Blend based on priority and falloff
            float falloff = CalculateFalloff(zone, position);
            if (falloff > 0.0f) {
                float blend = falloff * (zone.priority + 1.0f) / (result.speed + zone.baseSpeed + 0.001f);
                blend = std::clamp(blend, 0.0f, 1.0f);

                result.direction = glm::normalize(glm::mix(result.direction, zoneSample.direction, blend));
                result.speed = glm::mix(result.speed, zoneSample.speed, blend);
                result.gustFactor = std::max(result.gustFactor, zoneSample.gustFactor * falloff);
            }
        }
    }

    return result;
}

glm::vec3 WindSystem::SampleVegetationWind(const glm::vec3& position, float phase) const {
    WindSample base = SampleWind(position);

    // Add high-frequency noise for grass/leaf flutter
    float flutter = std::sin(m_time * 5.0f + phase + position.x * 0.5f) * 0.3f;
    float sway = std::sin(m_time * 1.5f + phase * 0.5f + position.z * 0.3f) * 0.5f;

    glm::vec3 wind = base.GetWindVector();
    wind.x += flutter * base.speed * 0.2f;
    wind.z += sway * base.speed * 0.2f;

    return wind;
}

glm::vec3 WindSystem::SampleParticleWind(const glm::vec3& position) const {
    WindSample base = SampleWind(position);

    // Particles are more affected by turbulence
    float turbNoise = FBMNoise(position.x * 0.05f + m_time, position.z * 0.05f, 2);

    glm::vec3 wind = base.GetWindVector();
    wind += glm::vec3(turbNoise, turbNoise * 0.5f, turbNoise) * base.speed * 0.5f;

    return wind;
}

void WindSystem::SetWindChangeCallback(WindChangeCallback callback) {
    m_onWindChange = std::move(callback);
}

void WindSystem::UpdateGusts(float deltaTime) {
    m_gustTimer += deltaTime;

    // Natural gust pattern
    float gustCycle = m_gustTimer * m_gustFrequency * 2.0f * PI;
    float naturalGust = 1.0f;

    switch (m_gustPattern) {
        case GustPattern::None:
            naturalGust = 1.0f;
            break;

        case GustPattern::Sine:
            naturalGust = 1.0f + std::sin(gustCycle) * m_gustStrength;
            break;

        case GustPattern::Random: {
            // Perlin-based random gusts
            float noise = PerlinNoise2D(m_gustTimer * 0.5f, 0.0f);
            naturalGust = 1.0f + noise * m_gustStrength;
            break;
        }

        case GustPattern::Burst: {
            // Occasional strong bursts
            float t = std::fmod(m_gustTimer * m_gustFrequency, 1.0f);
            if (t < 0.2f) {
                naturalGust = 1.0f + m_gustStrength * std::sin(t * 5.0f * PI);
            } else {
                naturalGust = 1.0f;
            }
            break;
        }

        case GustPattern::Storm: {
            // Chaotic storm pattern
            float noise1 = std::sin(gustCycle * 1.0f) * 0.3f;
            float noise2 = std::sin(gustCycle * 2.3f) * 0.2f;
            float noise3 = std::sin(gustCycle * 0.7f) * 0.5f;
            float random = PerlinNoise2D(m_gustTimer * 2.0f, m_gustTimer * 1.5f);
            naturalGust = 1.0f + (noise1 + noise2 + noise3 + random * 0.5f) * m_gustStrength;
            break;
        }
    }

    // Manual gust override
    if (m_manualGustActive) {
        m_manualGustTimer += deltaTime;
        if (m_manualGustTimer >= m_manualGustDuration) {
            m_manualGustActive = false;
        } else {
            // Smooth rise and fall
            float t = m_manualGustTimer / m_manualGustDuration;
            float envelope = std::sin(t * PI);
            naturalGust = std::max(naturalGust, 1.0f + m_manualGustStrength * envelope);
        }
    }

    m_currentGust = std::max(0.1f, naturalGust);
}

void WindSystem::UpdateTransition(float deltaTime) {
    m_transitionTime += deltaTime;
    float t = std::min(m_transitionTime / m_transitionDuration, 1.0f);

    // Smoothstep
    float smoothT = t * t * (3.0f - 2.0f * t);

    // Interpolate direction using slerp-like approach
    float dot = glm::dot(m_startDirection, m_targetDirection);
    if (dot > 0.9999f) {
        m_globalDirection = m_targetDirection;
    } else {
        m_globalDirection = glm::normalize(glm::mix(m_startDirection, m_targetDirection, smoothT));
    }

    m_globalSpeed = glm::mix(m_startSpeed, m_targetSpeed, smoothT);

    if (t >= 1.0f) {
        m_isTransitioning = false;
        m_globalDirection = m_targetDirection;
        m_globalSpeed = m_targetSpeed;

        if (m_onWindChange) {
            m_onWindChange(m_globalDirection, m_globalSpeed);
        }
    }
}

WindSample WindSystem::SampleZone(const WindZone& zone, const glm::vec3& position) const {
    WindSample sample;
    sample.gustFactor = 1.0f;

    switch (zone.type) {
        case WindZoneType::Global:
            sample.direction = zone.direction;
            sample.speed = zone.baseSpeed;
            break;

        case WindZoneType::Directional:
            sample.direction = zone.direction;
            sample.speed = zone.baseSpeed;
            break;

        case WindZoneType::Spherical: {
            glm::vec3 toCenter = zone.position - position;
            float dist = glm::length(toCenter);
            if (dist > 0.001f) {
                sample.direction = toCenter / dist;  // Inward
                sample.speed = zone.baseSpeed;
            }
            break;
        }

        case WindZoneType::Cylindrical: {
            // Vortex around Y axis
            glm::vec3 toCenter = zone.position - position;
            toCenter.y = 0.0f;
            float dist = glm::length(toCenter);
            if (dist > 0.001f) {
                // Perpendicular (tangent) direction
                sample.direction = glm::normalize(glm::vec3(-toCenter.z, 0.0f, toCenter.x));
                sample.speed = zone.baseSpeed * (1.0f - dist / zone.radius);
            }
            break;
        }

        case WindZoneType::Box:
            sample.direction = zone.direction;
            sample.speed = zone.baseSpeed;
            break;
    }

    // Add vertical component
    if (zone.verticalFactor != 0.0f || zone.liftFactor != 0.0f) {
        sample.direction.y += zone.verticalFactor;
        sample.direction.y += zone.liftFactor;
        sample.direction = glm::normalize(sample.direction);
    }

    // Add zone turbulence
    if (zone.turbulence > 0.0f) {
        float noise = PerlinNoise2D(position.x * 0.02f + m_time, position.z * 0.02f);
        sample.speed *= 1.0f + noise * zone.turbulence;
    }

    // Gust
    if (zone.gustStrength > 0.0f) {
        float gustPhase = m_time * zone.gustFrequency * 2.0f * PI;
        sample.gustFactor = 1.0f + std::sin(gustPhase) * zone.gustStrength;
    }

    return sample;
}

float WindSystem::CalculateFalloff(const WindZone& zone, const glm::vec3& position) const {
    float distance = 0.0f;

    switch (zone.type) {
        case WindZoneType::Global:
            return 1.0f;

        case WindZoneType::Spherical:
        case WindZoneType::Directional: {
            distance = glm::length(position - zone.position);
            if (distance > zone.radius) return 0.0f;
            break;
        }

        case WindZoneType::Cylindrical: {
            glm::vec3 toCenter = position - zone.position;
            distance = glm::length(glm::vec2(toCenter.x, toCenter.z));
            if (distance > zone.radius) return 0.0f;
            if (std::abs(toCenter.y) > zone.boxExtents.y) return 0.0f;
            break;
        }

        case WindZoneType::Box: {
            glm::vec3 local = glm::abs(position - zone.position);
            if (local.x > zone.boxExtents.x ||
                local.y > zone.boxExtents.y ||
                local.z > zone.boxExtents.z) {
                return 0.0f;
            }
            // Use largest axis distance for falloff
            distance = std::max(std::max(local.x / zone.boxExtents.x,
                                          local.y / zone.boxExtents.y),
                                 local.z / zone.boxExtents.z) * zone.radius;
            break;
        }
    }

    // Calculate falloff
    float normalizedDist = distance / zone.radius;
    if (normalizedDist < zone.falloffStart) {
        return 1.0f;
    }

    float falloffRange = 1.0f - zone.falloffStart;
    float falloffT = (normalizedDist - zone.falloffStart) / falloffRange;
    return std::pow(1.0f - falloffT, zone.falloffExponent);
}

void WindSystem::GenerateNoiseTexture() {
    for (int y = 0; y < NOISE_SIZE; ++y) {
        for (int x = 0; x < NOISE_SIZE; ++x) {
            float nx = static_cast<float>(x) / NOISE_SIZE;
            float ny = static_cast<float>(y) / NOISE_SIZE;
            m_noiseTexture[y * NOISE_SIZE + x] = FBMNoise(nx * 4.0f, ny * 4.0f, 4);
        }
    }
}

void WindSystem::PackConstantBuffer() {
    m_cbData.globalWindDir = glm::vec4(m_globalDirection, m_globalSpeed);
    m_cbData.gustParams = glm::vec4(m_gustStrength, m_gustFrequency, m_time, m_turbulence);
    m_cbData.noiseParams = glm::vec4(0.01f, 0.5f, 0.3f, 0.0f);
    m_cbData.time = m_time;
    m_cbData.deltaTime = 0.016f;
    m_cbData.globalSpeed = m_globalSpeed;
    m_cbData.globalGust = m_currentGust;
}

float WindSystem::PerlinNoise2D(float x, float y) const {
    // Simple hash-based noise
    auto hash = [](int x, int y) -> float {
        int n = x + y * 57;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    };

    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float xf = x - xi;
    float yf = y - yi;

    // Smoothstep
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);

    // Bilinear interpolation
    float a = hash(xi, yi);
    float b = hash(xi + 1, yi);
    float c = hash(xi, yi + 1);
    float d = hash(xi + 1, yi + 1);

    return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
}

float WindSystem::FBMNoise(float x, float y, int octaves) const {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        value += amplitude * PerlinNoise2D(x * frequency, y * frequency);
        maxValue += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value / maxValue;
}

// Global instance
WindSystem& GetWindSystem() {
    static WindSystem instance;
    return instance;
}

} // namespace Cortex::Scene
