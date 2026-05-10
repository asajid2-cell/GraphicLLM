#include "Graphics/RendererTuningState.h"

#include "Graphics/Renderer.h"
#include "Graphics/RendererControlApplier.h"

#include <algorithm>
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
            {"specular_intensity", state.environment.specularIntensity}
        }},
        {"ray_tracing", {
            {"enabled", state.rayTracing.enabled},
            {"reflections", state.rayTracing.reflectionsEnabled},
            {"gi", state.rayTracing.giEnabled}
        }},
        {"screen_space", {
            {"ssao", state.screenSpace.ssaoEnabled},
            {"ssao_radius", state.screenSpace.ssaoRadius},
            {"ssao_bias", state.screenSpace.ssaoBias},
            {"ssao_intensity", state.screenSpace.ssaoIntensity},
            {"ssr", state.screenSpace.ssrEnabled},
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
            {"secondary_amplitude", state.water.secondaryAmplitude}
        }},
        {"particles", {
            {"enabled", state.particles.enabled},
            {"density_scale", state.particles.densityScale}
        }},
        {"cinematic_post", {
            {"enabled", state.cinematicPost.enabled},
            {"bloom_threshold", state.cinematicPost.bloomThreshold},
            {"bloom_soft_knee", state.cinematicPost.bloomSoftKnee},
            {"vignette", state.cinematicPost.vignette},
            {"lens_dirt", state.cinematicPost.lensDirt}
        }}
    };
}

template <typename T>
void ReadValue(const json& object, const char* key, T& value) {
    if (object.contains(key) && !object.at(key).is_null()) {
        value = object.at(key).get<T>();
    }
}

RendererTuningState FromJson(const json& root) {
    RendererTuningState state{};

    if (root.contains("quality") && root.at("quality").is_object()) {
        const auto& q = root.at("quality");
        ReadValue(q, "preset", state.quality.preset);
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
    }
    if (root.contains("ray_tracing") && root.at("ray_tracing").is_object()) {
        const auto& rt = root.at("ray_tracing");
        ReadValue(rt, "enabled", state.rayTracing.enabled);
        ReadValue(rt, "reflections", state.rayTracing.reflectionsEnabled);
        ReadValue(rt, "gi", state.rayTracing.giEnabled);
    }
    if (root.contains("screen_space") && root.at("screen_space").is_object()) {
        const auto& ss = root.at("screen_space");
        ReadValue(ss, "ssao", state.screenSpace.ssaoEnabled);
        ReadValue(ss, "ssao_radius", state.screenSpace.ssaoRadius);
        ReadValue(ss, "ssao_bias", state.screenSpace.ssaoBias);
        ReadValue(ss, "ssao_intensity", state.screenSpace.ssaoIntensity);
        ReadValue(ss, "ssr", state.screenSpace.ssrEnabled);
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
    }
    if (root.contains("particles") && root.at("particles").is_object()) {
        const auto& p = root.at("particles");
        ReadValue(p, "enabled", state.particles.enabled);
        ReadValue(p, "density_scale", state.particles.densityScale);
    }
    if (root.contains("cinematic_post") && root.at("cinematic_post").is_object()) {
        const auto& c = root.at("cinematic_post");
        ReadValue(c, "enabled", state.cinematicPost.enabled);
        ReadValue(c, "bloom_threshold", state.cinematicPost.bloomThreshold);
        ReadValue(c, "bloom_soft_knee", state.cinematicPost.bloomSoftKnee);
        ReadValue(c, "vignette", state.cinematicPost.vignette);
        ReadValue(c, "lens_dirt", state.cinematicPost.lensDirt);
    }

    return ClampRendererTuningState(state);
}

} // namespace

