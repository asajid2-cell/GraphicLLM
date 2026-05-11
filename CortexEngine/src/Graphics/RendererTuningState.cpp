#include "Graphics/RendererTuningState.h"

#include "Graphics/Renderer.h"
#include "Graphics/RendererControlApplier.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Cortex::Graphics {

namespace {

using nlohmann::json;

json ToJson(const RendererTuningState& state) {
    return {
        {"schema", 1},
        {"quality", {
            {"preset", state.quality.preset},
            {"dirty_from_ui", state.quality.dirtyFromUI},
            {"render_scale", state.quality.renderScale},
            {"taa", state.quality.taaEnabled},
            {"fxaa", state.quality.fxaaEnabled},
            {"gpu_culling", state.quality.gpuCullingEnabled},
            {"safe_lighting_rig_on_low_vram", state.quality.safeLightingRigOnLowVRAM}
        }},
        {"lighting", {
            {"exposure", state.lighting.exposure},
            {"bloom_intensity", state.lighting.bloomIntensity},
            {"warm", state.lighting.warm},
            {"cool", state.lighting.cool},
            {"sun_intensity", state.lighting.sunIntensity},
            {"god_ray_intensity", state.lighting.godRayIntensity},
            {"area_light_size_scale", state.lighting.areaLightSizeScale},
            {"shadow_bias", state.lighting.shadowBias},
            {"shadow_pcf_radius", state.lighting.shadowPCFRadius},
            {"cascade_split_lambda", state.lighting.cascadeSplitLambda}
        }},
        {"environment", {
            {"id", state.environment.environmentId},
            {"ibl_enabled", state.environment.iblEnabled},
            {"ibl_limit_enabled", state.environment.iblLimitEnabled},
            {"diffuse_intensity", state.environment.diffuseIntensity},
            {"specular_intensity", state.environment.specularIntensity},
            {"background_visible", state.environment.backgroundVisible},
            {"background_exposure", state.environment.backgroundExposure},
            {"background_blur", state.environment.backgroundBlur},
            {"rotation_degrees", state.environment.rotationDegrees}
        }},
        {"ray_tracing", {
            {"enabled", state.rayTracing.enabled},
            {"reflections", state.rayTracing.reflectionsEnabled},
            {"gi", state.rayTracing.giEnabled},
            {"reflection_denoise_alpha", state.rayTracing.reflectionDenoiseAlpha},
            {"reflection_composition_strength", state.rayTracing.reflectionCompositionStrength},
            {"reflection_roughness_threshold", state.rayTracing.reflectionRoughnessThreshold},
            {"reflection_history_max_blend", state.rayTracing.reflectionHistoryMaxBlend},
            {"reflection_firefly_clamp_luma", state.rayTracing.reflectionFireflyClampLuma},
            {"reflection_signal_scale", state.rayTracing.reflectionSignalScale}
        }},
        {"screen_space", {
            {"ssao", state.screenSpace.ssaoEnabled},
            {"ssao_radius", state.screenSpace.ssaoRadius},
            {"ssao_bias", state.screenSpace.ssaoBias},
            {"ssao_intensity", state.screenSpace.ssaoIntensity},
            {"ssr", state.screenSpace.ssrEnabled},
            {"ssr_max_distance", state.screenSpace.ssrMaxDistance},
            {"ssr_thickness", state.screenSpace.ssrThickness},
            {"ssr_strength", state.screenSpace.ssrStrength},
            {"pcss", state.screenSpace.pcssEnabled}
        }},
        {"atmosphere", {
            {"fog", state.atmosphere.fogEnabled},
            {"fog_density", state.atmosphere.fogDensity},
            {"fog_height", state.atmosphere.fogHeight},
            {"fog_falloff", state.atmosphere.fogFalloff}
        }},
        {"water", {
            {"level_y", state.water.levelY},
            {"wave_amplitude", state.water.waveAmplitude},
            {"wave_length", state.water.waveLength},
            {"wave_speed", state.water.waveSpeed},
            {"secondary_amplitude", state.water.secondaryAmplitude},
            {"roughness", state.water.roughness},
            {"fresnel_strength", state.water.fresnelStrength}
        }},
        {"particles", {
            {"enabled", state.particles.enabled},
            {"density_scale", state.particles.densityScale},
            {"quality_scale", state.particles.qualityScale},
            {"bloom_contribution", state.particles.bloomContribution},
            {"soft_depth_fade", state.particles.softDepthFade},
            {"wind_influence", state.particles.windInfluence}
        }},
        {"cinematic_post", {
            {"enabled", state.cinematicPost.enabled},
            {"color_grade_preset", state.cinematicPost.colorGradePreset},
            {"tone_mapper_preset", state.cinematicPost.toneMapperPreset},
            {"bloom_threshold", state.cinematicPost.bloomThreshold},
            {"bloom_soft_knee", state.cinematicPost.bloomSoftKnee},
            {"contrast", state.cinematicPost.contrast},
            {"saturation", state.cinematicPost.saturation},
            {"vignette", state.cinematicPost.vignette},
            {"lens_dirt", state.cinematicPost.lensDirt},
            {"motion_blur", state.cinematicPost.motionBlur},
            {"depth_of_field", state.cinematicPost.depthOfField}
        }}
    };
}

template <typename T>
void ReadValue(const json& object, const char* key, T& value) {
    if (object.contains(key) && !object.at(key).is_null()) {
        value = object.at(key).get<T>();
    }
}

void ApplyNamedColorGradePreset(RendererTuningState& state) {
    std::string& id = state.cinematicPost.colorGradePreset;
    if (id.empty()) {
        id = "custom";
    }

    if (id == "neutral") {
        state.lighting.warm = 0.0f;
        state.lighting.cool = 0.0f;
        state.cinematicPost.contrast = 1.0f;
        state.cinematicPost.saturation = 1.0f;
    } else if (id == "warm_film") {
        state.lighting.warm = 0.32f;
        state.lighting.cool = -0.08f;
        state.cinematicPost.contrast = 1.12f;
        state.cinematicPost.saturation = 1.18f;
    } else if (id == "cool_moon") {
        state.lighting.warm = -0.08f;
        state.lighting.cool = 0.34f;
        state.cinematicPost.contrast = 1.08f;
        state.cinematicPost.saturation = 0.92f;
    } else if (id == "bleach_bypass") {
        state.lighting.warm = 0.04f;
        state.lighting.cool = 0.08f;
        state.cinematicPost.contrast = 1.28f;
        state.cinematicPost.saturation = 0.72f;
    } else if (id != "custom") {
        id = "custom";
    }
}

bool IsKnownColorGradePreset(const std::string& id) {
    return id == "neutral" ||
           id == "warm_film" ||
           id == "cool_moon" ||
           id == "bleach_bypass" ||
           id == "custom";
}

void ClampToneMapperPreset(RendererTuningState& state) {
    std::string& id = state.cinematicPost.toneMapperPreset;
    if (id.empty() || id == "clean_filmic") {
        id = "aces";
    } else if (id != "aces" && id != "reinhard" && id != "filmic_soft" && id != "punchy") {
        id = "aces";
    }
}

RendererTuningState FromJson(const json& root) {
    RendererTuningState state{};

    if (root.contains("quality") && root.at("quality").is_object()) {
        const auto& q = root.at("quality");
        ReadValue(q, "preset", state.quality.preset);
        ReadValue(q, "dirty_from_ui", state.quality.dirtyFromUI);
        ReadValue(q, "render_scale", state.quality.renderScale);
        ReadValue(q, "taa", state.quality.taaEnabled);
        ReadValue(q, "fxaa", state.quality.fxaaEnabled);
        ReadValue(q, "gpu_culling", state.quality.gpuCullingEnabled);
        ReadValue(q, "safe_lighting_rig_on_low_vram", state.quality.safeLightingRigOnLowVRAM);
    }
    if (root.contains("lighting") && root.at("lighting").is_object()) {
        const auto& l = root.at("lighting");
        ReadValue(l, "exposure", state.lighting.exposure);
        ReadValue(l, "bloom_intensity", state.lighting.bloomIntensity);
        ReadValue(l, "warm", state.lighting.warm);
        ReadValue(l, "cool", state.lighting.cool);
        ReadValue(l, "sun_intensity", state.lighting.sunIntensity);
        ReadValue(l, "god_ray_intensity", state.lighting.godRayIntensity);
        ReadValue(l, "area_light_size_scale", state.lighting.areaLightSizeScale);
        ReadValue(l, "shadow_bias", state.lighting.shadowBias);
        ReadValue(l, "shadow_pcf_radius", state.lighting.shadowPCFRadius);
        ReadValue(l, "cascade_split_lambda", state.lighting.cascadeSplitLambda);
    }
    if (root.contains("environment") && root.at("environment").is_object()) {
        const auto& e = root.at("environment");
        ReadValue(e, "id", state.environment.environmentId);
        ReadValue(e, "ibl_enabled", state.environment.iblEnabled);
        ReadValue(e, "ibl_limit_enabled", state.environment.iblLimitEnabled);
        ReadValue(e, "diffuse_intensity", state.environment.diffuseIntensity);
        ReadValue(e, "specular_intensity", state.environment.specularIntensity);
        ReadValue(e, "background_visible", state.environment.backgroundVisible);
        ReadValue(e, "background_exposure", state.environment.backgroundExposure);
        ReadValue(e, "background_blur", state.environment.backgroundBlur);
        ReadValue(e, "rotation_degrees", state.environment.rotationDegrees);
    }
    if (root.contains("ray_tracing") && root.at("ray_tracing").is_object()) {
        const auto& rt = root.at("ray_tracing");
        ReadValue(rt, "enabled", state.rayTracing.enabled);
        ReadValue(rt, "reflections", state.rayTracing.reflectionsEnabled);
        ReadValue(rt, "gi", state.rayTracing.giEnabled);
        ReadValue(rt, "reflection_denoise_alpha", state.rayTracing.reflectionDenoiseAlpha);
        ReadValue(rt, "reflection_composition_strength", state.rayTracing.reflectionCompositionStrength);
        ReadValue(rt, "reflection_roughness_threshold", state.rayTracing.reflectionRoughnessThreshold);
        ReadValue(rt, "reflection_history_max_blend", state.rayTracing.reflectionHistoryMaxBlend);
        ReadValue(rt, "reflection_firefly_clamp_luma", state.rayTracing.reflectionFireflyClampLuma);
        ReadValue(rt, "reflection_signal_scale", state.rayTracing.reflectionSignalScale);
    }
    if (root.contains("screen_space") && root.at("screen_space").is_object()) {
        const auto& ss = root.at("screen_space");
        ReadValue(ss, "ssao", state.screenSpace.ssaoEnabled);
        ReadValue(ss, "ssao_radius", state.screenSpace.ssaoRadius);
        ReadValue(ss, "ssao_bias", state.screenSpace.ssaoBias);
        ReadValue(ss, "ssao_intensity", state.screenSpace.ssaoIntensity);
        ReadValue(ss, "ssr", state.screenSpace.ssrEnabled);
        ReadValue(ss, "ssr_max_distance", state.screenSpace.ssrMaxDistance);
        ReadValue(ss, "ssr_thickness", state.screenSpace.ssrThickness);
        ReadValue(ss, "ssr_strength", state.screenSpace.ssrStrength);
        ReadValue(ss, "pcss", state.screenSpace.pcssEnabled);
    }
    if (root.contains("atmosphere") && root.at("atmosphere").is_object()) {
        const auto& a = root.at("atmosphere");
        ReadValue(a, "fog", state.atmosphere.fogEnabled);
        ReadValue(a, "fog_density", state.atmosphere.fogDensity);
        ReadValue(a, "fog_height", state.atmosphere.fogHeight);
        ReadValue(a, "fog_falloff", state.atmosphere.fogFalloff);
    }
    if (root.contains("water") && root.at("water").is_object()) {
        const auto& w = root.at("water");
        ReadValue(w, "level_y", state.water.levelY);
        ReadValue(w, "wave_amplitude", state.water.waveAmplitude);
        ReadValue(w, "wave_length", state.water.waveLength);
        ReadValue(w, "wave_speed", state.water.waveSpeed);
        ReadValue(w, "secondary_amplitude", state.water.secondaryAmplitude);
        ReadValue(w, "roughness", state.water.roughness);
        ReadValue(w, "fresnel_strength", state.water.fresnelStrength);
    }
    if (root.contains("particles") && root.at("particles").is_object()) {
        const auto& p = root.at("particles");
        ReadValue(p, "enabled", state.particles.enabled);
        ReadValue(p, "density_scale", state.particles.densityScale);
        ReadValue(p, "quality_scale", state.particles.qualityScale);
        ReadValue(p, "bloom_contribution", state.particles.bloomContribution);
        ReadValue(p, "soft_depth_fade", state.particles.softDepthFade);
        ReadValue(p, "wind_influence", state.particles.windInfluence);
    }
    if (root.contains("cinematic_post") && root.at("cinematic_post").is_object()) {
        const auto& c = root.at("cinematic_post");
        ReadValue(c, "enabled", state.cinematicPost.enabled);
        if (c.contains("color_grade_preset")) {
            ReadValue(c, "color_grade_preset", state.cinematicPost.colorGradePreset);
        } else {
            std::string legacyPreset;
            ReadValue(c, "preset", legacyPreset);
            if (IsKnownColorGradePreset(legacyPreset)) {
                state.cinematicPost.colorGradePreset = legacyPreset;
            }
        }
        if (c.contains("tone_mapper_preset")) {
            ReadValue(c, "tone_mapper_preset", state.cinematicPost.toneMapperPreset);
        } else {
            ReadValue(c, "preset", state.cinematicPost.toneMapperPreset);
        }
        ReadValue(c, "bloom_threshold", state.cinematicPost.bloomThreshold);
        ReadValue(c, "bloom_soft_knee", state.cinematicPost.bloomSoftKnee);
        ReadValue(c, "contrast", state.cinematicPost.contrast);
        ReadValue(c, "saturation", state.cinematicPost.saturation);
        ReadValue(c, "vignette", state.cinematicPost.vignette);
        ReadValue(c, "lens_dirt", state.cinematicPost.lensDirt);
        ReadValue(c, "motion_blur", state.cinematicPost.motionBlur);
        ReadValue(c, "depth_of_field", state.cinematicPost.depthOfField);
    }

    return ClampRendererTuningState(state);
}

json GraphicsPresetToTuningJson(const json& preset) {
    json root = json::object();
    root["schema"] = 1;

    root["quality"] = preset.value("quality", json::object());
    root["quality"]["preset"] = preset.value("id", "runtime");
    root["quality"]["dirty_from_ui"] = false;

    root["lighting"] = preset.value("lighting", json::object());

    root["environment"] = json::object();
    if (preset.contains("environment") && preset.at("environment").is_object()) {
        const auto& env = preset.at("environment");
        if (env.contains("environment_id")) {
            root["environment"]["id"] = env.at("environment_id");
        }
        if (env.contains("id")) {
            root["environment"]["id"] = env.at("id");
        }
        if (env.contains("ibl_enabled")) {
            root["environment"]["ibl_enabled"] = env.at("ibl_enabled");
        }
        if (env.contains("ibl_limit_enabled")) {
            root["environment"]["ibl_limit_enabled"] = env.at("ibl_limit_enabled");
        }
        if (env.contains("diffuse_intensity")) {
            root["environment"]["diffuse_intensity"] = env.at("diffuse_intensity");
        }
        if (env.contains("specular_intensity")) {
            root["environment"]["specular_intensity"] = env.at("specular_intensity");
        }
        if (env.contains("background_visible")) {
            root["environment"]["background_visible"] = env.at("background_visible");
        }
        if (env.contains("background_exposure")) {
            root["environment"]["background_exposure"] = env.at("background_exposure");
        }
        if (env.contains("background_blur")) {
            root["environment"]["background_blur"] = env.at("background_blur");
        }
        if (env.contains("rotation_degrees")) {
            root["environment"]["rotation_degrees"] = env.at("rotation_degrees");
        }
    }

    root["ray_tracing"] = preset.value("ray_tracing", json::object());
    root["screen_space"] = preset.value("screen_space", json::object());
    root["atmosphere"] = preset.value("atmosphere", json::object());
    root["water"] = preset.value("water", json::object());

    root["particles"] = json::object();
    if (preset.contains("particles") && preset.at("particles").is_object()) {
        const auto& particles = preset.at("particles");
        if (particles.contains("enabled")) {
            root["particles"]["enabled"] = particles.at("enabled");
        }
        if (particles.contains("density_scale")) {
            root["particles"]["density_scale"] = particles.at("density_scale");
        } else if (particles.contains("density")) {
            root["particles"]["density_scale"] = particles.at("density");
        }
        if (particles.contains("quality_scale")) {
            root["particles"]["quality_scale"] = particles.at("quality_scale");
        } else if (particles.contains("quality")) {
            const std::string quality = particles.at("quality").get<std::string>();
            root["particles"]["quality_scale"] =
                (quality == "low") ? 0.5f : ((quality == "high") ? 1.5f : 1.0f);
        }
        if (particles.contains("bloom_contribution")) {
            root["particles"]["bloom_contribution"] = particles.at("bloom_contribution");
        }
        if (particles.contains("soft_depth_fade")) {
            root["particles"]["soft_depth_fade"] = particles.at("soft_depth_fade");
        }
        if (particles.contains("wind_influence")) {
            root["particles"]["wind_influence"] = particles.at("wind_influence");
        }
    }

    root["cinematic_post"] = preset.value("cinematic_post", json::object());
    return root;
}

} // namespace

