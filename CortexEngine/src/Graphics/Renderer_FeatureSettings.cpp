#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

bool Renderer::IsFogEnabled() const {
    return GetFeatureState().fogEnabled;
}

float Renderer::GetFogDensity() const {
    return GetFeatureState().fogDensity;
}

float Renderer::GetFogHeight() const {
    return GetFeatureState().fogHeight;
}

float Renderer::GetFogFalloff() const {
    return GetFeatureState().fogFalloff;
}

bool Renderer::IsPCSS() const {
    return GetFeatureState().pcssEnabled;
}

bool Renderer::IsFXAAEnabled() const {
    return GetFeatureState().fxaaEnabled;
}

bool Renderer::GetSSAOEnabled() const {
    return GetFeatureState().ssaoEnabled;
}

bool Renderer::GetIBLEnabled() const {
    return GetFeatureState().iblEnabled;
}

bool Renderer::GetSSREnabled() const {
    return GetFeatureState().ssrEnabled;
}

float Renderer::GetGodRayIntensity() const {
    return GetFeatureState().godRayIntensity;
}

float Renderer::GetAreaLightSizeScale() const {
    return GetFeatureState().areaLightSizeScale;
}

bool Renderer::GetRTReflectionsEnabled() const {
    return GetRayTracingState().reflectionsEnabled;
}

bool Renderer::GetRTGIEnabled() const {
    return GetRayTracingState().giEnabled;
}

float Renderer::GetRTReflectionDenoiseAlpha() const {
    return m_rtDenoiseState.reflectionHistoryAlpha;
}

float Renderer::GetRTReflectionCompositionStrength() const {
    return m_rtDenoiseState.reflectionCompositionStrength;
}

float Renderer::GetRTReflectionRoughnessThreshold() const {
    return m_rtDenoiseState.reflectionRoughnessThreshold;
}

float Renderer::GetRTReflectionHistoryMaxBlend() const {
    return m_rtDenoiseState.reflectionHistoryMaxBlend;
}

float Renderer::GetRTReflectionFireflyClampLuma() const {
    return m_rtDenoiseState.reflectionFireflyClampLuma;
}

float Renderer::GetRTReflectionSignalScale() const {
    return m_rtDenoiseState.reflectionSignalScale;
}

bool Renderer::IsRayTracingSupported() const {
    return GetRayTracingState().supported;
}

bool Renderer::IsRayTracingEnabled() const {
    return GetRayTracingState().enabled;
}

bool Renderer::IsDeviceRemoved() const {
    return m_frameLifecycle.deviceRemoved;
}

