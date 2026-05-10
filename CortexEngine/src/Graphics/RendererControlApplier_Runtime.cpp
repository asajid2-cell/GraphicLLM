#include "RendererControlApplier.h"

#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

void ApplyHighQualityStartupControls(Renderer& renderer, bool preserveRenderScale) {
    if (!preserveRenderScale) {
        renderer.SetRenderScale(1.0f);
    }

    renderer.SetTAAEnabled(true);
    renderer.SetFXAAEnabled(false);
    renderer.SetSSAOEnabled(true);
    renderer.SetSSREnabled(true);
    renderer.SetFogEnabled(true);
    renderer.SetShadowsEnabled(true);
    renderer.SetIBLEnabled(true);
    renderer.SetBloomIntensity(0.3f);
    renderer.SetExposure(1.2f);
    renderer.SetParticlesEnabled(true);
    renderer.SetRTReflectionsEnabled(true);
    renderer.SetRTGIEnabled(true);
}

bool ApplyRenderScaleControl(Renderer& renderer, float scale) {
    scale = std::clamp(scale, 0.5f, 1.0f);
    const float current = renderer.GetRenderScale();
    if (std::fabs(scale - current) <= 0.01f) {
        return false;
    }

    renderer.SetRenderScale(scale);
    return true;
}

bool ApplyExposureControl(Renderer& renderer, float exposure) {
    exposure = std::clamp(exposure, 0.05f, 10.0f);
    const float current = renderer.GetQualityState().exposure;
    if (std::fabs(exposure - current) <= 0.001f) {
        return false;
    }

    renderer.SetExposure(exposure);
    return true;
}

bool ApplyBloomIntensityControl(Renderer& renderer, float intensity) {
    intensity = std::clamp(intensity, 0.0f, 5.0f);
    const float current = renderer.GetQualityState().bloomIntensity;
    if (std::fabs(intensity - current) <= 0.01f) {
        return false;
    }

    renderer.SetBloomIntensity(intensity);
    return true;
}

void ApplyBloomShapeControl(Renderer& renderer, float threshold, float softKnee, float maxContribution) {
    renderer.SetBloomShape(std::clamp(threshold, 0.1f, 10.0f),
                           std::clamp(softKnee, 0.0f, 1.0f),
                           std::clamp(maxContribution, 0.0f, 16.0f));
}

void ApplyCinematicPostControl(Renderer& renderer, float vignette, float lensDirt) {
    renderer.SetCinematicPost(std::clamp(vignette, 0.0f, 1.0f),
                              std::clamp(lensDirt, 0.0f, 1.0f));
}

void ApplyFeatureToggleControl(Renderer& renderer, RendererFeatureToggle toggle, bool enabled) {
    switch (toggle) {
    case RendererFeatureToggle::Shadows:
        renderer.SetShadowsEnabled(enabled);
        break;
    case RendererFeatureToggle::RayTracing:
        if (renderer.GetRayTracingState().supported) {
            renderer.SetRayTracingEnabled(enabled);
        }
        break;
    case RendererFeatureToggle::RTReflections:
        renderer.SetRTReflectionsEnabled(enabled);
        break;
    case RendererFeatureToggle::RTGI:
        renderer.SetRTGIEnabled(enabled);
        break;
    case RendererFeatureToggle::TAA:
        renderer.SetTAAEnabled(enabled);
        break;
    case RendererFeatureToggle::FXAA:
        renderer.SetFXAAEnabled(enabled);
        break;
    case RendererFeatureToggle::SSR:
        renderer.SetSSREnabled(enabled);
        break;
    case RendererFeatureToggle::SSAO:
        renderer.SetSSAOEnabled(enabled);
        break;
    case RendererFeatureToggle::IBL:
        renderer.SetIBLEnabled(enabled);
        break;
    case RendererFeatureToggle::Fog:
        renderer.SetFogEnabled(enabled);
        break;
    case RendererFeatureToggle::Particles:
        renderer.SetParticlesEnabled(enabled);
        break;
    case RendererFeatureToggle::IBLLimit:
        renderer.SetIBLLimitEnabled(enabled);
        break;
    case RendererFeatureToggle::PCSS:
        renderer.SetPCSS(enabled);
        break;
    }
}

bool ToggleFeatureControl(Renderer& renderer, RendererFeatureToggle toggle) {
    const auto quality = renderer.GetQualityState();
    const auto features = renderer.GetFeatureState();
    const auto rt = renderer.GetRayTracingState();

    bool current = false;
    switch (toggle) {
    case RendererFeatureToggle::Shadows:
        current = quality.shadowsEnabled;
        break;
    case RendererFeatureToggle::RayTracing:
        current = rt.enabled;
        break;
    case RendererFeatureToggle::RTReflections:
        current = rt.reflectionsEnabled;
        break;
    case RendererFeatureToggle::RTGI:
        current = rt.giEnabled;
        break;
    case RendererFeatureToggle::TAA:
        current = features.taaEnabled;
        break;
    case RendererFeatureToggle::FXAA:
        current = features.fxaaEnabled;
        break;
    case RendererFeatureToggle::SSR:
        current = features.ssrEnabled;
        break;
    case RendererFeatureToggle::SSAO:
        current = features.ssaoEnabled;
        break;
    case RendererFeatureToggle::IBL:
        current = features.iblEnabled;
        break;
    case RendererFeatureToggle::Fog:
        current = features.fogEnabled;
        break;
    case RendererFeatureToggle::Particles:
        current = features.particlesEnabled;
        break;
    case RendererFeatureToggle::IBLLimit:
        current = features.iblLimitEnabled;
        break;
    case RendererFeatureToggle::PCSS:
        current = features.pcssEnabled;
        break;
    }

    const bool enabled = !current;
    ApplyFeatureToggleControl(renderer, toggle, enabled);
    return enabled;
}

