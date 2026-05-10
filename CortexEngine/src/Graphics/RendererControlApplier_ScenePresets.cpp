#include "RendererControlApplier.h"

#include "Renderer.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace Cortex::Graphics {

void ApplyHeroVisualBaselineControls(Renderer& renderer) {
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
    if (renderer.GetRayTracingState().supported) {
        renderer.SetRayTracingEnabled(true);
    }

    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetEnvironmentPreset("studio");
}

void ApplyRTShowcaseSceneControls(Renderer& renderer, bool conservativeMode) {
    renderer.SetLightingRigContract("rt_showcase_gallery", "scene_preset", conservativeMode);
    renderer.SetParticlesEnabled(true);
    renderer.SetEnvironmentPreset("studio");
    renderer.SetIBLEnabled(true);
    renderer.SetIBLIntensity(1.05f, 0.78f);

    renderer.SetShadowsEnabled(true);
    renderer.SetShadowBias(0.0005f);
    renderer.SetShadowPCFRadius(1.5f);
    renderer.SetCascadeSplitLambda(0.5f);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.35f, 0.85f, 0.25f));
    renderer.SetSunDirection(sunDir);
    renderer.SetSunColor(glm::vec3(1.0f));
    renderer.SetSunIntensity(2.85f);

    renderer.SetWaterParams(
        0.0f,
        0.15f,
        10.0f,
        1.0f,
        1.0f,
        0.25f,
        0.08f,
        0.6f);

    if (!conservativeMode) {
        renderer.SetRenderScale(0.85f);
        renderer.SetExposure(1.12f);
        renderer.SetBloomIntensity(0.12f);

        renderer.SetFXAAEnabled(true);
        renderer.SetTAAEnabled(true);
        renderer.SetSSREnabled(true);
        renderer.SetSSAOEnabled(true);
        renderer.SetSSAOParams(0.20f, 0.04f, 0.22f);

        renderer.SetFogEnabled(true);
        renderer.SetFogParams(0.012f, 0.0f, 0.55f);
        renderer.SetGodRayIntensity(0.42f);
        return;
    }

    renderer.SetRenderScale(0.67f);
    renderer.SetExposure(1.0f);
    renderer.SetBloomIntensity(0.11f);

    renderer.SetFXAAEnabled(true);
    renderer.SetTAAEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSAOParams(0.20f, 0.04f, 0.20f);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.01f, 0.0f, 0.6f);
    renderer.SetGodRayIntensity(0.36f);
    renderer.SetShadowsEnabled(true);
    renderer.SetIBLEnabled(true);
}

void ApplyMaterialLabSceneControls(Renderer& renderer) {
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

void ApplyEffectsShowcaseSceneControls(Renderer& renderer) {
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
    renderer.SetCinematicPost(0.18f, 0.34f);
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
    renderer.SetLightingRigContract("dragon_water_studio_sun", "scene_preset", false);
    renderer.SetSunDirection(glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f)));
    renderer.SetSunColor(glm::vec3(1.0f));
    renderer.SetSunIntensity(5.0f);
}

void ApplyOutdoorWorldSceneControls(Renderer& renderer,
                                    const glm::vec3& sunDirection,
                                    const glm::vec3& sunColor,
                                    float sunIntensity) {
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
