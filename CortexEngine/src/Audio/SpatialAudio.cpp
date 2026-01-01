// SpatialAudio.cpp
// 3D audio processing implementation.

#include "SpatialAudio.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Audio {

// Constants
static constexpr float PI = 3.14159265358979f;
static constexpr float DEG_TO_RAD = PI / 180.0f;

// AttenuationCurve implementation
AttenuationCurve AttenuationCurve::Linear(float maxDistance) {
    AttenuationCurve curve;
    curve.points.push_back({ 0.0f, 1.0f });
    curve.points.push_back({ maxDistance, 0.0f });
    return curve;
}

AttenuationCurve AttenuationCurve::Logarithmic(float refDistance, float maxDistance, float rolloff) {
    AttenuationCurve curve;

    // Sample logarithmic curve at several points
    const int samples = 10;
    for (int i = 0; i <= samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        float distance = refDistance + t * (maxDistance - refDistance);

        // Inverse distance law: volume = refDist / (refDist + rolloff * (dist - refDist))
        float volume = refDistance / (refDistance + rolloff * (distance - refDistance));
        volume = std::max(0.0f, std::min(1.0f, volume));

        curve.points.push_back({ distance, volume });
    }

    return curve;
}

AttenuationCurve AttenuationCurve::Exponential(float halfDistance, float maxDistance) {
    AttenuationCurve curve;

    const int samples = 10;
    for (int i = 0; i <= samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        float distance = t * maxDistance;

        // Exponential: volume = 2^(-distance / halfDistance)
        float volume = std::pow(2.0f, -distance / halfDistance);
        volume = std::max(0.0f, std::min(1.0f, volume));

        curve.points.push_back({ distance, volume });
    }

    return curve;
}

float AttenuationCurve::Sample(float distance) const {
    if (points.empty()) {
        return 1.0f;
    }

    if (distance <= points.front().distance) {
        return points.front().volume;
    }

    if (distance >= points.back().distance) {
        return points.back().volume;
    }

    // Find surrounding points and interpolate
    for (size_t i = 0; i < points.size() - 1; ++i) {
        if (distance >= points[i].distance && distance <= points[i + 1].distance) {
            float t = (distance - points[i].distance) /
                      (points[i + 1].distance - points[i].distance);
            return points[i].volume + t * (points[i + 1].volume - points[i].volume);
        }
    }

    return points.back().volume;
}

// SpatialAudioProcessor implementation
SpatialAudioProcessor::SpatialAudioProcessor() {
    m_listener.position = glm::vec3(0.0f);
    m_listener.forward = glm::vec3(0.0f, 0.0f, 1.0f);
    m_listener.up = glm::vec3(0.0f, 1.0f, 0.0f);
    m_listener.right = glm::vec3(1.0f, 0.0f, 0.0f);
}

void SpatialAudioProcessor::SetListener(const ListenerState& listener) {
    m_listener = listener;

    // Ensure orthonormal basis
    m_listener.forward = glm::normalize(m_listener.forward);
    m_listener.up = glm::normalize(m_listener.up);
    m_listener.right = glm::cross(m_listener.forward, m_listener.up);
}

SpatialState SpatialAudioProcessor::Process(
    const glm::vec3& sourcePos,
    const glm::vec3& sourceVel,
    const SpatialConfig& config
) const {
    SpatialState state;
    state.position = sourcePos;
    state.velocity = sourceVel;

    // Calculate relative position
    glm::vec3 toSource = sourcePos - m_listener.position;
    state.distance = glm::length(toSource);

    // Attenuation
    state.attenuation = CalculateAttenuation(state.distance, config);

    // Doppler
    if (config.dopplerEnabled && state.distance > 0.001f) {
        glm::vec3 relativeVel = sourceVel - m_listener.velocity;
        state.dopplerPitch = CalculateDoppler(relativeVel, state.distance, config.dopplerScale);
    } else {
        state.dopplerPitch = 1.0f;
    }

    // Pan and elevation
    if (state.distance > 0.001f) {
        glm::vec3 dirNorm = toSource / state.distance;
        state.pan = CalculatePan(dirNorm);
        state.elevation = CalculateElevation(dirNorm);
    }

    // Occlusion
    if (config.occlusionModel != OcclusionModel::None) {
        state.occlusion = QueryOcclusion(sourcePos) * config.occlusionFactor;
        state.attenuation *= (1.0f - state.occlusion);
    }

    return state;
}

