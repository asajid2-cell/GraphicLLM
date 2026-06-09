#include "RendererControlApplier.h"

#include "Renderer.h"
#include "RendererSceneProfile.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <cmath>
#include <cstdlib>

namespace Cortex::Graphics {

namespace {
float ReadRTShowcaseRenderScaleOverride(float fallback) {
    const char* value = std::getenv("CORTEX_RT_SHOWCASE_RENDER_SCALE");
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const float scale = std::strtof(value, &end);
    if (end == value || !std::isfinite(scale)) {
        return fallback;
    }
    return glm::clamp(scale, 0.5f, 1.0f);
}

float ReadRTShowcaseFloatOverride(const char* name, float fallback, float minValue, float maxValue) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return fallback;
    }

    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || !std::isfinite(parsed)) {
        return fallback;
    }
    return glm::clamp(parsed, minValue, maxValue);
}

bool ReadRTShowcaseDisableFlag(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

void ApplyRTShowcaseDiagnosticFeatureOverrides(Renderer& renderer) {
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_SHADOWS")) {
        renderer.SetShadowsEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_RT")) {
        renderer.SetRayTracingEnabled(false);
        renderer.SetRTReflectionsEnabled(false);
        renderer.SetRTGIEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_RT_REFLECTIONS")) {
        renderer.SetRTReflectionsEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_RT_GI")) {
        renderer.SetRTGIEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_TAA")) {
        renderer.SetTAAEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_FXAA")) {
        renderer.SetFXAAEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_SSR")) {
        renderer.SetSSREnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_SSAO")) {
        renderer.SetSSAOEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_FOG")) {
        renderer.SetFogEnabled(false);
        renderer.SetGodRayIntensity(0.0f);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_PARTICLES")) {
        renderer.SetParticlesEnabled(false);
    }
    if (ReadRTShowcaseDisableFlag("CORTEX_DISABLE_IBL")) {
        renderer.SetIBLEnabled(false);
    }
}
} // namespace

void ApplyHeroVisualBaselineControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetEnvironmentPreset("studio");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.85f, 1.25f);

    renderer.SetExposure(1.2f);
    renderer.SetBloomIntensity(0.3f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.5f);
    renderer.SetCascadeSplitLambda(0.5f);

    renderer.SetTAAEnabled(true);
    renderer.SetFXAAEnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSREnabled(true);

    renderer.SetWaterParams(
        -0.02f,
        0.03f,
        6.0f,
        0.6f,
        1.0f, 0.2f,
        0.015f);

    renderer.SetFogEnabled(true);
}

void ApplyAutoDemoFeatureLock(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetRayTracingEnabled(true);

    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetEnvironmentPreset("studio");
}

void ApplyRTShowcaseSceneControls(Renderer& renderer, bool conservativeMode) {
    SceneCinematicProfile profile = BuildGalleryCinematicProfile(conservativeMode);
    const bool visibleExternalBackground =
        ReadRTShowcaseDisableFlag("CORTEX_RT_SHOWCASE_VISIBLE_EXTERNAL_BACKGROUND");
    profile.environment.ownership = visibleExternalBackground
        ? "authored_visible_gallery_ibl"
        : "scene_local_gallery_background_hidden_external_ibl";
    profile.environment.iblDiffuse =
        ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_IBL_DIFFUSE_INTENSITY", 0.85f, 0.0f, 3.0f);
    profile.environment.iblSpecular =
        ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_IBL_SPECULAR_INTENSITY", 1.25f, 0.0f, 3.0f);
    profile.environment.backgroundVisible = visibleExternalBackground;
    profile.environment.backgroundExposure = visibleExternalBackground
        ? ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_BACKGROUND_EXPOSURE", 1.0f, 0.0f, 4.0f)
        : 0.0f;
    profile.environment.backgroundBlur =
        ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_BACKGROUND_BLUR", 0.55f, 0.0f, 1.0f);
    profile.lighting.shadowBias =
        ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_SHADOW_BIAS", 0.0030f, 0.00001f, 0.05f);
    profile.lighting.shadowPCFRadius =
        ReadRTShowcaseFloatOverride("CORTEX_RT_SHOWCASE_SHADOW_PCF_RADIUS", 3.0f, 0.0f, 12.0f);
    profile.post.renderScale =
        ReadRTShowcaseRenderScaleOverride(conservativeMode ? 0.67f : 0.85f);

    ApplySceneCinematicProfile(renderer, profile);
    ApplyRTShowcaseDiagnosticFeatureOverrides(renderer);
}

void ApplyMaterialLabSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("material_lab_review", "scene_preset", false);
    renderer.SetParticlesEnabled(false);
    renderer.SetEnvironmentPreset("cool_overcast");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.95f, 1.15f);
    renderer.SetBackgroundPresentation(true, 0.9f, 0.25f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.2f);
    renderer.SetCascadeSplitLambda(0.55f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.28f, 0.82f, 0.38f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(1.0f, 0.98f, 0.94f));
    renderer.SetSunIntensity(2.2f);

    renderer.SetRenderScale(0.85f);
    renderer.SetExposure(1.08f);
    renderer.SetBloomIntensity(0.08f);
    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSAOParams(0.24f, 0.035f, 0.24f);
    renderer.SetFogEnabled(false);
    renderer.SetGodRayIntensity(0.0f);
    renderer.SetWaterParams(
        -10.0f,
        0.0f,
        8.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f);
}

void ApplyGlassWaterCourtyardSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("sunset_rim", "scene_preset", false);
    renderer.SetParticlesEnabled(false);
    renderer.SetEnvironmentPreset("sunset_courtyard");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.85f, 0.95f);
    renderer.SetBackgroundPresentation(true, 1.0f, 0.05f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.35f);
    renderer.SetCascadeSplitLambda(0.55f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.42f, 0.68f, 0.28f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(1.0f, 0.74f, 0.46f));
    renderer.SetSunIntensity(3.2f);

    renderer.SetRenderScale(0.85f);
    renderer.SetExposure(1.08f);
    renderer.SetBloomIntensity(0.10f);
    renderer.SetBloomShape(1.05f, 0.45f, 2.0f);
    renderer.SetCinematicPostEnabled(true);
    renderer.SetCinematicPost(0.10f, 0.10f);
    renderer.SetCinematicPostEffects(0.0f, 0.08f);
    renderer.SetToneMapperPreset("filmic_soft");
    renderer.SetColorGrade(0.20f, 0.04f);

    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSAOParams(0.22f, 0.035f, 0.22f);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.016f, 0.0f, 0.52f);
    renderer.SetGodRayIntensity(0.24f);
    renderer.SetRTReflectionsEnabled(true);

    renderer.SetWaterParams(
        -0.02f,
        0.07f,
        7.5f,
        0.75f,
        1.0f,
        0.28f,
        0.035f,
        0.45f);
}

void ApplyLiquidGallerySceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("liquid_gallery", "scene_preset", false);
    renderer.SetParticlesEnabled(true);
    renderer.SetEnvironmentPreset("warm_gallery");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.68f, 0.96f);
    renderer.SetBackgroundPresentation(true, 0.78f, 0.16f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.25f);
    renderer.SetCascadeSplitLambda(0.55f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.25f, 0.82f, 0.32f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(1.0f, 0.88f, 0.70f));
    renderer.SetSunIntensity(2.95f);

    renderer.SetRenderScale(0.85f);
    renderer.SetExposure(0.99f);
    renderer.SetBloomIntensity(0.22f);
    renderer.SetBloomShape(0.85f, 0.50f, 1.80f);
    renderer.SetCinematicPostEnabled(true);
    renderer.SetCinematicPost(0.10f, 0.18f);
    renderer.SetCinematicPostEffects(0.04f, 0.08f);
    renderer.SetToneMapperPreset("filmic_soft");
    renderer.SetColorGrade(0.15f, 0.04f);

    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSAOParams(0.22f, 0.035f, 0.24f);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.010f, 0.0f, 0.48f);
    renderer.SetGodRayIntensity(0.16f);
    renderer.SetRTReflectionsEnabled(true);

    renderer.SetWaterParams(
        0.0f,
        0.075f,
        7.0f,
        0.80f,
        0.9f,
        0.26f,
        0.030f,
        0.70f);
}

void ApplyEffectsShowcaseSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("night_emissive", "scene_preset", false);
    renderer.SetParticlesEnabled(true);
    renderer.SetEnvironmentPreset("night_city");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.55f, 1.25f);
    renderer.SetBackgroundPresentation(true, 1.0f, 0.15f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.3f);
    renderer.SetCascadeSplitLambda(0.52f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.18f, 0.75f, 0.42f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(0.72f, 0.82f, 1.0f));
    renderer.SetSunIntensity(1.25f);

    renderer.SetRenderScale(0.85f);
    renderer.SetExposure(1.18f);
    renderer.SetBloomIntensity(0.34f);
    renderer.SetBloomShape(0.75f, 0.55f, 1.65f);
    renderer.SetCinematicPostEnabled(true);
    renderer.SetCinematicPost(0.18f, 0.34f);
    renderer.SetCinematicPostEffects(0.18f, 0.22f);
    renderer.SetToneMapperPreset("punchy");
    renderer.SetColorGrade(0.08f, 0.24f);

    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSAOParams(0.26f, 0.035f, 0.26f);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.018f, 0.0f, 0.48f);
    renderer.SetGodRayIntensity(0.28f);

    renderer.SetRTReflectionsEnabled(true);
    renderer.SetWaterParams(
        -10.0f,
        0.0f,
        8.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f);
}

void ApplyTemporalValidationSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("temporal_validation_lab", "scene_preset", false);
    renderer.SetSunDirection(glm::normalize(glm::vec3(-0.35f, -0.85f, 0.25f)));
    renderer.SetSunColor(glm::vec3(1.0f, 0.96f, 0.88f));
    renderer.SetSunIntensity(3.2f);
    renderer.SetEnvironmentPreset("studio");
    renderer.SetIBLEnabled(true);
    renderer.SetExposure(1.05f);
    renderer.SetBloomIntensity(0.10f);
    renderer.SetFogEnabled(false);
    renderer.SetGodRayIntensity(0.0f);
    renderer.SetSSAOParams(0.18f, 0.035f, 0.18f);
    renderer.SetWaterParams(
        0.0f,
        0.035f,
        4.5f,
        0.85f,
        0.7f,
        0.2f,
        0.012f);
}

void ApplyCornellSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("cornell_god_rays", "scene_preset", false);
    renderer.SetSunDirection(glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f)));
    renderer.SetSunColor(glm::vec3(1.0f));
    renderer.SetSunIntensity(2.0f);
    renderer.SetEnvironmentPreset("studio");
    renderer.SetIBLEnabled(true);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.03f, 0.0f, 0.55f);
    renderer.SetGodRayIntensity(0.9f);
    renderer.SetWaterParams(
        0.0f,
        0.015f,
        4.0f,
        0.5f,
        1.0f, 0.0f,
        0.01f);
}

void ApplyGodRaysSceneControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("god_rays_volume", "scene_preset", false);
    renderer.SetEnvironmentPreset("studio");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(0.75f, 1.1f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.5f);
    renderer.SetCascadeSplitLambda(0.5f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.45f, 0.75f, 0.15f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(1.0f));
    renderer.SetSunIntensity(4.0f);

    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.045f, 0.0f, 0.65f);
    renderer.SetGodRayIntensity(2.0f);

    renderer.SetWaterParams(
        0.0f,
        0.05f,
        8.0f,
        0.5f,
        1.0f,
        0.2f,
        0.02f,
        0.5f);
}

void ApplyDragonWaterStudioSunControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("dragon_water_studio_sun", "scene_preset", false);
    renderer.SetIBLEnabled(false);
    renderer.SetIBLIntensity(0.0f, 0.0f);
    renderer.SetBackgroundPresentation(false, 1.0f, 0.0f);
    renderer.SetSunDirection(glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f)));
    renderer.SetSunColor(glm::vec3(1.0f));
    renderer.SetSunIntensity(5.0f);
    // The dragon/pool closeups put thin hero geometry over a low reflective
    // floor at grazing view angles. A slightly larger bias keeps the shadow
    // pattern from crawling during mouse-look without erasing the scene's
    // broad contact shadows.
    renderer.SetShadowBias(0.0015f);
    renderer.SetShadowPCFRadius(1.5f);
}

void ApplyOutdoorWorldSceneControls(Renderer& renderer,
                                    const glm::vec3& sunDirection,
                                    const glm::vec3& sunColor,
                                    float sunIntensity) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("outdoor_world_sun", "scene_preset", false);
    renderer.SetIBLEnabled(false);
    renderer.SetSSREnabled(true);
    renderer.SetRTReflectionsEnabled(true);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.001f, 0.0f, 0.2f);
    renderer.SetExposure(1.0f);
    renderer.SetShadowsEnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSunDirection(sunDirection);
    renderer.SetSunColor(sunColor);
    renderer.SetSunIntensity(sunIntensity);
}

void ApplyEditorModeBaseControls(Renderer& renderer) {
    renderer.SetVisibilityBufferEnabled(true);
    renderer.SetLightingRigContract("editor_time_of_day", "editor", false);
    renderer.SetIBLEnabled(false);
    renderer.SetFogEnabled(true);
}

void ApplyEditorTimeOfDayControls(Renderer& renderer,
                                  const glm::vec3& sunDirection,
                                  const glm::vec3& sunColor,
                                  float sunIntensity) {
    renderer.SetSunDirection(sunDirection);
    renderer.SetSunColor(sunColor);
    renderer.SetSunIntensity(sunIntensity);
}

} // namespace Cortex::Graphics
