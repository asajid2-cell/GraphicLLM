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

float Renderer::GetFogStartDistance() const {
    return GetFeatureState().fogStartDistance;
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

bool Renderer::IsV2ReflectionCandidateEnabled() const {
    return GetFeatureState().v2ReflectionCandidateEnabled;
}

bool Renderer::IsFullSceneCandidateBeautyV3Enabled() const {
    return GetFeatureState().fullSceneCandidateBeautyV3Enabled;
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
    return m_rt.DenoiseState().reflectionHistoryAlpha;
}

float Renderer::GetRTReflectionCompositionStrength() const {
    return m_rt.DenoiseState().reflectionCompositionStrength;
}

float Renderer::GetRTReflectionRoughnessThreshold() const {
    return m_rt.DenoiseState().reflectionRoughnessThreshold;
}

float Renderer::GetRTReflectionHistoryMaxBlend() const {
    return m_rt.DenoiseState().reflectionHistoryMaxBlend;
}

float Renderer::GetRTReflectionFireflyClampLuma() const {
    return m_rt.DenoiseState().reflectionFireflyClampLuma;
}

float Renderer::GetRTReflectionSignalScale() const {
    return m_rt.DenoiseState().reflectionSignalScale;
}

float Renderer::GetRTGIStrength() const {
    return m_rt.DenoiseState().giStrength;
}

float Renderer::GetRTGIRayDistance() const {
    return m_rt.DenoiseState().giRayDistance;
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
    if (m_temporal.AAState().enabled == enabled) {
        return;
    }
    m_temporal.AAState().enabled = enabled;
    // When toggling TAA, reset sample index so the Halton sequence
    // restarts cleanly and avoid sudden large jumps in jitter.
    m_temporal.AAState().sampleIndex = 0;
    m_temporal.AAState().jitterPrevPixels = glm::vec2(0.0f);
    m_temporal.AAState().jitterCurrPixels = glm::vec2(0.0f);
    // Force history to be re-seeded on the next frame so we do not mix
    // incompatible LDR/HDR or pre/post-teleport data.
    InvalidateTAAHistory(enabled ? "feature_enabled" : "feature_disabled");
    spdlog::info("TAA {}", m_temporal.AAState().enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetPCSS(bool enabled) {
    m_shadows.Resources().controls.pcssEnabled = enabled;
}

void Renderer::SetFXAAEnabled(bool enabled) {
    m_postProcessState.fxaaEnabled = enabled;
}

void Renderer::SetV2ReflectionCandidateEnabled(bool enabled) {
    if (m_postProcessState.v2ReflectionCandidateEnabled == enabled) {
        return;
    }
    m_postProcessState.v2ReflectionCandidateEnabled = enabled;
    spdlog::info("V2 reflection candidate beauty {}", enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetFullSceneCandidateBeautyV3Enabled(bool enabled) {
    if (m_postProcessState.fullSceneCandidateBeautyV3Enabled == enabled) {
        return;
    }
    m_postProcessState.fullSceneCandidateBeautyV3Enabled = enabled;
    spdlog::info("FullSceneCandidateBeautyV3 {}", enabled ? "ENABLED" : "DISABLED");
}

bool Renderer::IsTAAEnabled() const {
    return GetFeatureState().taaEnabled;
}

void Renderer::ToggleTAA() {
    SetTAAEnabled(!m_temporal.AAState().enabled);
}

void Renderer::SetParticlesEnabled(bool enabled) {
    m_particles.State().controls.enabledForScene = enabled;
}

bool Renderer::GetParticlesEnabled() const {
    return GetFeatureState().particlesEnabled;
}

void Renderer::SetParticleDensityScale(float scale) {
    m_particles.State().controls.SetDensityScale(scale);
}

float Renderer::GetParticleDensityScale() const {
    return m_particles.State().controls.densityScale;
}

void Renderer::SetParticleTuning(float qualityScale,
                                 float bloomContribution,
                                 float softDepthFade,
                                 float windInfluence) {
    m_particles.State().controls.SetTuning(qualityScale, bloomContribution, softDepthFade, windInfluence);
}

void Renderer::SetParticleEffectPreset(const std::string& presetId) {
    m_particles.State().controls.SetEffectPreset(presetId);
}

const std::string& Renderer::GetParticleEffectPreset() const {
    return m_particles.State().controls.effectPreset;
}

bool Renderer::UsesGpuParticleLifecycle() const {
    return m_pipelineState.particleLifecycleCompute != nullptr &&
           m_pipelineState.singleSrvUavComputeRootSignature != nullptr;
}

void Renderer::SetSSREnabled(bool enabled) {
    if (m_ssr.State().controls.enabled == enabled) {
        return;
    }
    m_ssr.State().controls.enabled = enabled;
    spdlog::info("SSR {}", m_ssr.State().controls.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSSRParams(float maxDistance, float thickness, float strength) {
    const float d = std::clamp(maxDistance, 1.0f, 120.0f);
    const float t = std::clamp(thickness, 0.005f, 1.0f);
    const float s = std::clamp(strength, 0.0f, 1.0f);

    if (std::abs(d - m_ssr.State().controls.maxDistance) < 1e-3f &&
        std::abs(t - m_ssr.State().controls.thickness) < 1e-4f &&
        std::abs(s - m_ssr.State().controls.strength) < 1e-3f) {
        return;
    }

    m_ssr.State().controls.maxDistance = d;
    m_ssr.State().controls.thickness = t;
    m_ssr.State().controls.strength = s;
    spdlog::info("SSR params set to max_distance={}, thickness={}, strength={}",
                 m_ssr.State().controls.maxDistance,
                 m_ssr.State().controls.thickness,
                 m_ssr.State().controls.strength);
}

void Renderer::ToggleSSR() {
    SetSSREnabled(!m_ssr.State().controls.enabled);
}

void Renderer::CycleScreenSpaceEffectsDebug() {
    // Determine current state from flags:
    // 0 = both on, 1 = SSR only, 2 = SSAO only, 3 = both off
    uint32_t state = 0;
    if (m_ssr.State().controls.enabled && m_ssao.State().controls.enabled) {
        state = 0;
    } else if (m_ssr.State().controls.enabled && !m_ssao.State().controls.enabled) {
        state = 1;
    } else if (!m_ssr.State().controls.enabled && m_ssao.State().controls.enabled) {
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

void Renderer::SetFogParams(float density, float height, float falloff, float startDistance) {
    float d = std::max(density, 0.0f);
    float f = std::max(falloff, 0.0f);
    float s = std::max(startDistance, 0.0f);
    if (std::abs(d - m_fogState.density) < 1e-6f &&
        std::abs(s - m_fogState.startDistance) < 1e-6f &&
        std::abs(height - m_fogState.height) < 1e-6f &&
        std::abs(f - m_fogState.falloff) < 1e-6f) {
        return;
    }
    m_fogState.density = d;
    m_fogState.startDistance = s;
    m_fogState.height = height;
    m_fogState.falloff = f;
    spdlog::info("Fog params: density={}, start={}, height={}, falloff={}",
                 m_fogState.density,
                 m_fogState.startDistance,
                 m_fogState.height,
                 m_fogState.falloff);
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
    const bool oldRequested = m_rt.RuntimeState().requested;
    const bool newValue = enabled && m_rt.RuntimeState().supported;
    m_rt.RuntimeState().requested = enabled;
    if (m_rt.RuntimeState().enabled == newValue) {
        if (enabled && !m_rt.RuntimeState().supported && oldRequested != enabled) {
            spdlog::info("Ray tracing toggle requested, but DXR is not supported on this device.");
        }
        return;
    }
    if (enabled && !m_rt.RuntimeState().supported) {
        spdlog::info("Ray tracing toggle requested, but DXR is not supported on this device.");
        return;
    }
    m_rt.RuntimeState().enabled = newValue;
    InvalidateRTShadowHistory(m_rt.RuntimeState().enabled ? "feature_enabled" : "feature_disabled");
    InvalidateRTReflectionHistory(m_rt.RuntimeState().enabled ? "feature_enabled" : "feature_disabled");
    InvalidateRTGIHistory(m_rt.RuntimeState().enabled ? "feature_enabled" : "feature_disabled");
    spdlog::info("Ray tracing {}", m_rt.RuntimeState().enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetRTReflectionsEnabled(bool enabled) {
    if (m_rt.RuntimeState().reflectionsEnabled == enabled) {
        return;
    }
    m_rt.RuntimeState().reflectionsEnabled = enabled;
    InvalidateRTReflectionHistory(enabled ? "feature_enabled" : "feature_disabled");
}

void Renderer::SetRTGIEnabled(bool enabled) {
    if (m_rt.RuntimeState().giEnabled == enabled) {
        return;
    }
    m_rt.RuntimeState().giEnabled = enabled;
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
    const bool alphaChanged = std::abs(alpha - m_rt.DenoiseState().reflectionHistoryAlpha) > 1e-4f;
    const bool strengthChanged = std::abs(strength - m_rt.DenoiseState().reflectionCompositionStrength) > 1e-4f;
    const bool roughnessChanged = std::abs(roughness - m_rt.DenoiseState().reflectionRoughnessThreshold) > 1e-4f;
    const bool historyBlendChanged = std::abs(historyBlend - m_rt.DenoiseState().reflectionHistoryMaxBlend) > 1e-4f;
    const bool fireflyChanged = std::abs(fireflyClamp - m_rt.DenoiseState().reflectionFireflyClampLuma) > 1e-4f;
    const bool scaleChanged = std::abs(scale - m_rt.DenoiseState().reflectionSignalScale) > 1e-4f;
    if (!alphaChanged &&
        !strengthChanged &&
        !roughnessChanged &&
        !historyBlendChanged &&
        !fireflyChanged &&
        !scaleChanged) {
        return;
    }

    m_rt.DenoiseState().reflectionHistoryAlpha = alpha;
    m_rt.DenoiseState().reflectionAlpha = alpha;
    m_rt.DenoiseState().reflectionCompositionStrength = strength;
    m_rt.DenoiseState().reflectionRoughnessThreshold = roughness;
    m_rt.DenoiseState().reflectionHistoryMaxBlend = historyBlend;
    m_rt.DenoiseState().reflectionFireflyClampLuma = fireflyClamp;
    m_rt.DenoiseState().reflectionSignalScale = scale;
    if (alphaChanged || roughnessChanged || historyBlendChanged || fireflyChanged || scaleChanged) {
        InvalidateRTReflectionHistory("tuning_changed");
    }
    spdlog::info("RT reflection tuning: denoiseAlpha={} compositionStrength={} roughnessThreshold={} historyMaxBlend={} fireflyClampLuma={} signalScale={}",
                 m_rt.DenoiseState().reflectionHistoryAlpha,
                 m_rt.DenoiseState().reflectionCompositionStrength,
                 m_rt.DenoiseState().reflectionRoughnessThreshold,
                 m_rt.DenoiseState().reflectionHistoryMaxBlend,
                 m_rt.DenoiseState().reflectionFireflyClampLuma,
                 m_rt.DenoiseState().reflectionSignalScale);
}

void Renderer::SetRTGITuning(float strength, float rayDistance) {
    m_rt.DenoiseState().giStrength = std::clamp(strength, 0.0f, 1.0f);
    m_rt.DenoiseState().giRayDistance = std::clamp(rayDistance, 0.5f, 20.0f);
}

} // namespace Cortex::Graphics
