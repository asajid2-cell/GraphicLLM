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

void Renderer::SetSSREnabled(bool enabled) {
    if (m_ssrResources.enabled == enabled) {
        return;
    }
    m_ssrResources.enabled = enabled;
    spdlog::info("SSR {}", m_ssrResources.enabled ? "ENABLED" : "DISABLED");
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
    bool newValue = enabled && m_rtRuntimeState.supported;
    if (m_rtRuntimeState.enabled == newValue) {
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

} // namespace Cortex::Graphics