SpatialState SpatialAudioProcessor::ProcessDirectional(
    const glm::vec3& sourcePos,
    const glm::vec3& sourceVel,
    const glm::vec3& sourceForward,
    const SpatialConfig& config
) const {
    SpatialState state = Process(sourcePos, sourceVel, config);

    state.forward = glm::normalize(sourceForward);

    // Cone attenuation
    if (config.directional && state.distance > 0.001f) {
        glm::vec3 toListener = m_listener.position - sourcePos;
        toListener = glm::normalize(toListener);
        state.coneAttenuation = CalculateConeAttenuation(toListener, sourceForward, config);
        state.attenuation *= state.coneAttenuation;
    }

    return state;
}

float SpatialAudioProcessor::CalculateAttenuation(float distance, const SpatialConfig& config) const {
    if (distance <= config.minDistance) {
        return 1.0f;
    }

    if (distance >= config.maxDistance) {
        return 0.0f;
    }

    switch (config.attenuationModel) {
        case AttenuationModel::None:
            return 1.0f;

        case AttenuationModel::Linear: {
            float t = (distance - config.minDistance) / (config.maxDistance - config.minDistance);
            return 1.0f - t;
        }

        case AttenuationModel::Logarithmic: {
            // Inverse distance law
            float refDist = config.minDistance;
            float rolloff = config.rolloffFactor;
            return refDist / (refDist + rolloff * (distance - refDist));
        }

        case AttenuationModel::ExponentialDecay: {
            float halfDist = (config.minDistance + config.maxDistance) * 0.25f;
            return std::pow(2.0f, -(distance - config.minDistance) / halfDist);
        }

        case AttenuationModel::Custom:
            return config.customCurve.Sample(distance);
    }

    return 1.0f;
}

float SpatialAudioProcessor::CalculateDoppler(
    const glm::vec3& relativeVel,
    float distance,
    float dopplerScale
) const {
    if (distance < 0.001f) {
        return 1.0f;
    }

    // Direction from source to listener
    glm::vec3 dir = glm::normalize(m_listener.position - glm::vec3(0.0f));  // Simplified

    // Relative velocity along direction
    float approachSpeed = glm::dot(relativeVel, dir);

    // Doppler formula: f' = f * (c / (c - vs))
    // where c = speed of sound, vs = source velocity toward listener
    float speedRatio = approachSpeed / m_speedOfSound;
    speedRatio *= dopplerScale * m_globalDopplerScale;

    // Clamp to reasonable range
    speedRatio = std::clamp(speedRatio, -0.9f, 0.9f);

    return 1.0f / (1.0f - speedRatio);
}

float SpatialAudioProcessor::CalculateConeAttenuation(
    const glm::vec3& toListener,
    const glm::vec3& sourceForward,
    const SpatialConfig& config
) const {
    // Angle between source forward and direction to listener
    float dot = glm::dot(glm::normalize(sourceForward), glm::normalize(toListener));
    float angleDeg = std::acos(std::clamp(dot, -1.0f, 1.0f)) * (180.0f / PI);

    float halfInner = config.innerConeAngle * 0.5f;
    float halfOuter = config.outerConeAngle * 0.5f;

    if (angleDeg <= halfInner) {
        return 1.0f;
    }

    if (angleDeg >= halfOuter) {
        return config.outerConeVolume;
    }

    // Interpolate between inner and outer
    float t = (angleDeg - halfInner) / (halfOuter - halfInner);
    return 1.0f + t * (config.outerConeVolume - 1.0f);
}

float SpatialAudioProcessor::CalculatePan(const glm::vec3& toSource) const {
    // Project onto listener's horizontal plane
    float rightAmount = glm::dot(toSource, m_listener.right);

    // Normalize to -1..1 range
    return std::clamp(rightAmount, -1.0f, 1.0f);
}

float SpatialAudioProcessor::CalculateElevation(const glm::vec3& toSource) const {
    // Project onto listener's up axis
    float upAmount = glm::dot(toSource, m_listener.up);

    return std::clamp(upAmount, -1.0f, 1.0f);
}