RendererTuningState CaptureRendererTuningState(const Renderer& renderer) {
    RendererTuningState state{};

    const auto quality = renderer.GetQualityState();
    const auto features = renderer.GetFeatureState();
    const auto rt = renderer.GetRayTracingState();
    const auto water = renderer.GetWaterState();
    const auto post = renderer.GetPostProcessState();

    state.quality.preset = renderer.GetActiveGraphicsPreset();
    state.quality.dirtyFromUI = renderer.IsGraphicsPresetDirtyFromUI();
    state.quality.renderScale = quality.renderScale;
    state.quality.taaEnabled = features.taaEnabled;
    state.quality.fxaaEnabled = features.fxaaEnabled;
    state.quality.gpuCullingEnabled = renderer.IsGPUCullingEnabled();
    state.quality.safeLightingRigOnLowVRAM = features.useSafeLightingRigOnLowVRAM;

    state.lighting.exposure = quality.exposure;
    state.lighting.bloomIntensity = quality.bloomIntensity;
    state.lighting.warm = post.warm;
    state.lighting.cool = post.cool;
    state.lighting.sunIntensity = features.sunIntensity;
    state.lighting.godRayIntensity = features.godRayIntensity;
    state.lighting.areaLightSizeScale = features.areaLightSizeScale;
    state.lighting.shadowBias = quality.shadowBias;
    state.lighting.shadowPCFRadius = quality.shadowPCFRadius;
    state.lighting.cascadeSplitLambda = quality.cascadeSplitLambda;

    state.environment.environmentId = renderer.GetCurrentEnvironmentName();
    state.environment.iblEnabled = features.iblEnabled;
    state.environment.iblLimitEnabled = features.iblLimitEnabled;
    state.environment.diffuseIntensity = features.iblDiffuseIntensity;
    state.environment.specularIntensity = features.iblSpecularIntensity;
    state.environment.backgroundVisible = features.backgroundVisible;
    state.environment.backgroundExposure = features.backgroundExposure;
    state.environment.backgroundBlur = features.backgroundBlur;
    state.environment.rotationDegrees = features.environmentRotationDegrees;

    state.rayTracing.enabled = rt.requested;
    state.rayTracing.reflectionsEnabled = rt.reflectionsEnabled;
    state.rayTracing.giEnabled = rt.giEnabled;
    state.rayTracing.reflectionDenoiseAlpha = rt.reflectionDenoiseAlpha;
    state.rayTracing.reflectionCompositionStrength = rt.reflectionCompositionStrength;
    state.rayTracing.reflectionRoughnessThreshold = rt.reflectionRoughnessThreshold;
    state.rayTracing.reflectionHistoryMaxBlend = rt.reflectionHistoryMaxBlend;
    state.rayTracing.reflectionFireflyClampLuma = rt.reflectionFireflyClampLuma;
    state.rayTracing.reflectionSignalScale = rt.reflectionSignalScale;

    state.screenSpace.ssaoEnabled = features.ssaoEnabled;
    state.screenSpace.ssaoRadius = features.ssaoRadius;
    state.screenSpace.ssaoBias = features.ssaoBias;
    state.screenSpace.ssaoIntensity = features.ssaoIntensity;
    state.screenSpace.ssrEnabled = features.ssrEnabled;
    state.screenSpace.ssrMaxDistance = features.ssrMaxDistance;
    state.screenSpace.ssrThickness = features.ssrThickness;
    state.screenSpace.ssrStrength = features.ssrStrength;
    state.screenSpace.pcssEnabled = features.pcssEnabled;

    state.atmosphere.fogEnabled = features.fogEnabled;
    state.atmosphere.fogDensity = features.fogDensity;
    state.atmosphere.fogHeight = features.fogHeight;
    state.atmosphere.fogFalloff = features.fogFalloff;

    state.water.levelY = water.levelY;
    state.water.waveAmplitude = water.waveAmplitude;
    state.water.waveLength = water.waveLength;
    state.water.waveSpeed = water.waveSpeed;
    state.water.secondaryAmplitude = water.secondaryAmplitude;
    state.water.roughness = water.roughness;
    state.water.fresnelStrength = water.fresnelStrength;

    state.particles.enabled = features.particlesEnabled;
    state.particles.densityScale = features.particleDensityScale;
    state.particles.qualityScale = features.particleQualityScale;
    state.particles.bloomContribution = features.particleBloomContribution;
    state.particles.softDepthFade = features.particleSoftDepthFade;
    state.particles.windInfluence = features.particleWindInfluence;

    state.cinematicPost.bloomThreshold = post.bloomThreshold;
    state.cinematicPost.bloomSoftKnee = post.bloomSoftKnee;
    state.cinematicPost.colorGradePreset = post.colorGradePreset;
    state.cinematicPost.toneMapperPreset = post.toneMapperPreset;
    state.cinematicPost.contrast = post.contrast;
    state.cinematicPost.saturation = post.saturation;
    state.cinematicPost.enabled = post.cinematicEnabled;
    state.cinematicPost.vignette = post.vignette;
    state.cinematicPost.lensDirt = post.lensDirt;
    state.cinematicPost.motionBlur = post.motionBlur;
    state.cinematicPost.depthOfField = post.depthOfField;

    return ClampRendererTuningState(state);
}

