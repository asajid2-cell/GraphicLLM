#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

float Renderer::GetExposure() const {
    return GetQualityState().exposure;
}

void Renderer::SetExposure(float exposure) {
    float clamped = std::max(exposure, 0.01f);
    if (std::abs(clamped - m_qualityRuntimeState.exposure) < 1e-6f) {
        return;
    }
    m_qualityRuntimeState.exposure = clamped;
    spdlog::info("Renderer exposure set to {}", m_qualityRuntimeState.exposure);
}

float Renderer::GetBloomIntensity() const {
    return GetQualityState().bloomIntensity;
}

void Renderer::SetBloomIntensity(float intensity) {
    float clamped = glm::clamp(intensity, 0.0f, 5.0f);
    if (std::abs(clamped - m_bloomResources.controls.intensity) < 1e-6f) {
        return;
    }
    m_bloomResources.controls.intensity = clamped;
    spdlog::info("Renderer bloom intensity set to {}", m_bloomResources.controls.intensity);
}

void Renderer::SetBloomShape(float threshold, float softKnee, float maxContribution) {
    const float clampedThreshold = glm::clamp(threshold, 0.1f, 10.0f);
    const float clampedSoftKnee = glm::clamp(softKnee, 0.0f, 1.0f);
    const float clampedMaxContribution = glm::clamp(maxContribution, 0.0f, 16.0f);
    if (std::abs(clampedThreshold - m_bloomResources.controls.threshold) < 1e-4f &&
        std::abs(clampedSoftKnee - m_bloomResources.controls.softKnee) < 1e-4f &&
        std::abs(clampedMaxContribution - m_bloomResources.controls.maxContribution) < 1e-4f) {
        return;
    }
    m_bloomResources.controls.threshold = clampedThreshold;
    m_bloomResources.controls.softKnee = clampedSoftKnee;
    m_bloomResources.controls.maxContribution = clampedMaxContribution;
    spdlog::info("Renderer bloom shape set to threshold={} soft_knee={} max={}",
                 m_bloomResources.controls.threshold,
                 m_bloomResources.controls.softKnee,
                 m_bloomResources.controls.maxContribution);
}

void Renderer::SetColorGrade(float warm, float cool) {
    // Clamp to a reasonable range to keep grading subtle.
    float clampedWarm = glm::clamp(warm, -1.0f, 1.0f);
    float clampedCool = glm::clamp(cool, -1.0f, 1.0f);
    if (std::abs(clampedWarm - m_postProcessState.warm) < 1e-3f &&
        std::abs(clampedCool - m_postProcessState.cool) < 1e-3f) {
        return;
    }
    m_postProcessState.warm = clampedWarm;
    m_postProcessState.cool = clampedCool;
    m_postProcessState.colorGradePreset = "custom";
    spdlog::info("Color grade warm/cool set to ({}, {})",
                 m_postProcessState.warm,
                 m_postProcessState.cool);
}

void Renderer::SetToneGrade(float contrast, float saturation) {
    const float clampedContrast = glm::clamp(contrast, 0.5f, 1.5f);
    const float clampedSaturation = glm::clamp(saturation, 0.0f, 2.0f);
    if (std::abs(clampedContrast - m_postProcessState.contrast) < 1e-3f &&
        std::abs(clampedSaturation - m_postProcessState.saturation) < 1e-3f) {
        return;
    }

    m_postProcessState.contrast = clampedContrast;
    m_postProcessState.saturation = clampedSaturation;
    m_postProcessState.colorGradePreset = "custom";
    spdlog::info("Tone grade contrast/saturation set to ({}, {})",
                 m_postProcessState.contrast,
                 m_postProcessState.saturation);
}

void Renderer::SetToneMapperPreset(const std::string& preset) {
    std::string id = preset.empty() ? "aces" : preset;
    if (id == "clean_filmic") {
        id = "aces";
    }
    if (id != "aces" && id != "reinhard" && id != "filmic_soft" && id != "punchy") {
        id = "aces";
    }
    if (m_postProcessState.toneMapperPreset == id) {
        return;
    }
    m_postProcessState.toneMapperPreset = id;
    spdlog::info("Tone mapper preset set to '{}'", m_postProcessState.toneMapperPreset);
}

void Renderer::SetColorGradePreset(const std::string& preset) {
    const std::string id = preset.empty() ? "custom" : preset;
    if (m_postProcessState.colorGradePreset == id) {
        return;
    }
    m_postProcessState.colorGradePreset = id;
    spdlog::info("Color grade preset set to '{}'", m_postProcessState.colorGradePreset);
}