float SpatialAudioProcessor::QueryOcclusion(const glm::vec3& sourcePos) const {
    if (!m_occlusionQuery) {
        return 0.0f;
    }

    return m_occlusionQuery(m_listener.position, sourcePos);
}

// ReverbZone implementation
bool ReverbZone::Contains(const glm::vec3& point) const {
    float dist = glm::length(point - position);
    return dist <= radius;
}

float ReverbZone::GetBlendFactor(const glm::vec3& point) const {
    float dist = glm::length(point - position);

    if (dist >= radius) {
        return 0.0f;
    }

    float fadeStart = radius - fadeDistance;
    if (dist <= fadeStart) {
        return 1.0f;
    }

    // Linear fade at edge
    return 1.0f - (dist - fadeStart) / fadeDistance;
}

// ReverbManager implementation
void ReverbManager::AddZone(const ReverbZone& zone) {
    m_zones.push_back(zone);

    // Sort by priority (descending)
    std::sort(m_zones.begin(), m_zones.end(),
        [](const ReverbZone& a, const ReverbZone& b) {
            return a.priority > b.priority;
        });
}

void ReverbManager::RemoveZone(const glm::vec3& position, float radius) {
    m_zones.erase(
        std::remove_if(m_zones.begin(), m_zones.end(),
            [&](const ReverbZone& zone) {
                return glm::length(zone.position - position) < 0.1f &&
                       std::abs(zone.radius - radius) < 0.1f;
            }),
        m_zones.end()
    );
}

void ReverbManager::Clear() {
    m_zones.clear();
}

ReverbPreset ReverbManager::GetReverbAtPosition(const glm::vec3& position, float& wetLevel) const {
    wetLevel = 0.0f;

    if (m_zones.empty()) {
        return ReverbPreset::None;
    }

    // Find highest priority zone containing position
    for (const auto& zone : m_zones) {
        float blend = zone.GetBlendFactor(position);
        if (blend > 0.0f) {
            wetLevel = blend;
            return zone.preset;
        }
    }

    return ReverbPreset::None;
}

void ReverbManager::GetReverbParameters(
    ReverbPreset preset,
    float& decayTime,
    float& reflections,
    float& density,
    float& diffusion
) {
    switch (preset) {
        case ReverbPreset::SmallRoom:
            decayTime = 0.5f;
            reflections = 0.8f;
            density = 0.5f;
            diffusion = 0.7f;
            break;

        case ReverbPreset::MediumRoom:
            decayTime = 1.0f;
            reflections = 0.7f;
            density = 0.6f;
            diffusion = 0.8f;
            break;

        case ReverbPreset::LargeRoom:
            decayTime = 1.8f;
            reflections = 0.6f;
            density = 0.7f;
            diffusion = 0.9f;
            break;

        case ReverbPreset::Hall:
            decayTime = 2.5f;
            reflections = 0.5f;
            density = 0.8f;
            diffusion = 1.0f;
            break;

        case ReverbPreset::Cave:
            decayTime = 3.5f;
            reflections = 0.9f;
            density = 1.0f;
            diffusion = 0.6f;
            break;

        case ReverbPreset::Arena:
            decayTime = 4.0f;
            reflections = 0.4f;
            density = 0.9f;
            diffusion = 1.0f;
            break;

        case ReverbPreset::Forest:
            decayTime = 0.8f;
            reflections = 0.3f;
            density = 0.3f;
            diffusion = 0.5f;
            break;

        case ReverbPreset::Underwater:
            decayTime = 1.5f;
            reflections = 0.6f;
            density = 0.9f;
            diffusion = 0.4f;
            break;

        default:
            decayTime = 0.0f;
            reflections = 0.0f;
            density = 0.0f;
            diffusion = 0.0f;
            break;
    }
}

// HRTFProcessor implementation
HRTFProcessor::HRTFProcessor() {
}