RendererTuningState ClampRendererTuningState(RendererTuningState state) {
    state.quality.renderScale = std::clamp(state.quality.renderScale, 0.5f, 1.0f);

    state.lighting.exposure = std::clamp(state.lighting.exposure, 0.05f, 10.0f);
    state.lighting.bloomIntensity = std::clamp(state.lighting.bloomIntensity, 0.0f, 5.0f);
    state.lighting.warm = std::clamp(state.lighting.warm, -1.0f, 1.0f);
    state.lighting.cool = std::clamp(state.lighting.cool, -1.0f, 1.0f);
    state.lighting.sunIntensity = std::clamp(state.lighting.sunIntensity, 0.0f, 20.0f);
    state.lighting.godRayIntensity = std::clamp(state.lighting.godRayIntensity, 0.0f, 3.0f);
    state.lighting.areaLightSizeScale = std::clamp(state.lighting.areaLightSizeScale, 0.25f, 2.0f);
    state.lighting.shadowBias = std::clamp(state.lighting.shadowBias, 0.0f, 0.02f);
    state.lighting.shadowPCFRadius = std::clamp(state.lighting.shadowPCFRadius, 0.0f, 8.0f);
    state.lighting.cascadeSplitLambda = std::clamp(state.lighting.cascadeSplitLambda, 0.0f, 1.0f);

    state.environment.diffuseIntensity = std::clamp(state.environment.diffuseIntensity, 0.0f, 3.0f);
    state.environment.specularIntensity = std::clamp(state.environment.specularIntensity, 0.0f, 3.0f);
    state.environment.backgroundExposure = std::clamp(state.environment.backgroundExposure, 0.0f, 4.0f);
    state.environment.backgroundBlur = std::clamp(state.environment.backgroundBlur, 0.0f, 1.0f);
    if (!std::isfinite(state.environment.rotationDegrees)) {
        state.environment.rotationDegrees = 0.0f;
    }
    state.environment.rotationDegrees = std::fmod(state.environment.rotationDegrees, 360.0f);
    if (state.environment.rotationDegrees < 0.0f) {
        state.environment.rotationDegrees += 360.0f;
    }

    state.rayTracing.reflectionDenoiseAlpha = std::clamp(state.rayTracing.reflectionDenoiseAlpha, 0.02f, 1.0f);
    state.rayTracing.reflectionCompositionStrength =
        std::clamp(state.rayTracing.reflectionCompositionStrength, 0.0f, 1.0f);
    state.rayTracing.reflectionRoughnessThreshold =
        std::clamp(state.rayTracing.reflectionRoughnessThreshold, 0.05f, 1.0f);
    state.rayTracing.reflectionHistoryMaxBlend =
        std::clamp(state.rayTracing.reflectionHistoryMaxBlend, 0.0f, 0.5f);
    state.rayTracing.reflectionFireflyClampLuma =
        std::clamp(state.rayTracing.reflectionFireflyClampLuma, 4.0f, 32.0f);
    state.rayTracing.reflectionSignalScale =
        std::clamp(state.rayTracing.reflectionSignalScale, 0.0f, 2.0f);

    state.screenSpace.ssaoRadius = std::clamp(state.screenSpace.ssaoRadius, 0.01f, 5.0f);
    state.screenSpace.ssaoBias = std::clamp(state.screenSpace.ssaoBias, 0.0f, 1.0f);
    state.screenSpace.ssaoIntensity = std::clamp(state.screenSpace.ssaoIntensity, 0.0f, 5.0f);
    state.screenSpace.ssrMaxDistance = std::clamp(state.screenSpace.ssrMaxDistance, 1.0f, 120.0f);
    state.screenSpace.ssrThickness = std::clamp(state.screenSpace.ssrThickness, 0.005f, 1.0f);
    state.screenSpace.ssrStrength = std::clamp(state.screenSpace.ssrStrength, 0.0f, 1.0f);

    state.atmosphere.fogDensity = std::clamp(state.atmosphere.fogDensity, 0.0f, 0.1f);
    state.atmosphere.fogHeight = std::clamp(state.atmosphere.fogHeight, -100.0f, 100.0f);
    state.atmosphere.fogFalloff = std::clamp(state.atmosphere.fogFalloff, 0.01f, 10.0f);

    state.water.waveAmplitude = std::clamp(state.water.waveAmplitude, 0.0f, 2.0f);
    state.water.waveLength = std::clamp(state.water.waveLength, 0.1f, 100.0f);
    state.water.waveSpeed = std::clamp(state.water.waveSpeed, 0.0f, 20.0f);
    state.water.secondaryAmplitude = std::clamp(state.water.secondaryAmplitude, 0.0f, 2.0f);
    state.water.roughness = std::clamp(state.water.roughness, 0.01f, 1.0f);
    state.water.fresnelStrength = std::clamp(state.water.fresnelStrength, 0.0f, 3.0f);

    state.particles.densityScale = std::clamp(state.particles.densityScale, 0.0f, 2.0f);
    state.particles.qualityScale = std::clamp(state.particles.qualityScale, 0.25f, 2.0f);
    state.particles.bloomContribution = std::clamp(state.particles.bloomContribution, 0.0f, 2.0f);
    state.particles.softDepthFade = std::clamp(state.particles.softDepthFade, 0.0f, 1.0f);
    state.particles.windInfluence = std::clamp(state.particles.windInfluence, 0.0f, 2.0f);

    ApplyNamedColorGradePreset(state);
    ClampToneMapperPreset(state);

    state.cinematicPost.bloomThreshold = std::clamp(state.cinematicPost.bloomThreshold, 0.1f, 10.0f);
    state.cinematicPost.bloomSoftKnee = std::clamp(state.cinematicPost.bloomSoftKnee, 0.0f, 1.0f);
    state.cinematicPost.contrast = std::clamp(state.cinematicPost.contrast, 0.5f, 1.5f);
    state.cinematicPost.saturation = std::clamp(state.cinematicPost.saturation, 0.0f, 2.0f);
    state.cinematicPost.vignette = std::clamp(state.cinematicPost.vignette, 0.0f, 1.0f);
    state.cinematicPost.lensDirt = std::clamp(state.cinematicPost.lensDirt, 0.0f, 1.0f);
    state.cinematicPost.motionBlur = std::clamp(state.cinematicPost.motionBlur, 0.0f, 1.0f);
    state.cinematicPost.depthOfField = std::clamp(state.cinematicPost.depthOfField, 0.0f, 1.0f);

    return state;
}

