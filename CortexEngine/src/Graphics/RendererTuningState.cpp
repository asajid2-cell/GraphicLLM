#include "Graphics/RendererTuningState.h"

#include "Graphics/Renderer.h"
#include "Graphics/RendererControlApplier.h"

#include <algorithm>

namespace Cortex::Graphics {

RendererTuningState CaptureRendererTuningState(const Renderer& renderer) {
    RendererTuningState state{};

    const auto quality = renderer.GetQualityState();
    const auto features = renderer.GetFeatureState();
    const auto rt = renderer.GetRayTracingState();
    const auto water = renderer.GetWaterState();

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
}

} // namespace Cortex::Graphics