void Renderer::SetCinematicPostEnabled(bool enabled) {
    if (m_postProcessState.cinematicEnabled == enabled) {
        return;
    }
    m_postProcessState.cinematicEnabled = enabled;
    spdlog::info("Cinematic post {}", m_postProcessState.cinematicEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetCinematicPost(float vignette, float lensDirt) {
    const float clampedVignette = glm::clamp(vignette, 0.0f, 1.0f);
    const float clampedLensDirt = glm::clamp(lensDirt, 0.0f, 1.0f);
    if (std::abs(clampedVignette - m_postProcessState.vignette) < 1e-3f &&
        std::abs(clampedLensDirt - m_postProcessState.lensDirt) < 1e-3f) {
        return;
    }

    m_postProcessState.vignette = clampedVignette;
    m_postProcessState.lensDirt = clampedLensDirt;
    spdlog::info("Cinematic post set to vignette={} lens_dirt={}",
                 m_postProcessState.vignette,
                 m_postProcessState.lensDirt);
}

void Renderer::SetCinematicPostEffects(float motionBlur,
                                       float depthOfField,
                                       float dofFocusDistance,
                                       float dofAperture,
                                       bool motionBlurEnabled,
                                       bool depthOfFieldEnabled) {
    const float clampedMotionBlur = glm::clamp(motionBlur, 0.0f, 1.0f);
    const float clampedDepthOfField = glm::clamp(depthOfField, 0.0f, 1.0f);
    const float clampedFocusDistance = glm::clamp(dofFocusDistance, 0.1f, 100.0f);
    const float clampedAperture = glm::clamp(dofAperture, 0.0f, 8.0f);
    if (std::abs(clampedMotionBlur - m_postProcessState.motionBlur) < 1e-3f &&
        std::abs(clampedDepthOfField - m_postProcessState.depthOfField) < 1e-3f &&
        std::abs(clampedFocusDistance - m_postProcessState.dofFocusDistance) < 1e-3f &&
        std::abs(clampedAperture - m_postProcessState.dofAperture) < 1e-3f &&
        motionBlurEnabled == m_postProcessState.motionBlurEnabled &&
        depthOfFieldEnabled == m_postProcessState.depthOfFieldEnabled) {
        return;
    }

    m_postProcessState.motionBlur = clampedMotionBlur;
    m_postProcessState.motionBlurEnabled = motionBlurEnabled;
    m_postProcessState.depthOfField = clampedDepthOfField;
    m_postProcessState.depthOfFieldEnabled = depthOfFieldEnabled;
    m_postProcessState.dofFocusDistance = clampedFocusDistance;
    m_postProcessState.dofAperture = clampedAperture;
    spdlog::info("Cinematic post effects set to motion_blur={} motion_blur_enabled={} depth_of_field={} depth_of_field_enabled={} focus_distance={} aperture={}",
                 m_postProcessState.motionBlur,
                 m_postProcessState.motionBlurEnabled,
                 m_postProcessState.depthOfField,
                 m_postProcessState.depthOfFieldEnabled,
                 m_postProcessState.dofFocusDistance,
                 m_postProcessState.dofAperture);
}

float Renderer::GetRenderScale() const {
    return GetQualityState().renderScale;
}

void Renderer::SetActiveGraphicsPreset(const std::string& id, bool dirtyFromUI) {
    m_qualityRuntimeState.activeGraphicsPresetId = id.empty() ? "runtime" : id;
    m_qualityRuntimeState.graphicsPresetDirtyFromUI = dirtyFromUI;
}

std::string Renderer::GetActiveGraphicsPreset() const {
    return m_qualityRuntimeState.activeGraphicsPresetId.empty()
        ? "runtime"
        : m_qualityRuntimeState.activeGraphicsPresetId;
}

bool Renderer::IsGraphicsPresetDirtyFromUI() const {
    return m_qualityRuntimeState.graphicsPresetDirtyFromUI;
}

void Renderer::SetRenderScale(float scale) {
    if (m_frameLifecycle.deviceRemoved) {
        return;
    }

    float clamped = std::clamp(scale, 0.5f, 1.5f);

    if (m_services.window) {
        const unsigned int width = std::max(1u, m_services.window->GetWidth());
        const unsigned int height = std::max(1u, m_services.window->GetHeight());
        const bool heavyEffects =
            m_rtRuntimeState.enabled || m_ssrResources.enabled || m_ssaoResources.enabled ||
            m_rtRuntimeState.reflectionsEnabled || m_rtRuntimeState.giEnabled;

        if (m_services.device) {
            const auto budget = BudgetPlanner::BuildPlan(
                m_services.device->GetDedicatedVideoMemoryBytes(),
                width,
                height);
            if (budget.targetRenderScale > 0.0f && budget.targetRenderScale < 1.0f) {
                clamped = std::min(clamped, budget.targetRenderScale);
            }
        }

        if (height >= 2160 || width >= 3840) {
            const float maxScale = heavyEffects ? 0.6f : 0.75f;
            clamped = std::clamp(clamped, 0.5f, maxScale);
        } else if (height >= 1440 || width >= 2560) {
            const float maxScale = heavyEffects ? 0.8f : 1.0f;
            clamped = std::clamp(clamped, 0.5f, maxScale);
        }
    }

    if (std::abs(m_qualityRuntimeState.renderScale - clamped) > 0.0001f) {
        m_qualityRuntimeState.renderScale = clamped;
        InvalidateTAAHistory("render_scale_changed");
        InvalidateRTShadowHistory("render_scale_changed");
        InvalidateRTReflectionHistory("render_scale_changed");
        InvalidateRTGIHistory("render_scale_changed");
    }
}

void Renderer::ApplySafeQualityPreset() {
    // Budget preset for memory-constrained systems. Preserve renderer feature
    // flags and reduce the resolution/shadow footprint first so advanced
    // effects remain available for diagnosis and presentation.
    SetRenderScale(0.67f);

    // Cap shadow-map resolution aggressively to keep cascaded shadows from
    // dominating memory and bandwidth in conservative mode.
    m_shadowResources.mapSize = std::min(m_shadowResources.mapSize, 1024.0f);
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        m_shadowResources.cascadeResolutionScale[i] =
            std::min(m_shadowResources.cascadeResolutionScale[i], 0.60f);
    }
    // If the current atlas is larger than the new safe size, recreate it so
    // the VRAM savings take effect immediately instead of waiting for a
    // resize-triggered reallocation.
    RecreateShadowMapResourcesForCurrentSize();

    spdlog::info("Renderer: applied memory-budget preset (scale=0.67, shadows capped, feature flags preserved)");
}

} // namespace Cortex::Graphics
