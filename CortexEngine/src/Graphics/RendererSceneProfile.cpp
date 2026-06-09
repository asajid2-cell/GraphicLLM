#include "Graphics/RendererSceneProfile.h"

#include "Graphics/Renderer.h"

#include <algorithm>
#include <glm/geometric.hpp>
#include <utility>

namespace Cortex::Graphics {

namespace {

glm::vec3 SafeDirection(const glm::vec3& v) {
    const float len2 = glm::dot(v, v);
    if (len2 <= 1e-6f) {
        return glm::normalize(glm::vec3(-0.38f, 0.72f, 0.28f));
    }
    return glm::normalize(v);
}

SceneCinematicProfile EnclosedInteriorBase(std::string_view family) {
    SceneCinematicProfile p{};
    p.id = std::string(family) + "_cinematic_v1";
    p.family = std::string(family);
    p.enclosedScene = true;
    p.visibilityBufferEnabled = true;
    p.particlesEnabled = false;

    p.environment.preset = "neutral_procedural";
    p.environment.ownership = "scene_local_neutral";
    p.environment.iblEnabled = false;
    p.environment.iblDiffuse = 0.0f;
    p.environment.iblSpecular = 0.0f;
    p.environment.backgroundVisible = false;
    p.environment.backgroundExposure = 0.0f;
    p.environment.backgroundBlur = 1.0f;

    p.lighting.rigId = "scene_local_enclosed_room";
    p.lighting.source = "scene_cinematic_profile";
    p.lighting.ambientColor = glm::vec3(0.045f, 0.046f, 0.050f);
    p.lighting.ambientIntensity = 0.95f;
    p.lighting.fogEnabled = true;
    p.lighting.fogDensity = 0.006f;
    p.lighting.fogFalloff = 0.62f;
    p.lighting.godRayIntensity = 0.0f;

    p.reflections.ownership = "local_room_no_visible_hdri";
    p.reflections.localProbeRigId = "single_room_scene_local_probe";
    p.reflections.localProbeEnabled = true;
    p.reflections.localProbeDiffuse = 0.24f;
    p.reflections.localProbeSpecular = 0.20f;
    p.reflections.ssrEnabled = true;
    p.reflections.ssrStrength = 0.68f;
    p.reflections.rayTracingEnabled = true;
    p.reflections.rtReflectionsEnabled = true;
    p.reflections.rtGIEnabled = true;
    p.reflections.rtReflectionDenoiseAlpha = 0.24f;
    p.reflections.rtReflectionComposition = 0.68f;
    p.reflections.rtGIRayDistance = 14.0f;

    p.temporal.taaEnabled = true;
    p.temporal.policyId = "stable_interior_reprojection";
    p.temporal.fxaaEnabled = true;
    p.temporal.ssaoEnabled = true;
    p.temporal.ssaoRadius = 0.22f;
    p.temporal.ssaoBias = 0.035f;
    p.temporal.ssaoIntensity = 0.22f;

    p.post.renderScale = 0.85f;
    p.post.policyId = "cinematic_interior_soft";
    p.post.exposure = 1.12f;
    p.post.bloomIntensity = 0.14f;
    p.post.bloomThreshold = 0.95f;
    p.post.bloomSoftKnee = 0.52f;
    p.post.bloomMaxContribution = 3.0f;
    p.post.cinematicEnabled = true;
    p.post.vignette = 0.08f;
    p.post.lensDirt = 0.04f;
    p.post.toneMapperPreset = "filmic_soft";
    p.post.contrast = 1.02f;
    p.post.saturation = 1.04f;

    p.material.worldPaletteId = "scene_local_enclosed_room";
    p.material.lightingScriptId = "scene_local_enclosed_room";

    SceneReflectionProbeProfile roomProbe{};
    roomProbe.id = "SceneLocal_RoomProbe";
    roomProbe.center = glm::vec3(0.0f, 1.45f, 0.0f);
    roomProbe.extents = glm::vec3(4.25f, 2.25f, 4.25f);
    roomProbe.blendDistance = 1.75f;
    p.reflectionProbes.push_back(roomProbe);
    return p;
}

void AddPointFixture(SceneCinematicProfile& profile,
                     std::string id,
                     std::string semanticClass,
                     const glm::vec3& position,
                     const glm::vec3& color,
                     float intensity,
                     float range) {
    SceneLightFixtureProfile fixture{};
    fixture.id = std::move(id);
    fixture.type = "point";
    fixture.semanticClass = std::move(semanticClass);
    fixture.position = position;
    fixture.color = color;
    fixture.intensity = intensity;
    fixture.range = range;
    fixture.castsShadows = false;
    profile.lightFixtures.push_back(std::move(fixture));
}

void AddSpotFixture(SceneCinematicProfile& profile,
                    std::string id,
                    std::string semanticClass,
                    const glm::vec3& position,
                    const glm::vec3& target,
                    const glm::vec3& color,
                    float intensity,
                    float range,
                    bool castsShadows) {
    SceneLightFixtureProfile fixture{};
    fixture.id = std::move(id);
    fixture.type = "spot";
    fixture.semanticClass = std::move(semanticClass);
    fixture.position = position;
    fixture.target = target;
    fixture.color = color;
    fixture.intensity = intensity;
    fixture.range = range;
    fixture.innerConeDegrees = 26.0f;
    fixture.outerConeDegrees = 48.0f;
    fixture.castsShadows = castsShadows;
    profile.lightFixtures.push_back(std::move(fixture));
}

void AddAreaFixture(SceneCinematicProfile& profile,
                    std::string id,
                    std::string semanticClass,
                    const glm::vec3& position,
                    const glm::vec3& target,
                    const glm::vec2& areaSize,
                    const glm::vec3& color,
                    float intensity,
                    float range,
                    bool twoSided = false) {
    SceneLightFixtureProfile fixture{};
    fixture.id = std::move(id);
    fixture.type = "area_rect";
    fixture.semanticClass = std::move(semanticClass);
    fixture.position = position;
    fixture.target = target;
    fixture.areaSize = areaSize;
    fixture.color = color;
    fixture.intensity = intensity;
    fixture.range = range;
    fixture.twoSided = twoSided;
    fixture.castsShadows = false;
    profile.lightFixtures.push_back(std::move(fixture));
}

bool Contains(std::string_view text, std::string_view token) {
    return text.find(token) != std::string_view::npos;
}

void ApplyLightingBalance(SceneCinematicProfile& profile) {
    SceneLightingBalanceProfile balance{};
    balance.policyId = "scene_local_lighting_balance_v1";
    balance.active = true;

    const std::string_view family(profile.family);
    const std::string_view palette(profile.material.worldPaletteId);
    const bool highKeyPublicInterior =
        Contains(family, "gym") ||
        Contains(family, "classroom") ||
        Contains(family, "stadium") ||
        Contains(palette, "gym") ||
        Contains(palette, "classroom") ||
        Contains(palette, "stadium");
    const bool officeLike = Contains(family, "office") || Contains(palette, "office");
    const bool kitchenLike = Contains(family, "kitchen") || Contains(palette, "kitchen");
    const bool concertLike =
        Contains(family, "concert") ||
        Contains(family, "red_light") ||
        Contains(palette, "concert") ||
        Contains(palette, "red_light");
    const bool galleryLike = profile.family == "rt_showcase_gallery";

    if (highKeyPublicInterior) {
        balance.sunScale = 0.58f;
        balance.ambientScale = 0.70f;
        balance.localFixtureScale = 0.52f;
        balance.localProbeDiffuseScale = 0.65f;
        balance.localProbeSpecularScale = 0.85f;
        balance.exposureScale = 0.72f;
        balance.ssaoScale = 1.55f;
        profile.post.bloomIntensity *= 0.78f;
        profile.post.contrast = std::max(profile.post.contrast, 1.08f);
        profile.post.vignette = std::max(profile.post.vignette, 0.12f);
        profile.lighting.shadowPCFRadius = std::max(profile.lighting.shadowPCFRadius, 1.9f);
    } else if (officeLike) {
        balance.sunScale = 0.78f;
        balance.ambientScale = 0.78f;
        balance.localFixtureScale = 0.72f;
        balance.localProbeDiffuseScale = 0.80f;
        balance.localProbeSpecularScale = 0.90f;
        balance.exposureScale = 0.86f;
        balance.ssaoScale = 1.30f;
        profile.post.contrast = std::max(profile.post.contrast, 1.06f);
        profile.post.vignette = std::max(profile.post.vignette, 0.10f);
    } else if (kitchenLike) {
        balance.sunScale = 0.88f;
        balance.ambientScale = 0.86f;
        balance.localFixtureScale = 0.82f;
        balance.localProbeDiffuseScale = 0.84f;
        balance.localProbeSpecularScale = 0.92f;
        balance.exposureScale = 0.88f;
        balance.ssaoScale = 1.25f;
        profile.post.contrast = std::max(profile.post.contrast, 1.05f);
        profile.post.vignette = std::max(profile.post.vignette, 0.10f);
    } else if (galleryLike) {
        balance.sunScale = 0.86f;
        balance.ambientScale = 0.95f;
        balance.localFixtureScale = 0.88f;
        balance.localProbeDiffuseScale = 0.95f;
        balance.localProbeSpecularScale = 0.95f;
        balance.exposureScale = 0.90f;
        balance.ssaoScale = 1.10f;
    } else if (concertLike) {
        balance.sunScale = 0.94f;
        balance.ambientScale = 0.88f;
        balance.localFixtureScale = 0.90f;
        balance.localProbeDiffuseScale = 0.90f;
        balance.localProbeSpecularScale = 0.98f;
        balance.exposureScale = 0.92f;
        balance.ssaoScale = 1.05f;
    }

    profile.lighting.sunIntensity *= balance.sunScale;
    profile.lighting.ambientIntensity *= balance.ambientScale;
    profile.reflections.localProbeDiffuse *= balance.localProbeDiffuseScale;
    profile.reflections.localProbeSpecular *= balance.localProbeSpecularScale;
    profile.post.exposure *= balance.exposureScale;
    profile.temporal.ssaoIntensity *= balance.ssaoScale;
    for (SceneLightFixtureProfile& fixture : profile.lightFixtures) {
        fixture.intensity *= balance.localFixtureScale;
    }
    profile.lightingBalance = balance;
}

} // namespace

SceneCinematicProfile BuildSceneLocalCinematicProfile(std::string_view sceneFamily) {
    SceneCinematicProfile p = EnclosedInteriorBase(sceneFamily);
    bool recognizedFamily = false;

    if (sceneFamily == "home_kitchen_lantern") {
        recognizedFamily = true;
        p.id = "kitchen_morning_warm_scene_local_v1";
        p.lighting.rigId = "home_kitchen_enclosed_warm";
        p.lighting.sunDirection = glm::vec3(-0.55f, 0.60f, 0.22f);
        p.lighting.sunColor = glm::vec3(1.0f, 0.78f, 0.55f);
        p.lighting.sunIntensity = 1.45f;
        p.lighting.ambientColor = glm::vec3(0.050f, 0.036f, 0.026f);
        p.lighting.ambientIntensity = 0.95f;
        p.lighting.fogDensity = 0.004f;
        p.reflections.ssrStrength = 0.74f;
        p.reflections.localProbeRigId = "kitchen_room_probe_warm";
        p.reflections.localProbeDiffuse = 0.26f;
        p.reflections.localProbeSpecular = 0.24f;
        p.reflectionProbes[0].id = "Kitchen_LocalProbe_Main";
        p.reflectionProbes[0].center = glm::vec3(0.0f, 1.25f, 0.0f);
        p.reflectionProbes[0].extents = glm::vec3(3.8f, 1.9f, 3.6f);
        p.post.exposure = 1.18f;
        p.post.bloomIntensity = 0.16f;
        p.post.warm = 0.18f;
        p.post.cool = 0.02f;
        p.post.saturation = 1.05f;
        p.material.worldPaletteId = "home_kitchen_enclosed_warm";
        p.material.lightingScriptId = "home_kitchen_warm";
        AddAreaFixture(p, "ProfileLight_Kitchen_WindowFill", "window_softbox", glm::vec3(-1.35f, 1.65f, -0.75f), glm::vec3(0.0f, 1.05f, 0.15f), glm::vec2(1.65f, 0.80f), glm::vec3(0.72f, 0.86f, 1.0f), 5.0f, 4.5f);
        AddAreaFixture(p, "ProfileLight_Kitchen_UnderCabinetWarm", "under_cabinet_strip", glm::vec3(0.75f, 1.05f, 1.28f), glm::vec3(0.35f, 0.70f, 0.55f), glm::vec2(1.35f, 0.18f), glm::vec3(1.0f, 0.62f, 0.32f), 4.8f, 3.2f);
        AddPointFixture(p, "ProfileLight_Kitchen_TableLanternBounce", "warm_practical", glm::vec3(-0.55f, 0.92f, -0.55f), glm::vec3(1.0f, 0.52f, 0.24f), 5.6f, 3.4f);
    } else if (sceneFamily == "home_office_evening") {
        recognizedFamily = true;
        p.id = "office_evening_scene_local_v1";
        p.lighting.rigId = "home_office_enclosed_evening";
        p.lighting.sunDirection = glm::vec3(-0.32f, 0.66f, 0.42f);
        p.lighting.sunColor = glm::vec3(0.72f, 0.84f, 1.0f);
        p.lighting.sunIntensity = 0.95f;
        p.lighting.ambientColor = glm::vec3(0.026f, 0.032f, 0.046f);
        p.lighting.ambientIntensity = 0.85f;
        p.post.exposure = 1.02f;
        p.post.bloomIntensity = 0.12f;
        p.post.warm = 0.02f;
        p.post.cool = 0.18f;
        p.post.contrast = 1.04f;
        p.reflections.localProbeRigId = "office_room_probe_evening";
        p.reflections.localProbeDiffuse = 0.20f;
        p.reflections.localProbeSpecular = 0.22f;
        p.reflectionProbes[0].id = "Office_LocalProbe_Main";
        p.reflectionProbes[0].center = glm::vec3(0.0f, 1.30f, 0.0f);
        p.reflectionProbes[0].extents = glm::vec3(3.8f, 2.0f, 3.6f);
        p.material.worldPaletteId = "home_office_enclosed_evening";
        p.material.lightingScriptId = "interior_evening";
        AddAreaFixture(p, "ProfileLight_Office_MonitorGlow", "screen_panel", glm::vec3(0.15f, 1.10f, 0.72f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.95f, 0.55f), glm::vec3(0.20f, 0.44f, 1.0f), 5.4f, 3.8f);
        AddPointFixture(p, "ProfileLight_Office_WarmDeskLamp", "desk_practical", glm::vec3(-0.95f, 1.15f, 0.12f), glm::vec3(1.0f, 0.62f, 0.34f), 5.8f, 3.4f);
    } else if (sceneFamily == "school_classroom_day") {
        recognizedFamily = true;
        p.id = "classroom_day_scene_local_v1";
        p.lighting.rigId = "school_classroom_enclosed_day";
        p.lighting.sunDirection = glm::vec3(-0.45f, 0.70f, 0.25f);
        p.lighting.sunColor = glm::vec3(1.0f, 0.92f, 0.78f);
        p.lighting.sunIntensity = 2.05f;
        p.lighting.ambientColor = glm::vec3(0.075f, 0.080f, 0.084f);
        p.lighting.ambientIntensity = 1.05f;
        p.post.exposure = 1.24f;
        p.post.bloomIntensity = 0.08f;
        p.post.warm = 0.08f;
        p.post.cool = 0.04f;
        p.reflections.localProbeRigId = "classroom_day_probe_grid";
        p.reflections.localProbeDiffuse = 0.28f;
        p.reflections.localProbeSpecular = 0.18f;
        p.reflectionProbes[0].id = "Classroom_LocalProbe_Main";
        p.reflectionProbes[0].extents = glm::vec3(5.0f, 2.4f, 4.4f);
        p.material.worldPaletteId = "school_classroom_enclosed_day";
        p.material.lightingScriptId = "classroom_day";
        AddAreaFixture(p, "ProfileLight_Classroom_WindowFill", "window_softbox", glm::vec3(-1.65f, 1.85f, -0.35f), glm::vec3(0.0f, 1.20f, 0.10f), glm::vec2(2.2f, 1.05f), glm::vec3(0.90f, 0.96f, 1.0f), 10.0f, 6.0f);
        AddAreaFixture(p, "ProfileLight_Classroom_CeilingFill", "ceiling_panel", glm::vec3(0.65f, 2.65f, 0.65f), glm::vec3(0.55f, 0.7f, 0.55f), glm::vec2(1.55f, 0.75f), glm::vec3(1.0f, 0.92f, 0.78f), 8.0f, 5.5f);
    } else if (sceneFamily == "basketball_gym_day") {
        recognizedFamily = true;
        p.id = "basketball_gym_bright_scene_local_v1";
        p.lighting.rigId = "basketball_gym_enclosed_day";
        p.lighting.sunDirection = glm::vec3(-0.40f, 0.72f, 0.18f);
        p.lighting.sunColor = glm::vec3(1.0f, 0.90f, 0.76f);
        p.lighting.sunIntensity = 1.45f;
        p.lighting.ambientColor = glm::vec3(0.070f, 0.078f, 0.082f);
        p.lighting.ambientIntensity = 1.0f;
        p.reflections.ssrMaxDistance = 65.0f;
        p.reflections.rtGIRayDistance = 24.0f;
        p.reflections.localProbeRigId = "gym_highbay_probe";
        p.reflections.localProbeDiffuse = 0.30f;
        p.reflections.localProbeSpecular = 0.20f;
        p.reflectionProbes[0].id = "Gym_LocalProbe_Court";
        p.reflectionProbes[0].center = glm::vec3(0.0f, 1.85f, 0.0f);
        p.reflectionProbes[0].extents = glm::vec3(6.4f, 3.0f, 5.4f);
        p.reflectionProbes[0].blendDistance = 2.4f;
        p.post.exposure = 1.12f;
        p.post.bloomIntensity = 0.08f;
        p.post.contrast = 1.10f;
        p.post.saturation = 1.08f;
        p.post.warm = 0.06f;
        p.post.cool = 0.10f;
        p.material.worldPaletteId = "basketball_gym_enclosed_day";
        p.material.lightingScriptId = "gym_day";
        AddAreaFixture(p, "ProfileLight_Gym_OverheadLeft", "high_bay_panel", glm::vec3(-1.95f, 2.75f, -0.85f), glm::vec3(-1.10f, 0.65f, -0.10f), glm::vec2(1.20f, 0.55f), glm::vec3(1.0f, 0.92f, 0.80f), 11.0f, 5.5f);
        AddAreaFixture(p, "ProfileLight_Gym_OverheadCenter", "high_bay_panel", glm::vec3(0.0f, 2.85f, 0.35f), glm::vec3(0.0f, 0.65f, 0.35f), glm::vec2(1.40f, 0.65f), glm::vec3(0.92f, 0.98f, 1.0f), 12.5f, 6.0f);
        AddAreaFixture(p, "ProfileLight_Gym_OverheadRight", "high_bay_panel", glm::vec3(1.95f, 2.75f, 1.30f), glm::vec3(1.10f, 0.65f, 0.25f), glm::vec2(1.20f, 0.55f), glm::vec3(1.0f, 0.92f, 0.80f), 10.0f, 5.5f);
        AddSpotFixture(p, "ProfileLight_Gym_BackboardWash", "backboard_spot", glm::vec3(-1.8f, 2.35f, -1.25f), glm::vec3(0.0f, 0.72f, 1.05f), glm::vec3(1.0f, 0.86f, 0.68f), 16.0f, 5.0f, false);
    } else if (sceneFamily == "neon_streamer_concert") {
        recognizedFamily = true;
        p.id = "neon_concert_auditorium_scene_local_v1";
        p.particlesEnabled = true;
        p.lighting.rigId = "neon_concert_enclosed_auditorium";
        p.lighting.sunDirection = glm::vec3(-0.08f, 0.82f, 0.20f);
        p.lighting.sunColor = glm::vec3(0.42f, 0.60f, 1.0f);
        p.lighting.sunIntensity = 0.74f;
        p.lighting.ambientColor = glm::vec3(0.032f, 0.038f, 0.060f);
        p.lighting.ambientIntensity = 0.96f;
        p.lighting.fogDensity = 0.018f;
        p.lighting.fogFalloff = 0.48f;
        p.lighting.godRayIntensity = 0.18f;
        p.reflections.ssrStrength = 0.84f;
        p.reflections.rtReflectionComposition = 0.80f;
        p.reflections.localProbeRigId = "concert_auditorium_neon_probe";
        p.reflections.localProbeDiffuse = 0.26f;
        p.reflections.localProbeSpecular = 0.30f;
        p.reflectionProbes[0].id = "Concert_LocalProbe_Stage";
        p.reflectionProbes[0].center = glm::vec3(0.0f, 1.85f, 1.4f);
        p.reflectionProbes[0].extents = glm::vec3(7.2f, 3.4f, 6.6f);
        p.reflectionProbes[0].blendDistance = 2.8f;
        p.post.exposure = 1.78f;
        p.post.bloomIntensity = 0.42f;
        p.post.bloomThreshold = 0.78f;
        p.post.bloomSoftKnee = 0.58f;
        p.post.bloomMaxContribution = 3.8f;
        p.post.vignette = 0.18f;
        p.post.lensDirt = 0.22f;
        p.post.toneMapperPreset = "punchy";
        p.post.warm = 0.02f;
        p.post.cool = 0.28f;
        p.post.saturation = 1.12f;
        p.material.worldPaletteId = "neon_concert_enclosed_auditorium";
        p.material.lightingScriptId = "neon_concert";
        AddAreaFixture(p, "ProfileLight_Concert_CenterScreenBounce", "screen_panel", glm::vec3(0.0f, 2.15f, 3.55f), glm::vec3(0.0f, 1.2f, 1.15f), glm::vec2(2.40f, 1.10f), glm::vec3(0.18f, 0.62f, 1.0f), 18.0f, 7.0f);
        AddAreaFixture(p, "ProfileLight_Concert_LeftNeonBounce", "neon_strip_magenta", glm::vec3(-2.7f, 1.25f, 2.55f), glm::vec3(-1.15f, 1.1f, 1.0f), glm::vec2(1.60f, 0.22f), glm::vec3(0.95f, 0.16f, 1.0f), 9.0f, 5.0f, true);
        AddAreaFixture(p, "ProfileLight_Concert_RightNeonBounce", "neon_strip_green", glm::vec3(2.7f, 1.25f, 2.55f), glm::vec3(1.15f, 1.1f, 1.0f), glm::vec2(1.60f, 0.22f), glm::vec3(0.20f, 1.0f, 0.38f), 8.5f, 5.0f, true);
        AddAreaFixture(p, "ProfileLight_Concert_AudienceBlueFill", "audience_soft_fill", glm::vec3(0.0f, 2.15f, -1.35f), glm::vec3(0.0f, 0.70f, -1.25f), glm::vec2(3.20f, 0.80f), glm::vec3(0.18f, 0.36f, 0.80f), 6.4f, 8.5f);
        AddPointFixture(p, "ProfileLight_Concert_SubjectWarmPractical", "warm_practical", glm::vec3(-0.25f, 1.15f, 2.45f), glm::vec3(1.0f, 0.46f, 0.18f), 8.0f, 3.4f);
        AddAreaFixture(p, "ProfileLight_Concert_LeftWallPractical", "wall_strip_warm", glm::vec3(-5.55f, 2.15f, 0.95f), glm::vec3(-3.2f, 1.4f, 0.55f), glm::vec2(1.20f, 0.36f), glm::vec3(1.0f, 0.38f, 0.12f), 6.4f, 4.2f);
        AddAreaFixture(p, "ProfileLight_Concert_RightWallPractical", "wall_strip_warm", glm::vec3(5.55f, 2.15f, 0.95f), glm::vec3(3.2f, 1.4f, 0.55f), glm::vec2(1.20f, 0.36f), glm::vec3(1.0f, 0.38f, 0.12f), 6.4f, 4.2f);
        AddPointFixture(p, "ProfileLight_Concert_LeftAudienceWarmPool", "audience_floor_pool", glm::vec3(-3.0f, 0.86f, -1.95f), glm::vec3(0.95f, 0.20f, 0.08f), 5.4f, 4.0f);
        AddPointFixture(p, "ProfileLight_Concert_RightAudienceCoolPool", "audience_floor_pool", glm::vec3(3.0f, 0.86f, -1.95f), glm::vec3(0.08f, 0.42f, 1.0f), 5.4f, 4.0f);
        AddAreaFixture(p, "ProfileLight_Concert_BackPanelWarmLift", "back_panel_lift", glm::vec3(0.0f, 2.65f, 4.15f), glm::vec3(0.0f, 1.3f, 2.3f), glm::vec2(2.20f, 0.70f), glm::vec3(1.0f, 0.30f, 0.10f), 5.2f, 4.6f);
        AddPointFixture(p, "ProfileLight_Concert_FrontAisleWarmFill", "aisle_fill", glm::vec3(-2.85f, 1.18f, -3.15f), glm::vec3(1.0f, 0.32f, 0.10f), 7.2f, 5.4f);
        AddPointFixture(p, "ProfileLight_Concert_FrontAisleCoolFill", "aisle_fill", glm::vec3(2.85f, 1.18f, -3.15f), glm::vec3(0.08f, 0.48f, 1.0f), 7.6f, 5.4f);
        AddAreaFixture(p, "ProfileLight_Concert_HighRoomBlueLift", "overhead_soft_fill", glm::vec3(0.0f, 3.10f, -0.85f), glm::vec3(0.0f, 1.0f, 0.10f), glm::vec2(3.0f, 1.1f), glm::vec3(0.14f, 0.30f, 0.90f), 6.8f, 8.8f);
        AddPointFixture(p, "ProfileLight_Concert_RearWarmBalconyLift", "balcony_lift", glm::vec3(-3.65f, 2.20f, 0.18f), glm::vec3(1.0f, 0.28f, 0.10f), 5.6f, 5.8f);
        AddPointFixture(p, "ProfileLight_Concert_RearCoolBalconyLift", "balcony_lift", glm::vec3(3.65f, 2.20f, 0.18f), glm::vec3(0.06f, 0.42f, 1.0f), 5.9f, 5.8f);
        AddSpotFixture(p, "ProfileLight_Concert_StageWashA", "stage_spot_wash", glm::vec3(-2.6f, 4.0f, 0.55f), glm::vec3(-0.75f, 0.55f, 3.25f), glm::vec3(0.55f, 0.72f, 1.0f), 38.0f, 8.0f, false);
        AddSpotFixture(p, "ProfileLight_Concert_StageWashB", "stage_spot_wash", glm::vec3(2.6f, 4.0f, 0.55f), glm::vec3(0.75f, 0.55f, 3.25f), glm::vec3(0.75f, 0.55f, 1.0f), 34.0f, 8.0f, false);
    } else if (sceneFamily == "red_light_room") {
        recognizedFamily = true;
        p.id = "red_room_moody_scene_local_v1";
        p.lighting.rigId = "red_light_room_enclosed_mood";
        p.lighting.sunDirection = glm::vec3(-0.18f, 0.55f, 0.38f);
        p.lighting.sunColor = glm::vec3(1.0f, 0.16f, 0.12f);
        p.lighting.sunIntensity = 0.18f;
        p.lighting.ambientColor = glm::vec3(0.035f, 0.004f, 0.006f);
        p.lighting.ambientIntensity = 0.55f;
        p.lighting.fogDensity = 0.020f;
        p.reflections.ssrStrength = 0.78f;
        p.reflections.localProbeRigId = "red_room_moody_probe";
        p.reflections.localProbeDiffuse = 0.18f;
        p.reflections.localProbeSpecular = 0.26f;
        p.reflectionProbes[0].id = "RedRoom_LocalProbe_Main";
        p.reflectionProbes[0].extents = glm::vec3(3.6f, 1.8f, 3.4f);
        p.post.exposure = 1.06f;
        p.post.bloomIntensity = 0.42f;
        p.post.bloomThreshold = 0.72f;
        p.post.vignette = 0.22f;
        p.post.warm = 0.35f;
        p.post.cool = 0.0f;
        p.post.saturation = 1.10f;
        p.material.worldPaletteId = "red_light_room_enclosed_mood";
        p.material.lightingScriptId = "red_room";
        AddAreaFixture(p, "ProfileLight_RedRoom_SofaGlow", "red_neon_panel", glm::vec3(0.0f, 0.95f, 0.62f), glm::vec3(0.0f, 0.8f, -0.10f), glm::vec2(1.8f, 0.65f), glm::vec3(1.0f, 0.08f, 0.045f), 8.0f, 4.2f, true);
        AddPointFixture(p, "ProfileLight_RedRoom_TableCandleBounce", "candle_practical", glm::vec3(-0.20f, 0.72f, 0.16f), glm::vec3(1.0f, 0.18f, 0.08f), 4.6f, 2.8f);
        AddAreaFixture(p, "ProfileLight_RedRoom_CoolEdge", "cool_edge_softbox", glm::vec3(1.35f, 1.25f, -0.65f), glm::vec3(0.25f, 0.85f, 0.10f), glm::vec2(0.75f, 0.45f), glm::vec3(0.20f, 0.24f, 0.42f), 2.5f, 3.2f);
    } else if (sceneFamily == "stadium_night_match") {
        recognizedFamily = true;
        p.id = "stadium_night_scene_local_v1";
        p.enclosedScene = false;
        p.lighting.rigId = "stadium_night_enclosed";
        p.lighting.sunDirection = glm::vec3(-0.18f, 0.92f, 0.08f);
        p.lighting.sunColor = glm::vec3(0.78f, 0.88f, 1.0f);
        p.lighting.sunIntensity = 0.28f;
        p.lighting.ambientColor = glm::vec3(0.075f, 0.086f, 0.104f);
        p.lighting.ambientIntensity = 0.42f;
        p.lighting.fogDensity = 0.014f;
        p.reflections.ssrMaxDistance = 90.0f;
        p.reflections.rtGIRayDistance = 36.0f;
        p.reflections.localProbeRigId = "stadium_open_probe";
        p.reflections.localProbeDiffuse = 0.18f;
        p.reflections.localProbeSpecular = 0.18f;
        p.reflectionProbes[0].id = "Stadium_LocalProbe_Field";
        p.reflectionProbes[0].center = glm::vec3(0.0f, 2.2f, 0.0f);
        p.reflectionProbes[0].extents = glm::vec3(12.0f, 4.0f, 10.0f);
        p.reflectionProbes[0].blendDistance = 3.0f;
        p.post.exposure = 0.78f;
        p.post.bloomIntensity = 0.14f;
        p.post.cool = 0.18f;
        p.material.worldPaletteId = "stadium_night_enclosed";
        p.material.lightingScriptId = "stadium_night";
        AddAreaFixture(p, "ProfileLight_Stadium_LeftFloodBank", "stadium_flood_bank", glm::vec3(-6.0f, 4.2f, -2.5f), glm::vec3(-1.2f, 0.55f, 0.0f), glm::vec2(2.8f, 0.9f), glm::vec3(0.82f, 0.90f, 1.0f), 7.5f, 12.0f);
        AddAreaFixture(p, "ProfileLight_Stadium_RightFloodBank", "stadium_flood_bank", glm::vec3(6.0f, 4.2f, -2.5f), glm::vec3(1.2f, 0.55f, 0.0f), glm::vec2(2.8f, 0.9f), glm::vec3(0.82f, 0.90f, 1.0f), 7.5f, 12.0f);
        AddSpotFixture(p, "ProfileLight_Stadium_FieldWash", "field_spot_wash", glm::vec3(0.0f, 5.0f, -5.5f), glm::vec3(0.0f, 0.55f, 0.0f), glm::vec3(1.0f, 0.92f, 0.78f), 9.5f, 16.0f, false);
    }

    if (!recognizedFamily) {
        p.id = "model_authored_open_fallback_scene_local_v1";
        p.enclosedScene = false;
        p.environment.preset = "studio";
        p.environment.ownership = "hidden_external_ibl_open_scene";
        p.environment.iblEnabled = true;
        p.environment.iblDiffuse = 0.42f;
        p.environment.iblSpecular = 0.62f;
        p.environment.backgroundVisible = false;
        p.environment.backgroundExposure = 0.72f;
        p.environment.backgroundBlur = 0.35f;
        p.reflections.localProbeEnabled = false;
        p.reflections.localProbeRigId = "none";
        p.reflections.localProbeDiffuse = 0.0f;
        p.reflections.localProbeSpecular = 0.0f;
        p.reflectionProbes.clear();
        p.lighting.rigId = "model_authored_open_scene";
        p.lighting.sunDirection = glm::vec3(-0.38f, 0.72f, 0.28f);
        p.lighting.sunColor = glm::vec3(1.0f, 0.84f, 0.62f);
        p.lighting.sunIntensity = 1.75f;
        p.lighting.ambientColor = glm::vec3(0.040f);
        p.lighting.ambientIntensity = 1.0f;
        p.material.worldPaletteId = "model_authored_open_scene";
        p.material.lightingScriptId = "model_authored_open_scene";
        p.lightFixtures.clear();
    }

    ApplyLightingBalance(p);
    return p;
}

SceneCinematicProfile BuildGalleryCinematicProfile(bool conservativeMode) {
    SceneCinematicProfile p{};
    p.id = conservativeMode ? "gallery_public_conservative_v1" : "gallery_public_cinematic_v1";
    p.family = "rt_showcase_gallery";
    p.enclosedScene = true;
    p.visibilityBufferEnabled = true;
    p.particlesEnabled = true;

    p.environment.preset = "studio";
    p.environment.ownership = "authored_visible_gallery_ibl";
    p.environment.iblEnabled = true;
    p.environment.iblDiffuse = 0.85f;
    p.environment.iblSpecular = 1.25f;
    p.environment.backgroundVisible = true;
    p.environment.backgroundExposure = 1.0f;
    p.environment.backgroundBlur = 0.55f;

    p.lighting.rigId = "rt_showcase_gallery";
    p.lighting.source = "scene_cinematic_profile";
    p.lighting.sunDirection = glm::vec3(0.35f, 0.85f, 0.25f);
    p.lighting.sunColor = glm::vec3(1.0f);
    p.lighting.sunIntensity = 2.85f;
    p.lighting.shadowBias = 0.0030f;
    p.lighting.shadowPCFRadius = 3.0f;
    p.lighting.fogDensity = conservativeMode ? 0.010f : 0.012f;
    p.lighting.fogFalloff = conservativeMode ? 0.60f : 0.55f;
    p.lighting.godRayIntensity = conservativeMode ? 0.36f : 0.42f;

    p.reflections.ownership = "gallery_visible_ibl_plus_local_panels";
    p.reflections.localProbeRigId = "gallery_visible_ibl_panels";
    p.reflections.localProbeEnabled = true;
    p.reflections.localProbeDiffuse = 0.18f;
    p.reflections.localProbeSpecular = 0.34f;
    p.reflections.ssrStrength = 0.70f;
    p.reflections.rtReflectionComposition = 0.72f;
    p.reflections.rtGIRayDistance = 18.0f;

    p.temporal.ssaoRadius = 0.16f;
    p.temporal.ssaoBias = 0.05f;
    p.temporal.ssaoIntensity = conservativeMode ? 0.12f : 0.18f;

    p.post.renderScale = conservativeMode ? 0.67f : 0.85f;
    p.post.exposure = conservativeMode ? 1.0f : 1.12f;
    p.post.bloomIntensity = conservativeMode ? 0.11f : 0.12f;
    p.post.vignette = conservativeMode ? 0.06f : 0.08f;
    p.post.lensDirt = 0.0f;
    p.post.warm = conservativeMode ? 0.0f : 0.06f;
    p.post.cool = conservativeMode ? 0.0f : 0.04f;
    p.post.contrast = conservativeMode ? 1.02f : 1.10f;
    p.post.saturation = conservativeMode ? 1.02f : 1.24f;

    SceneReflectionProbeProfile leftProbe{};
    leftProbe.id = "RTGallery_LocalProbe_Left";
    leftProbe.center = glm::vec3(-19.0f, 1.7f, -0.1f);
    leftProbe.extents = glm::vec3(5.5f, 2.4f, 3.2f);
    leftProbe.blendDistance = 2.25f;
    p.reflectionProbes.push_back(leftProbe);

    SceneReflectionProbeProfile rightProbe{};
    rightProbe.id = "RTGallery_LocalProbe_Right";
    rightProbe.center = glm::vec3(-9.5f, 1.7f, -0.1f);
    rightProbe.extents = glm::vec3(5.5f, 2.4f, 3.2f);
    rightProbe.blendDistance = 2.25f;
    p.reflectionProbes.push_back(rightProbe);

    AddAreaFixture(p,
                   "ProfileLight_Gallery_Softbox",
                   "gallery_key_softbox",
                   glm::vec3(-14.0f, 3.35f, -1.0f),
                   glm::vec3(-14.0f, 2.35f, -0.75f),
                   glm::vec2(5.5f, 2.2f),
                   glm::vec3(1.0f, 0.93f, 0.82f),
                   3.8f,
                   18.0f);
    AddSpotFixture(p,
                   "ProfileLight_Gallery_KeyLight",
                   "gallery_key_spot",
                   glm::vec3(-11.0f, 3.5f, -3.0f),
                   glm::vec3(-11.4f, 2.7f, -2.4f),
                   glm::vec3(1.0f, 0.95f, 0.85f),
                   6.2f,
                   30.0f,
                   true);
    AddAreaFixture(p,
                   "ProfileLight_Gallery_FillLight",
                   "gallery_cool_fill_softbox",
                   glm::vec3(-12.5f, 2.3f, -4.6f),
                   glm::vec3(-12.7f, 2.05f, -3.6f),
                   glm::vec2(6.0f, 3.0f),
                   glm::vec3(0.78f, 0.86f, 1.0f),
                   2.3f,
                   16.0f);
    AddSpotFixture(p,
                   "ProfileLight_Gallery_RimLight",
                   "gallery_cool_rim_spot",
                   glm::vec3(-20.0f, 3.0f, 3.0f),
                   glm::vec3(-19.8f, 2.4f, 2.0f),
                   glm::vec3(0.8f, 0.9f, 1.0f),
                   3.2f,
                   25.0f,
                   false);

    p.water.levelY = 0.0f;
    p.water.amplitude = 0.15f;
    p.water.waveLength = 10.0f;
    p.water.speed = 1.0f;
    p.water.dirX = 1.0f;
    p.water.dirZ = 0.25f;
    p.water.secondaryAmplitude = 0.08f;
    p.water.steepness = 0.6f;
    ApplyLightingBalance(p);
    return p;
}

void ApplySceneCinematicProfile(Renderer& renderer, const SceneCinematicProfile& profile) {
    FrameContract::SceneVisualInfo visualContract{};
    visualContract.active = true;
    visualContract.profileId = profile.id;
    visualContract.family = profile.family;
    visualContract.source = "scene_cinematic_profile";
    visualContract.enclosedScene = profile.enclosedScene;
    visualContract.visibleExternalHDRIAllowed = !profile.enclosedScene || profile.family == "rt_showcase_gallery";
    visualContract.externalHDRIVisible = profile.environment.backgroundVisible && profile.environment.iblEnabled;
    visualContract.invalidExternalHDRI =
        profile.enclosedScene &&
        !visualContract.visibleExternalHDRIAllowed &&
        visualContract.externalHDRIVisible;
    visualContract.environmentOwner = profile.environment.ownership;
    visualContract.reflectionOwner = profile.reflections.ownership;
    visualContract.localReflectionProbeRigId = profile.reflections.localProbeRigId;
    visualContract.lightRigId = profile.lighting.rigId;
    visualContract.shadowPolicyId = profile.lighting.shadowPolicyId;
    visualContract.exposurePolicyId = profile.post.exposurePolicyId;
    visualContract.profileLightFixtureCount = static_cast<uint32_t>(profile.lightFixtures.size());
    visualContract.materialPaletteId = profile.material.worldPaletteId;
    visualContract.lightingScriptId = profile.material.lightingScriptId;
    visualContract.lightingBalancePolicyId = profile.lightingBalance.policyId;
    visualContract.lightingBalancePolicyActive = profile.lightingBalance.active;
    visualContract.lightingBalanceSunScale = profile.lightingBalance.sunScale;
    visualContract.lightingBalanceAmbientScale = profile.lightingBalance.ambientScale;
    visualContract.lightingBalanceLocalFixtureScale = profile.lightingBalance.localFixtureScale;
    visualContract.lightingBalanceLocalProbeDiffuseScale =
        profile.lightingBalance.localProbeDiffuseScale;
    visualContract.lightingBalanceLocalProbeSpecularScale =
        profile.lightingBalance.localProbeSpecularScale;
    visualContract.lightingBalanceExposureScale = profile.lightingBalance.exposureScale;
    visualContract.lightingBalanceSSAOScale = profile.lightingBalance.ssaoScale;
    visualContract.materialClassSetId = profile.material.materialClassSetId;
    visualContract.materialLayerSetId = profile.material.materialLayerSetId;
    visualContract.temporalPolicyId = profile.temporal.policyId;
    visualContract.postPolicyId = profile.post.policyId;
    visualContract.postQualitySetId = profile.post.qualitySetId;
    visualContract.toneMapperPreset = profile.post.toneMapperPreset;
    renderer.SetSceneVisualContract(std::move(visualContract));

    renderer.SetVisibilityBufferEnabled(profile.visibilityBufferEnabled);
    renderer.SetParticlesEnabled(profile.particlesEnabled);

    renderer.SetEnvironmentPreset(profile.environment.preset);
    renderer.SetIBLEnabled(profile.environment.iblEnabled);
    renderer.SetIBLIntensity(profile.environment.iblDiffuse, profile.environment.iblSpecular);
    renderer.SetBackgroundPresentation(profile.environment.backgroundVisible,
                                       profile.environment.backgroundExposure,
                                       profile.environment.backgroundBlur);
    renderer.SetEnvironmentRotation(profile.environment.rotationDegrees);
    renderer.SetLocalReflectionProbeRadiance(profile.reflections.localProbeEnabled,
                                             profile.reflections.localProbeDiffuse,
                                             profile.reflections.localProbeSpecular);

    renderer.SetLightingRigContract(profile.lighting.rigId,
                                    profile.lighting.source,
                                    profile.lighting.safeVariantActive);
    renderer.SetSunDirection(SafeDirection(profile.lighting.sunDirection));
    renderer.SetSunColor(profile.lighting.sunColor);
    renderer.SetSunIntensity(profile.lighting.sunIntensity);
    renderer.SetAmbientLighting(profile.lighting.ambientColor, profile.lighting.ambientIntensity);
    renderer.SetShadowsEnabled(profile.lighting.shadowsEnabled);
    renderer.SetShadowBias(profile.lighting.shadowBias);
    renderer.SetShadowPCFRadius(profile.lighting.shadowPCFRadius);
    renderer.SetCascadeSplitLambda(profile.lighting.cascadeSplitLambda);
    renderer.SetFogEnabled(profile.lighting.fogEnabled);
    renderer.SetFogParams(profile.lighting.fogDensity,
                          profile.lighting.fogHeight,
                          profile.lighting.fogFalloff,
                          profile.lighting.fogStartDistance);
    renderer.SetGodRayIntensity(profile.lighting.godRayIntensity);

    renderer.SetWorldShaderPaletteContract(profile.material.worldPaletteId,
                                           profile.material.lightingScriptId);

    renderer.SetRayTracingEnabled(profile.reflections.rayTracingEnabled);
    renderer.SetRTReflectionsEnabled(profile.reflections.rtReflectionsEnabled);
    renderer.SetRTGIEnabled(profile.reflections.rtGIEnabled);
    renderer.SetSSREnabled(profile.reflections.ssrEnabled);
    renderer.SetSSRParams(profile.reflections.ssrMaxDistance,
                          profile.reflections.ssrThickness,
                          profile.reflections.ssrStrength);
    renderer.SetRTReflectionTuning(profile.reflections.rtReflectionDenoiseAlpha,
                                   profile.reflections.rtReflectionComposition,
                                   profile.reflections.rtReflectionRoughnessThreshold,
                                   profile.reflections.rtReflectionHistoryMaxBlend,
                                   profile.reflections.rtReflectionFireflyClampLuma,
                                   profile.reflections.rtReflectionSignalScale);
    renderer.SetRTGITuning(profile.reflections.rtGIStrength,
                           profile.reflections.rtGIRayDistance);

    renderer.SetTAAEnabled(profile.temporal.taaEnabled);
    renderer.SetFXAAEnabled(profile.temporal.fxaaEnabled);
    renderer.SetSSAOEnabled(profile.temporal.ssaoEnabled);
    renderer.SetSSAOParams(profile.temporal.ssaoRadius,
                           profile.temporal.ssaoBias,
                           profile.temporal.ssaoIntensity);

    renderer.SetRenderScale(profile.post.renderScale);
    renderer.SetExposure(profile.post.exposure);
    renderer.SetBloomIntensity(profile.post.bloomIntensity);
    renderer.SetBloomShape(profile.post.bloomThreshold,
                           profile.post.bloomSoftKnee,
                           profile.post.bloomMaxContribution);
    renderer.SetCinematicPostEnabled(profile.post.cinematicEnabled);
    renderer.SetCinematicPost(profile.post.vignette, profile.post.lensDirt);
    renderer.SetCinematicPostEffects(profile.post.motionBlur,
                                     profile.post.depthOfField,
                                     profile.post.dofFocusDistance,
                                     profile.post.dofAperture,
                                     profile.post.motionBlurEnabled,
                                     profile.post.depthOfFieldEnabled);
    renderer.SetToneMapperPreset(profile.post.toneMapperPreset);
    renderer.SetColorGrade(profile.post.warm, profile.post.cool);
    renderer.SetToneGrade(profile.post.contrast, profile.post.saturation);

    renderer.SetWaterParams(profile.water.levelY,
                            profile.water.amplitude,
                            profile.water.waveLength,
                            profile.water.speed,
                            profile.water.dirX,
                            profile.water.dirZ,
                            profile.water.secondaryAmplitude,
                            profile.water.steepness);
    renderer.SetWaterOptics(profile.water.roughness, profile.water.fresnelStrength);
}

} // namespace Cortex::Graphics