HRTFCoefficients HRTFProcessor::GetCoefficients(float azimuth, float elevation) const {
    HRTFCoefficients coeff;

    // ITD (Interaural Time Difference)
    float itd = CalculateITD(azimuth);
    float sampleRate = 44100.0f;
    float delaySamples = itd * sampleRate;

    if (azimuth > 0.0f) {
        // Source on right, left ear delayed
        coeff.leftDelay = delaySamples;
        coeff.rightDelay = 0.0f;
    } else {
        coeff.leftDelay = 0.0f;
        coeff.rightDelay = -delaySamples;
    }

    // ILD (Interaural Level Difference)
    float ild = CalculateILD(azimuth, elevation);
    if (azimuth > 0.0f) {
        coeff.leftGain = 1.0f - ild * 0.5f;
        coeff.rightGain = 1.0f;
    } else {
        coeff.leftGain = 1.0f;
        coeff.rightGain = 1.0f - std::abs(ild) * 0.5f;
    }

    // High frequency shadowing (simplified)
    float shadow = std::abs(azimuth) / 90.0f;
    if (azimuth > 0.0f) {
        coeff.leftHighShelf = 1.0f - shadow * 0.3f;
        coeff.rightHighShelf = 1.0f;
    } else {
        coeff.leftHighShelf = 1.0f;
        coeff.rightHighShelf = 1.0f - shadow * 0.3f;
    }

    return coeff;
}

void HRTFProcessor::ApplyToMatrix(float* outputMatrix, int channels, float azimuth, float elevation) const {
    if (channels < 2) return;

    HRTFCoefficients coeff = GetCoefficients(azimuth, elevation);

    // Simple stereo panning with HRTF gains
    outputMatrix[0] = coeff.leftGain;   // Left channel
    outputMatrix[1] = coeff.rightGain;  // Right channel
}

float HRTFProcessor::CalculateITD(float azimuth) const {
    // Woodworth-Schlosberg formula for spherical head model
    // ITD = (r/c) * (sin(theta) + theta)
    // where r = head radius, c = speed of sound, theta = azimuth

    float c = 343.0f;  // Speed of sound
    float theta = azimuth * DEG_TO_RAD;

    // Maximum ITD is about 0.7ms for humans
    float itd = (m_headRadius / c) * (std::sin(theta) + theta);

    return itd;
}

float HRTFProcessor::CalculateILD(float azimuth, float elevation) const {
    // Simplified ILD model
    // Real HRTF would use measured data or more complex model

    // ILD increases with frequency and azimuth
    float az = std::abs(azimuth) / 90.0f;  // Normalize to 0-1

    // Elevation reduces ILD slightly
    float elev = std::abs(elevation) / 90.0f;

    // Maximum ILD around 6-8 dB at 90 degrees
    float ildDb = 8.0f * az * (1.0f - elev * 0.3f);

    // Convert to linear
    return std::pow(10.0f, -ildDb / 20.0f);
}

// Factory functions
SpatialConfig CreateDefaultSpatialConfig() {
    SpatialConfig config;
    config.attenuationModel = AttenuationModel::Logarithmic;
    config.minDistance = 1.0f;
    config.maxDistance = 100.0f;
    config.rolloffFactor = 1.0f;
    config.dopplerEnabled = true;
    config.dopplerScale = 1.0f;
    return config;
}

SpatialConfig CreateAmbientSpatialConfig(float radius) {
    SpatialConfig config;
    config.attenuationModel = AttenuationModel::Linear;
    config.minDistance = radius * 0.5f;
    config.maxDistance = radius;
    config.rolloffFactor = 0.5f;
    config.dopplerEnabled = false;
    return config;
}

SpatialConfig CreateDirectionalSpatialConfig(float coneAngle) {
    SpatialConfig config;
    config.attenuationModel = AttenuationModel::Logarithmic;
    config.minDistance = 1.0f;
    config.maxDistance = 50.0f;
    config.directional = true;
    config.innerConeAngle = coneAngle;
    config.outerConeAngle = coneAngle * 1.5f;
    config.outerConeVolume = 0.2f;
    return config;
}

SpatialConfig CreatePointSourceConfig(float minDist, float maxDist) {
    SpatialConfig config;
    config.attenuationModel = AttenuationModel::Logarithmic;
    config.minDistance = minDist;
    config.maxDistance = maxDist;
    config.rolloffFactor = 1.0f;
    config.dopplerEnabled = true;
    return config;
}

} // namespace Cortex::Audio
