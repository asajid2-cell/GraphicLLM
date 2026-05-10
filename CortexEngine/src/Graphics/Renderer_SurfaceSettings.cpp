#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

float Renderer::GetWaterLevel() const {
    return GetWaterState().levelY;
}

float Renderer::GetWaterWaveAmplitude() const {
    return GetWaterState().waveAmplitude;
}

float Renderer::GetWaterWaveLength() const {
    return GetWaterState().waveLength;
}

float Renderer::GetWaterWaveSpeed() const {
    return GetWaterState().waveSpeed;
}

float Renderer::GetWaterSecondaryAmplitude() const {
    return GetWaterState().secondaryAmplitude;
}

float Renderer::GetWaterSteepness() const {
    return GetWaterState().steepness;
}

glm::vec2 Renderer::GetWaterPrimaryDir() const {
    return GetWaterState().primaryDirection;
}

void Renderer::SetWaterParams(float levelY, float amplitude, float waveLength, float speed,
                              float dirX, float dirZ, float secondaryAmplitude, float steepness) {
    m_waterState.levelY = levelY;
    m_waterState.waveAmplitude = amplitude;
    m_waterState.waveLength = (waveLength <= 0.0f) ? 1.0f : waveLength;
    m_waterState.waveSpeed = speed;
    glm::vec2 dir(dirX, dirZ);
    if (glm::length2(dir) < 1e-4f) {
        dir = glm::vec2(1.0f, 0.0f);
    }
    m_waterState.primaryDirection = glm::normalize(dir);
    m_waterState.secondaryAmplitude = glm::max(0.0f, secondaryAmplitude);
    m_waterState.steepness = glm::clamp(steepness, 0.0f, 1.0f);
}

float Renderer::SampleWaterHeightAt(const glm::vec2& worldXZ) const {
    const float amplitude = m_waterState.waveAmplitude;
    const float waveLen   = (m_waterState.waveLength <= 0.0f) ? 1.0f : m_waterState.waveLength;
    const float speed     = m_waterState.waveSpeed;
    const float waterY    = m_waterState.levelY;

    glm::vec2 dir = m_waterState.primaryDirection;
    if (glm::length2(dir) < 1e-4f) {
        dir = glm::vec2(1.0f, 0.0f);
    } else {
        dir = glm::normalize(dir);
    }
    const glm::vec2 dir2(-dir.y, dir.x);

    const float k = 2.0f * glm::pi<float>() / waveLen;
    const float t = m_frameRuntime.totalTime;

    const float phase0 = glm::dot(dir, worldXZ) * k + speed * t;
    const float h0 = amplitude * std::sin(phase0);

    const float phase1 = glm::dot(dir2, worldXZ) * k * 1.3f + speed * 0.8f * t;
    const float h1 = m_waterState.secondaryAmplitude * std::sin(phase1);

    return waterY + h0 + h1;
}

void Renderer::SetFractalParams(float amplitude, float frequency, float octaves,
                                float coordMode, float scaleX, float scaleZ,
                                float lacunarity, float gain,
                                float warpStrength, float noiseType) {
    float amp = glm::clamp(amplitude, 0.0f, 0.5f);
    float freq = glm::clamp(frequency, 0.1f, 4.0f);
    float oct = glm::clamp(octaves, 1.0f, 6.0f);
    float mode = (coordMode >= 0.5f) ? 1.0f : 0.0f;
    float sx = glm::clamp(scaleX, 0.1f, 4.0f);
    float sz = glm::clamp(scaleZ, 0.1f, 4.0f);
    float lac = glm::clamp(lacunarity, 1.0f, 4.0f);
    float gn = glm::clamp(gain, 0.1f, 0.9f);
    float warp = glm::clamp(warpStrength, 0.0f, 1.0f);
    int nt = static_cast<int>(noiseType + 0.5f);
    if (nt < 0) nt = 0;
    if (nt > 3) nt = 3;

    if (std::abs(amp - m_fractalSurfaceState.amplitude) < 1e-6f &&
        std::abs(freq - m_fractalSurfaceState.frequency) < 1e-6f &&
        std::abs(oct - m_fractalSurfaceState.octaves) < 1e-6f &&
        std::abs(mode - m_fractalSurfaceState.coordMode) < 1e-6f &&
        std::abs(sx - m_fractalSurfaceState.scaleX) < 1e-6f &&
        std::abs(sz - m_fractalSurfaceState.scaleZ) < 1e-6f &&
        std::abs(lac - m_fractalSurfaceState.lacunarity) < 1e-6f &&
        std::abs(gn - m_fractalSurfaceState.gain) < 1e-6f &&
        std::abs(warp - m_fractalSurfaceState.warpStrength) < 1e-6f &&
        nt == static_cast<int>(m_fractalSurfaceState.noiseType + 0.5f)) {
        return;
    }

    m_fractalSurfaceState.amplitude = amp;
    m_fractalSurfaceState.frequency = freq;
    m_fractalSurfaceState.octaves = oct;
    m_fractalSurfaceState.coordMode = mode;
    m_fractalSurfaceState.scaleX = sx;
    m_fractalSurfaceState.scaleZ = sz;
    m_fractalSurfaceState.lacunarity = lac;
    m_fractalSurfaceState.gain = gn;
    m_fractalSurfaceState.warpStrength = warp;
    m_fractalSurfaceState.noiseType = static_cast<float>(nt);

    const char* typeLabel = (nt == 0) ? "FBM" : (nt == 1 ? "Ridged" : (nt == 2 ? "Turbulence" : "Cellular"));
    spdlog::info("Fractal params: amp={} freq={} oct={} mode={} scale=({}, {}), lacunarity={}, gain={}, warp={}, type={}",
                 m_fractalSurfaceState.amplitude, m_fractalSurfaceState.frequency, m_fractalSurfaceState.octaves,
                 (m_fractalSurfaceState.coordMode > 0.5f ? "WorldXZ" : "UV"),
                 m_fractalSurfaceState.scaleX, m_fractalSurfaceState.scaleZ,
                 m_fractalSurfaceState.lacunarity, m_fractalSurfaceState.gain,
                 m_fractalSurfaceState.warpStrength, typeLabel);
}

} // namespace Cortex::Graphics