void ApplyRendererTuningState(Renderer& renderer, const RendererTuningState& rawState) {
    const RendererTuningState state = ClampRendererTuningState(rawState);

    renderer.SetActiveGraphicsPreset(state.quality.preset, state.quality.dirtyFromUI);
    ApplyRenderScaleControl(renderer, state.quality.renderScale);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::TAA, state.quality.taaEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::FXAA, state.quality.fxaaEnabled);
    ApplyGPUCullingEnabledControl(renderer, state.quality.gpuCullingEnabled);
    ApplySafeLightingRigControl(renderer, state.quality.safeLightingRigOnLowVRAM);

    ApplyExposureControl(renderer, state.lighting.exposure);
    ApplyBloomIntensityControl(renderer, state.lighting.bloomIntensity);
    ApplyColorGradeControl(renderer, state.lighting.warm, state.lighting.cool);
    ApplySunIntensityControl(renderer, state.lighting.sunIntensity);
    ApplyGodRayIntensityControl(renderer, state.lighting.godRayIntensity);
    ApplyAreaLightSizeControl(renderer, state.lighting.areaLightSizeScale);
    ApplyShadowBiasControl(renderer, state.lighting.shadowBias);
    ApplyShadowPCFRadiusControl(renderer, state.lighting.shadowPCFRadius);
    ApplyCascadeSplitLambdaControl(renderer, state.lighting.cascadeSplitLambda);

    if (!state.environment.environmentId.empty()) {
        ApplyEnvironmentPresetControl(renderer, state.environment.environmentId);
    }
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::IBL, state.environment.iblEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::IBLLimit, state.environment.iblLimitEnabled);
    ApplyIBLIntensityControl(renderer,
                             state.environment.diffuseIntensity,
                             state.environment.specularIntensity);
    ApplyBackgroundPresentationControl(renderer,
                                       state.environment.backgroundVisible,
                                       state.environment.backgroundExposure,
                                       state.environment.backgroundBlur);
    ApplyEnvironmentRotationControl(renderer, state.environment.rotationDegrees);

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RayTracing, state.rayTracing.enabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTReflections, state.rayTracing.reflectionsEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTGI, state.rayTracing.giEnabled);
    ApplyRTReflectionTuningControl(renderer,
                                   state.rayTracing.reflectionDenoiseAlpha,
                                   state.rayTracing.reflectionCompositionStrength,
                                   state.rayTracing.reflectionRoughnessThreshold,
                                   state.rayTracing.reflectionHistoryMaxBlend,
                                   state.rayTracing.reflectionFireflyClampLuma,
                                   state.rayTracing.reflectionSignalScale);

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::SSAO, state.screenSpace.ssaoEnabled);
    ApplySSAOParamsControl(renderer,
                           state.screenSpace.ssaoRadius,
                           state.screenSpace.ssaoBias,
                           state.screenSpace.ssaoIntensity);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::SSR, state.screenSpace.ssrEnabled);
    ApplySSRParamsControl(renderer,
                          state.screenSpace.ssrMaxDistance,
                          state.screenSpace.ssrThickness,
                          state.screenSpace.ssrStrength);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::PCSS, state.screenSpace.pcssEnabled);

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::Fog, state.atmosphere.fogEnabled);
    ApplyFogParamsControl(renderer,
                          state.atmosphere.fogDensity,
                          state.atmosphere.fogHeight,
                          state.atmosphere.fogFalloff);

    ApplyWaterStateControl(renderer,
                           state.water.levelY,
                           state.water.waveAmplitude,
                           state.water.waveLength,
                           state.water.waveSpeed,
                           state.water.secondaryAmplitude);
    ApplyWaterOpticsControl(renderer, state.water.roughness, state.water.fresnelStrength);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::Particles, state.particles.enabled);
    renderer.SetParticleDensityScale(state.particles.densityScale);
    renderer.SetParticleTuning(state.particles.qualityScale,
                               state.particles.bloomContribution,
                               state.particles.softDepthFade,
                               state.particles.windInfluence);
    ApplyBloomShapeControl(renderer,
                           state.cinematicPost.bloomThreshold,
                           state.cinematicPost.bloomSoftKnee,
                           4.0f);
    ApplyToneGradeControl(renderer,
                          state.cinematicPost.contrast,
                          state.cinematicPost.saturation);
    renderer.SetToneMapperPreset(state.cinematicPost.toneMapperPreset);
    renderer.SetColorGradePreset(state.cinematicPost.colorGradePreset);
    renderer.SetCinematicPostEnabled(state.cinematicPost.enabled);
    ApplyCinematicPostControl(renderer,
                              state.cinematicPost.vignette,
                              state.cinematicPost.lensDirt);
    renderer.SetCinematicPostEffects(state.cinematicPost.motionBlur,
                                     state.cinematicPost.depthOfField);
}