void Renderer::SetTAAEnabled(bool enabled) {
    if (m_temporalAAState.enabled == enabled) {
        return;
    }
    m_temporalAAState.enabled = enabled;
    // When toggling TAA, reset sample index so the Halton sequence
    // restarts cleanly and avoid sudden large jumps in jitter.
    m_temporalAAState.sampleIndex = 0;
    m_temporalAAState.jitterPrevPixels = glm::vec2(0.0f);
    m_temporalAAState.jitterCurrPixels = glm::vec2(0.0f);
    // Force history to be re-seeded on the next frame so we do not mix
    // incompatible LDR/HDR or pre/post-teleport data.
    InvalidateTAAHistory(enabled ? "feature_enabled" : "feature_disabled");
    spdlog::info("TAA {}", m_temporalAAState.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetPCSS(bool enabled) {
    m_shadowResources.pcssEnabled = enabled;
}

void Renderer::SetFXAAEnabled(bool enabled) {
    m_postProcessState.fxaaEnabled = enabled;
}

bool Renderer::IsTAAEnabled() const {
    return GetFeatureState().taaEnabled;
}

void Renderer::ToggleTAA() {
    SetTAAEnabled(!m_temporalAAState.enabled);
}

void Renderer::SetParticlesEnabled(bool enabled) {
    m_particleState.enabledForScene = enabled;
}

bool Renderer::GetParticlesEnabled() const {
    return GetFeatureState().particlesEnabled;
}

void Renderer::SetParticleDensityScale(float scale) {
    m_particleState.densityScale = std::clamp(scale, 0.0f, 2.0f);
}

float Renderer::GetParticleDensityScale() const {
    return m_particleState.densityScale;
}

void Renderer::SetParticleTuning(float qualityScale,
                                 float bloomContribution,
                                 float softDepthFade,
                                 float windInfluence) {
    m_particleState.qualityScale = std::clamp(qualityScale, 0.25f, 2.0f);
    m_particleState.bloomContribution = std::clamp(bloomContribution, 0.0f, 2.0f);
    m_particleState.softDepthFade = std::clamp(softDepthFade, 0.0f, 1.0f);
    m_particleState.windInfluence = std::clamp(windInfluence, 0.0f, 2.0f);
}

void Renderer::SetSSREnabled(bool enabled) {
    if (m_ssrResources.enabled == enabled) {
        return;
    }
    m_ssrResources.enabled = enabled;
    spdlog::info("SSR {}", m_ssrResources.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSSRParams(float maxDistance, float thickness, float strength) {
    const float d = std::clamp(maxDistance, 1.0f, 120.0f);
    const float t = std::clamp(thickness, 0.005f, 1.0f);
    const float s = std::clamp(strength, 0.0f, 1.0f);

    if (std::abs(d - m_ssrResources.maxDistance) < 1e-3f &&
        std::abs(t - m_ssrResources.thickness) < 1e-4f &&
        std::abs(s - m_ssrResources.strength) < 1e-3f) {
        return;
    }

    m_ssrResources.maxDistance = d;
    m_ssrResources.thickness = t;
    m_ssrResources.strength = s;
    spdlog::info("SSR params set to max_distance={}, thickness={}, strength={}",
                 m_ssrResources.maxDistance,
                 m_ssrResources.thickness,
                 m_ssrResources.strength);
}

void Renderer::ToggleSSR() {
    SetSSREnabled(!m_ssrResources.enabled);
}

void Renderer::CycleScreenSpaceEffectsDebug() {
    // Determine current state from flags:
    // 0 = both on, 1 = SSR only, 2 = SSAO only, 3 = both off
    uint32_t state = 0;
    if (m_ssrResources.enabled && m_ssaoResources.enabled) {
        state = 0;
    } else if (m_ssrResources.enabled && !m_ssaoResources.enabled) {
        state = 1;
    } else if (!m_ssrResources.enabled && m_ssaoResources.enabled) {
        state = 2;
    } else {
        state = 3;
    }

    uint32_t next = (state + 1u) % 4u;
    bool ssrOn = (next == 0u || next == 1u);
    bool ssaoOn = (next == 0u || next == 2u);

    SetSSREnabled(ssrOn);
    SetSSAOEnabled(ssaoOn);

    const char* label = nullptr;
    switch (next) {
        case 0: label = "Both SSR and SSAO ENABLED"; break;
        case 1: label = "SSR ONLY (SSAO disabled)"; break;
        case 2: label = "SSAO ONLY (SSR disabled)"; break;
        case 3: label = "Both SSR and SSAO DISABLED"; break;
        default: label = "Unknown"; break;
    }
    spdlog::info("Screen-space effects debug state: {}", label);
}

void Renderer::SetFogEnabled(bool enabled) {
    if (m_fogState.enabled == enabled) {
        return;
    }
    m_fogState.enabled = enabled;
    spdlog::info("Fog {}", m_fogState.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetFogParams(float density, float height, float falloff) {
    float d = std::max(density, 0.0f);
    float f = std::max(falloff, 0.0f);
    if (std::abs(d - m_fogState.density) < 1e-6f &&
        std::abs(height - m_fogState.height) < 1e-6f &&
        std::abs(f - m_fogState.falloff) < 1e-6f) {
        return;
    }
    m_fogState.density = d;
    m_fogState.height = height;
    m_fogState.falloff = f;
    spdlog::info("Fog params: density={}, height={}, falloff={}", m_fogState.density, m_fogState.height, m_fogState.falloff);
}

void Renderer::SetGodRayIntensity(float intensity) {
    float clamped = glm::clamp(intensity, 0.0f, 5.0f);
    if (std::abs(clamped - m_postProcessState.godRayIntensity) < 1e-3f) {
        return;
    }
    m_postProcessState.godRayIntensity = clamped;
    spdlog::info("God-ray intensity set to {}", m_postProcessState.godRayIntensity);
}

void Renderer::SetAreaLightSizeScale(float scale) {
    float clamped = glm::clamp(scale, 0.25f, 4.0f);
    if (std::abs(clamped - m_lightingState.areaLightSizeScale) < 1e-3f) {
        return;
    }
    m_lightingState.areaLightSizeScale = clamped;
    spdlog::info("Area light size scale set to {}", m_lightingState.areaLightSizeScale);
}

void Renderer::SetRayTracingEnabled(bool enabled) {
    const bool oldRequested = m_rtRuntimeState.requested;
    const bool newValue = enabled && m_rtRuntimeState.supported;
    m_rtRuntimeState.requested = enabled;
    if (m_rtRuntimeState.enabled == newValue) {
        if (enabled && !m_rtRuntimeState.supported && oldRequested != enabled) {
            spdlog::info("Ray tracing toggle requested, but DXR is not supported on this device.");
        }
        return;
    }
    if (enabled && !m_rtRuntimeState.supported) {
        spdlog::info("Ray tracing toggle requested, but DXR is not supported on this device.");
        return;
    }
    m_rtRuntimeState.enabled = newValue;
    InvalidateRTShadowHistory(m_rtRuntimeState.enabled ? "feature_enabled" : "feature_disabled");
    InvalidateRTReflectionHistory(m_rtRuntimeState.enabled ? "feature_enabled" : "feature_disabled");
    InvalidateRTGIHistory(m_rtRuntimeState.enabled ? "feature_enabled" : "feature_disabled");
    spdlog::info("Ray tracing {}", m_rtRuntimeState.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetRTReflectionsEnabled(bool enabled) {
    if (m_rtRuntimeState.reflectionsEnabled == enabled) {
        return;
    }
    m_rtRuntimeState.reflectionsEnabled = enabled;
    InvalidateRTReflectionHistory(enabled ? "feature_enabled" : "feature_disabled");
}

void Renderer::SetRTGIEnabled(bool enabled) {
    if (m_rtRuntimeState.giEnabled == enabled) {
        return;
    }
    m_rtRuntimeState.giEnabled = enabled;
    InvalidateRTGIHistory(enabled ? "feature_enabled" : "feature_disabled");
}

void Renderer::SetRTReflectionTuning(float denoiseAlpha,
                                     float compositionStrength,
                                     float roughnessThreshold,
                                     float historyMaxBlend,
                                     float fireflyClampLuma,
                                     float signalScale) {
    const float alpha = std::clamp(denoiseAlpha, 0.02f, 1.0f);
    const float strength = std::clamp(compositionStrength, 0.0f, 1.0f);
    const float roughness = std::clamp(roughnessThreshold, 0.05f, 1.0f);
    const float historyBlend = std::clamp(historyMaxBlend, 0.0f, 0.5f);
    const float fireflyClamp = std::clamp(fireflyClampLuma, 4.0f, 32.0f);
    const float scale = std::clamp(signalScale, 0.0f, 2.0f);
    const bool alphaChanged = std::abs(alpha - m_rtDenoiseState.reflectionHistoryAlpha) > 1e-4f;
    const bool strengthChanged = std::abs(strength - m_rtDenoiseState.reflectionCompositionStrength) > 1e-4f;
    const bool roughnessChanged = std::abs(roughness - m_rtDenoiseState.reflectionRoughnessThreshold) > 1e-4f;
    const bool historyBlendChanged = std::abs(historyBlend - m_rtDenoiseState.reflectionHistoryMaxBlend) > 1e-4f;
    const bool fireflyChanged = std::abs(fireflyClamp - m_rtDenoiseState.reflectionFireflyClampLuma) > 1e-4f;
    const bool scaleChanged = std::abs(scale - m_rtDenoiseState.reflectionSignalScale) > 1e-4f;
    if (!alphaChanged &&
        !strengthChanged &&
        !roughnessChanged &&
        !historyBlendChanged &&
        !fireflyChanged &&
        !scaleChanged) {
        return;
    }

    m_rtDenoiseState.reflectionHistoryAlpha = alpha;
    m_rtDenoiseState.reflectionAlpha = alpha;
    m_rtDenoiseState.reflectionCompositionStrength = strength;
    m_rtDenoiseState.reflectionRoughnessThreshold = roughness;
    m_rtDenoiseState.reflectionHistoryMaxBlend = historyBlend;
    m_rtDenoiseState.reflectionFireflyClampLuma = fireflyClamp;
    m_rtDenoiseState.reflectionSignalScale = scale;
    if (alphaChanged || roughnessChanged || historyBlendChanged || fireflyChanged || scaleChanged) {
        InvalidateRTReflectionHistory("tuning_changed");
    }
    spdlog::info("RT reflection tuning: denoiseAlpha={} compositionStrength={} roughnessThreshold={} historyMaxBlend={} fireflyClampLuma={} signalScale={}",
                 m_rtDenoiseState.reflectionHistoryAlpha,
                 m_rtDenoiseState.reflectionCompositionStrength,
                 m_rtDenoiseState.reflectionRoughnessThreshold,
                 m_rtDenoiseState.reflectionHistoryMaxBlend,
                 m_rtDenoiseState.reflectionFireflyClampLuma,
                 m_rtDenoiseState.reflectionSignalScale);
}

} // namespace Cortex::Graphics
