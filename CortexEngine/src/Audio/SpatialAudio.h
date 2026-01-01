#pragma once

// SpatialAudio.h
// Advanced 3D audio positioning with occlusion and HRTF.
// Reference: Microsoft HRTF Documentation, Wwise SDK Design

#include <glm/glm.hpp>
#include <x3daudio.h>
#include <vector>
#include <functional>
#include <cstdint>

namespace Cortex::Audio {

// Forward declarations
class AudioEngine;
struct AudioHandle;

// Attenuation model
enum class AttenuationModel {
    None,               // No distance attenuation
    Linear,             // Linear rolloff
    Logarithmic,        // Inverse distance (realistic)
    ExponentialDecay,   // Exponential decay
    Custom              // User-defined curve
};

// Reverb preset
enum class ReverbPreset : uint8_t {
    None = 0,
    SmallRoom,
    MediumRoom,
    LargeRoom,
    Hall,
    Cave,
    Arena,
    Forest,
    Underwater,
    COUNT
};

// Occlusion model
enum class OcclusionModel {
    None,               // No occlusion
    Simple,             // Binary occluded/not
    Raycast,            // Single ray from listener to source
    MultiRay            // Multiple rays for soft shadows
};

// Distance curve point
struct AttenuationPoint {
    float distance;     // Distance from source
    float volume;       // 0-1 volume at this distance
};

// Attenuation curve
struct AttenuationCurve {
    std::vector<AttenuationPoint> points;

    // Create default curves
    static AttenuationCurve Linear(float maxDistance);
    static AttenuationCurve Logarithmic(float refDistance, float maxDistance, float rolloff = 1.0f);
    static AttenuationCurve Exponential(float halfDistance, float maxDistance);

    // Sample curve at distance
    float Sample(float distance) const;
};

// Spatial audio source configuration
struct SpatialConfig {
    AttenuationModel attenuationModel = AttenuationModel::Logarithmic;
    AttenuationCurve customCurve;

    float minDistance = 1.0f;       // Full volume within this distance
    float maxDistance = 100.0f;     // Zero volume beyond this
    float rolloffFactor = 1.0f;     // Attenuation steepness

    // Doppler
    bool dopplerEnabled = true;
    float dopplerScale = 1.0f;

    // Directional
    bool directional = false;       // Use cone attenuation
    float innerConeAngle = 360.0f;  // Degrees, full volume
    float outerConeAngle = 360.0f;  // Degrees, outer edge
    float outerConeVolume = 0.0f;   // Volume at outer cone edge

    // Occlusion
    OcclusionModel occlusionModel = OcclusionModel::None;
    float occlusionFactor = 0.0f;   // 0-1, how much occlusion reduces volume

    // Reverb
    ReverbPreset reverbPreset = ReverbPreset::None;
    float reverbSend = 0.0f;        // How much goes to reverb bus

    // HRTF (Head-Related Transfer Function)
    bool hrtfEnabled = false;
};

// Audio source state for 3D processing
struct SpatialState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    // Computed values
    float distance = 0.0f;
    float attenuation = 1.0f;
    float dopplerPitch = 1.0f;
    float coneAttenuation = 1.0f;
    float occlusion = 0.0f;
    float pan = 0.0f;               // -1 to 1
    float elevation = 0.0f;         // -1 to 1 (for HRTF)
};

// Listener state
struct ListenerState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
};

// Occlusion query function type
using OcclusionQueryFunc = std::function<float(const glm::vec3& from, const glm::vec3& to)>;

// Spatial audio processor
class SpatialAudioProcessor {
public:
    SpatialAudioProcessor();
    ~SpatialAudioProcessor() = default;

    // Update listener
    void SetListener(const ListenerState& listener);
    const ListenerState& GetListener() const { return m_listener; }

    // Process source
    SpatialState Process(
        const glm::vec3& sourcePos,
        const glm::vec3& sourceVel,
        const SpatialConfig& config
    ) const;

    // Process directional source
    SpatialState ProcessDirectional(
        const glm::vec3& sourcePos,
        const glm::vec3& sourceVel,
        const glm::vec3& sourceForward,
        const SpatialConfig& config
    ) const;

    // Calculate individual components
    float CalculateAttenuation(float distance, const SpatialConfig& config) const;
    float CalculateDoppler(const glm::vec3& relativeVel, float distance, float dopplerScale) const;
    float CalculateConeAttenuation(const glm::vec3& toListener, const glm::vec3& sourceForward, const SpatialConfig& config) const;
    float CalculatePan(const glm::vec3& toSource) const;
    float CalculateElevation(const glm::vec3& toSource) const;

    // Occlusion
    void SetOcclusionQuery(OcclusionQueryFunc func) { m_occlusionQuery = func; }
    float QueryOcclusion(const glm::vec3& sourcePos) const;

    // Global settings
    void SetSpeedOfSound(float speed) { m_speedOfSound = speed; }
    void SetGlobalDopplerScale(float scale) { m_globalDopplerScale = scale; }

private:
    ListenerState m_listener;
    OcclusionQueryFunc m_occlusionQuery;

    float m_speedOfSound = 343.0f;      // m/s
    float m_globalDopplerScale = 1.0f;
};

// Reverb zone
struct ReverbZone {
    glm::vec3 position;
    float radius;
    ReverbPreset preset;
    float priority;             // Higher = takes precedence
    float fadeDistance;         // Blend distance at edge

    // Check if point is inside zone
    bool Contains(const glm::vec3& point) const;

    // Get blend factor at point (0 = outside, 1 = center)
    float GetBlendFactor(const glm::vec3& point) const;
};

// Reverb manager
class ReverbManager {
public:
    ReverbManager() = default;
    ~ReverbManager() = default;

    void AddZone(const ReverbZone& zone);
    void RemoveZone(const glm::vec3& position, float radius);
    void Clear();

    // Get reverb parameters at position (blends multiple zones)
    ReverbPreset GetReverbAtPosition(const glm::vec3& position, float& wetLevel) const;

    // Get reverb preset parameters
    static void GetReverbParameters(ReverbPreset preset,
                                    float& decayTime,
                                    float& reflections,
                                    float& density,
                                    float& diffusion);

private:
    std::vector<ReverbZone> m_zones;
};

// HRTF filter coefficients
struct HRTFCoefficients {
    float leftDelay;        // ITD (Interaural Time Difference)
    float rightDelay;
    float leftGain;         // ILD (Interaural Level Difference)
    float rightGain;

    // Simple filter coefficients
    float leftHighShelf;    // High frequency attenuation
    float rightHighShelf;
};

// Simple HRTF processor
class HRTFProcessor {
public:
    HRTFProcessor();
    ~HRTFProcessor() = default;

    // Get HRTF coefficients for angle
    HRTFCoefficients GetCoefficients(float azimuth, float elevation) const;

    // Apply HRTF to stereo output matrix
    void ApplyToMatrix(float* outputMatrix, int channels, float azimuth, float elevation) const;

private:
    // Simplified HRTF model using spherical head model
    float CalculateITD(float azimuth) const;
    float CalculateILD(float azimuth, float elevation) const;

    float m_headRadius = 0.0875f;   // Average human head radius (meters)
};

// Create default spatial config for common use cases
SpatialConfig CreateDefaultSpatialConfig();
SpatialConfig CreateAmbientSpatialConfig(float radius);
SpatialConfig CreateDirectionalSpatialConfig(float coneAngle);
SpatialConfig CreatePointSourceConfig(float minDist, float maxDist);

} // namespace Cortex::Audio