std::filesystem::path GetDefaultRendererTuningStatePath() {
    return std::filesystem::current_path() / "user" / "graphics_settings.json";
}

std::filesystem::path GetDefaultRendererGraphicsPresetCollectionPath() {
    namespace fs = std::filesystem;
    const fs::path cwd = fs::current_path();
    const fs::path direct = cwd / "assets" / "config" / "graphics_presets.json";
    if (fs::exists(direct)) {
        return direct;
    }
    const fs::path buildBinRelative = cwd.parent_path().parent_path() /
        "assets" / "config" / "graphics_presets.json";
    if (fs::exists(buildBinRelative)) {
        return buildBinRelative;
    }
    return direct;
}

std::optional<RendererTuningState> LoadRendererTuningStateFile(const std::filesystem::path& path,
                                                               std::string* error) {
    if (error) {
        error->clear();
    }
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    try {
        json root;
        in >> root;
        if (!root.is_object()) {
            if (error) {
                *error = "graphics settings root is not an object";
            }
            return std::nullopt;
        }
        const int schema = root.value("schema", 1);
        if (schema != 1) {
            if (error) {
                *error = "unsupported graphics settings schema";
            }
            return std::nullopt;
        }
        return FromJson(root);
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

std::optional<RendererTuningState> LoadRendererGraphicsPresetFile(const std::filesystem::path& path,
                                                                  const std::string& presetId,
                                                                  std::string* resolvedPresetId,
                                                                  std::string* error) {
    if (resolvedPresetId) {
        resolvedPresetId->clear();
    }
    if (error) {
        error->clear();
    }

    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "failed to open graphics preset collection";
        }
        return std::nullopt;
    }

    try {
        json root;
        in >> root;
        if (!root.is_object()) {
            if (error) {
                *error = "graphics preset root is not an object";
            }
            return std::nullopt;
        }
        const int schema = root.value("schema", 1);
        if (schema != 1) {
            if (error) {
                *error = "unsupported graphics preset schema";
            }
            return std::nullopt;
        }
        if (!root.contains("presets") || !root.at("presets").is_array()) {
            if (error) {
                *error = "graphics preset collection has no presets array";
            }
            return std::nullopt;
        }

        std::string requested = presetId.empty() ? root.value("default", "") : presetId;
        if (requested.empty()) {
            if (error) {
                *error = "graphics preset id is empty and no default is configured";
            }
            return std::nullopt;
        }

        for (const auto& preset : root.at("presets")) {
            if (!preset.is_object()) {
                continue;
            }
            const std::string id = preset.value("id", "");
            if (id != requested) {
                continue;
            }
            RendererTuningState state = FromJson(GraphicsPresetToTuningJson(preset));
            state.quality.preset = id;
            state.quality.dirtyFromUI = false;
            if (resolvedPresetId) {
                *resolvedPresetId = id;
            }
            return state;
        }

        if (error) {
            *error = "graphics preset '" + requested + "' was not found";
        }
        return std::nullopt;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return std::nullopt;
    }
}

bool SaveRendererTuningStateFile(const std::filesystem::path& path,
                                 const RendererTuningState& state,
                                 std::string* error) {
    if (error) {
        error->clear();
    }
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::trunc);
        if (!out) {
            if (error) {
                *error = "failed to open graphics settings file for write";
            }
            return false;
        }
        out << ToJson(ClampRendererTuningState(state)).dump(2) << '\n';
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

} // namespace Cortex::Graphics