RendererTuningState CaptureRendererTuningState(const Renderer& renderer) {
    RendererTuningState state{};

    const auto quality = renderer.GetQualityState();
    const auto features = renderer.GetFeatureState();
    const auto rt = renderer.GetRayTracingState();
    const auto water = renderer.GetWaterState();
    const auto post = renderer.GetPostProcessState();

    state.quality.renderScale = quality.renderScale;
    state.quality.taaEnabled = features.taaEnabled;
    state.quality.fxaaEnabled = features.fxaaEnabled;
    state.quality.gpuCullingEnabled = renderer.IsGPUCullingEnabled();
    state.quality.safeLightingRigOnLowVRAM = features.useSafeLightingRigOnLowVRAM;

    state.lighting.exposure = quality.exposure;
    state.lighting.bloomIntensity = quality.bloomIntensity;
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

    state.rayTracing.enabled = rt.enabled;
    state.rayTracing.reflectionsEnabled = rt.reflectionsEnabled;
    state.rayTracing.giEnabled = rt.giEnabled;

    state.screenSpace.ssaoEnabled = features.ssaoEnabled;
    state.screenSpace.ssaoRadius = 1.5f;
    state.screenSpace.ssaoBias = 0.02f;
    state.screenSpace.ssaoIntensity = 1.0f;
    state.screenSpace.ssrEnabled = features.ssrEnabled;
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

    state.particles.enabled = features.particlesEnabled;

    state.cinematicPost.bloomThreshold = post.bloomThreshold;
    state.cinematicPost.bloomSoftKnee = post.bloomSoftKnee;
    state.cinematicPost.vignette = 0.0f;
    state.cinematicPost.lensDirt = 0.0f;

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

    state.screenSpace.ssaoRadius = std::clamp(state.screenSpace.ssaoRadius, 0.01f, 5.0f);
    state.screenSpace.ssaoBias = std::clamp(state.screenSpace.ssaoBias, 0.0f, 1.0f);
    state.screenSpace.ssaoIntensity = std::clamp(state.screenSpace.ssaoIntensity, 0.0f, 5.0f);

    state.atmosphere.fogDensity = std::clamp(state.atmosphere.fogDensity, 0.0f, 0.1f);
    state.atmosphere.fogHeight = std::clamp(state.atmosphere.fogHeight, -100.0f, 100.0f);
    state.atmosphere.fogFalloff = std::clamp(state.atmosphere.fogFalloff, 0.01f, 10.0f);

    state.water.waveAmplitude = std::clamp(state.water.waveAmplitude, 0.0f, 2.0f);
    state.water.waveLength = std::clamp(state.water.waveLength, 0.1f, 100.0f);
    state.water.waveSpeed = std::clamp(state.water.waveSpeed, 0.0f, 20.0f);
    state.water.secondaryAmplitude = std::clamp(state.water.secondaryAmplitude, 0.0f, 2.0f);

    state.particles.densityScale = std::clamp(state.particles.densityScale, 0.0f, 2.0f);

    state.cinematicPost.bloomThreshold = std::clamp(state.cinematicPost.bloomThreshold, 0.1f, 10.0f);
    state.cinematicPost.bloomSoftKnee = std::clamp(state.cinematicPost.bloomSoftKnee, 0.0f, 1.0f);
    state.cinematicPost.vignette = std::clamp(state.cinematicPost.vignette, 0.0f, 1.0f);
    state.cinematicPost.lensDirt = std::clamp(state.cinematicPost.lensDirt, 0.0f, 1.0f);

    return state;
}

void ApplyRendererTuningState(Renderer& renderer, const RendererTuningState& rawState) {
    const RendererTuningState state = ClampRendererTuningState(rawState);

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

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RayTracing, state.rayTracing.enabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTReflections, state.rayTracing.reflectionsEnabled);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::RTGI, state.rayTracing.giEnabled);

    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::SSAO, state.screenSpace.ssaoEnabled);
    ApplySSAOParamsControl(renderer,
                           state.screenSpace.ssaoRadius,
                           state.screenSpace.ssaoBias,
                           state.screenSpace.ssaoIntensity);
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::SSR, state.screenSpace.ssrEnabled);
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
    ApplyFeatureToggleControl(renderer, RendererFeatureToggle::Particles, state.particles.enabled);
    ApplyBloomShapeControl(renderer,
                           state.cinematicPost.bloomThreshold,
                           state.cinematicPost.bloomSoftKnee,
                           4.0f);
}

std::filesystem::path GetDefaultRendererTuningStatePath() {
    return std::filesystem::current_path() / "user" / "graphics_settings.json";
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