void ApplyShadowBiasControl(Renderer& renderer, float bias) {
    renderer.SetShadowBias(bias);
}

void ApplyShadowPCFRadiusControl(Renderer& renderer, float radius) {
    renderer.SetShadowPCFRadius(radius);
}

void ApplyCascadeSplitLambdaControl(Renderer& renderer, float lambda) {
    renderer.SetCascadeSplitLambda(lambda);
}

void ApplyEnvironmentPresetControl(Renderer& renderer, const std::string& name) {
    renderer.SetEnvironmentPreset(name);
}

void ApplySunIntensityControl(Renderer& renderer, float intensity) {
    renderer.SetSunIntensity(std::clamp(intensity, 0.0f, 20.0f));
}

void ApplySunDirectionControl(Renderer& renderer, const glm::vec3& direction) {
    renderer.SetSunDirection(direction);
}

void ApplySunColorControl(Renderer& renderer, const glm::vec3& color) {
    renderer.SetSunColor(color);
}

void ApplyIBLIntensityControl(Renderer& renderer, float diffuse, float specular) {
    renderer.SetIBLIntensity(std::clamp(diffuse, 0.0f, 3.0f),
                             std::clamp(specular, 0.0f, 3.0f));
}

void ApplyColorGradeControl(Renderer& renderer, float warm, float cool) {
    renderer.SetColorGrade(std::clamp(warm, -1.0f, 1.0f),
                           std::clamp(cool, -1.0f, 1.0f));
}

void ApplySSAOParamsControl(Renderer& renderer, float radius, float bias, float intensity) {
    renderer.SetSSAOParams(std::clamp(radius, 0.01f, 5.0f),
                           std::clamp(bias, 0.0f, 1.0f),
                           std::clamp(intensity, 0.0f, 5.0f));
}

void ApplyGodRayIntensityControl(Renderer& renderer, float intensity) {
    renderer.SetGodRayIntensity(std::clamp(intensity, 0.0f, 3.0f));
}

void ApplySafeLightingRigControl(Renderer& renderer, bool enabled) {
    renderer.SetUseSafeLightingRigOnLowVRAM(enabled);
}

void ApplyWaterSteepnessControl(Renderer& renderer, float steepness) {
    const auto water = renderer.GetWaterState();
    renderer.SetWaterParams(
        water.levelY,
        water.waveAmplitude,
        water.waveLength,
        water.waveSpeed,
        water.primaryDirection.x,
        water.primaryDirection.y,
        water.secondaryAmplitude,
        std::clamp(steepness, 0.0f, 1.0f));
}

void ApplyWaterStateControl(Renderer& renderer,
                            float levelY,
                            float waveAmplitude,
                            float waveLength,
                            float waveSpeed,
                            float secondaryAmplitude) {
    const auto water = renderer.GetWaterState();
    renderer.SetWaterParams(
        levelY,
        std::clamp(waveAmplitude, 0.0f, 2.0f),
        std::clamp(waveLength, 0.1f, 100.0f),
        std::clamp(waveSpeed, 0.0f, 20.0f),
        water.primaryDirection.x,
        water.primaryDirection.y,
        std::clamp(secondaryAmplitude, 0.0f, 2.0f),
        water.steepness);
}

void ApplyFogDensityControl(Renderer& renderer, float density) {
    const auto features = renderer.GetFeatureState();
    renderer.SetFogParams(std::clamp(density, 0.0f, 0.1f),
                          features.fogHeight,
                          features.fogFalloff);
}

void ApplyFogParamsControl(Renderer& renderer, float density, float height, float falloff) {
    renderer.SetFogParams(std::clamp(density, 0.0f, 0.1f),
                          std::clamp(height, -100.0f, 100.0f),
                          std::clamp(falloff, 0.01f, 10.0f));
}

void ApplyAreaLightSizeControl(Renderer& renderer, float scale) {
    renderer.SetAreaLightSizeScale(std::clamp(scale, 0.25f, 2.0f));
}

void ApplySafeQualityPresetControl(Renderer& renderer) {
    renderer.ApplySafeQualityPreset();
}

void ApplyEnvironmentResidencyLoadControl(Renderer& renderer, int count) {
    renderer.LoadAdditionalEnvironmentMaps(count);
}

void CycleEnvironmentPresetControl(Renderer& renderer) {
    renderer.CycleEnvironmentPreset();
}

} // namespace Cortex::Graphics
