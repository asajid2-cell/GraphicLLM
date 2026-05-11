#pragma once

#include "RendererControlTypes.h"

#include <cstdint>
#include <string>

#include <glm/vec3.hpp>

namespace Cortex::Graphics {

class Renderer;

enum class RendererFeatureToggle {
    Shadows,
    RayTracing,
    RTReflections,
    RTGI,
    TAA,
    FXAA,
    SSR,
    SSAO,
    IBL,
    Fog,
    Particles,
    IBLLimit,
    PCSS
};

void ApplyDebugControlState(Renderer& renderer, const RendererDebugControlState& state);
void ApplyDebugControlReset(Renderer& renderer);
void ApplyHighQualityStartupControls(Renderer& renderer, bool preserveRenderScale);
void ApplyHeroVisualBaselineControls(Renderer& renderer);
void ApplyAutoDemoFeatureLock(Renderer& renderer);
void ApplyRTShowcaseSceneControls(Renderer& renderer, bool conservativeMode);
void ApplyMaterialLabSceneControls(Renderer& renderer);
void ApplyGlassWaterCourtyardSceneControls(Renderer& renderer);
void ApplyEffectsShowcaseSceneControls(Renderer& renderer);
void ApplyTemporalValidationSceneControls(Renderer& renderer);
void ApplyCornellSceneControls(Renderer& renderer);
void ApplyGodRaysSceneControls(Renderer& renderer);
void ApplyDragonWaterStudioSunControls(Renderer& renderer);
void ApplyOutdoorWorldSceneControls(Renderer& renderer,
                                    const glm::vec3& sunDirection,
                                    const glm::vec3& sunColor,
                                    float sunIntensity);
void ApplyEditorModeBaseControls(Renderer& renderer);
void ApplyEditorTimeOfDayControls(Renderer& renderer,
                                  const glm::vec3& sunDirection,
                                  const glm::vec3& sunColor,
                                  float sunIntensity);
bool ApplyRenderScaleControl(Renderer& renderer, float scale);
bool ApplyExposureControl(Renderer& renderer, float exposure);
bool ApplyBloomIntensityControl(Renderer& renderer, float intensity);
void ApplyBloomShapeControl(Renderer& renderer, float threshold, float softKnee, float maxContribution);
void ApplyFeatureToggleControl(Renderer& renderer, RendererFeatureToggle toggle, bool enabled);
bool ToggleFeatureControl(Renderer& renderer, RendererFeatureToggle toggle);
void ApplyRTReflectionTuningControl(Renderer& renderer, float denoiseAlpha, float compositionStrength);
void ApplyShadowBiasControl(Renderer& renderer, float bias);
void ApplyShadowPCFRadiusControl(Renderer& renderer, float radius);
void ApplyCascadeSplitLambdaControl(Renderer& renderer, float lambda);
void ApplyEnvironmentPresetControl(Renderer& renderer, const std::string& name);
void ApplySunIntensityControl(Renderer& renderer, float intensity);
void ApplySunDirectionControl(Renderer& renderer, const glm::vec3& direction);
void ApplySunColorControl(Renderer& renderer, const glm::vec3& color);
void ApplyIBLIntensityControl(Renderer& renderer, float diffuse, float specular);
void ApplyBackgroundPresentationControl(Renderer& renderer, bool visible, float exposure, float blur);
void ApplyColorGradeControl(Renderer& renderer, float warm, float cool);
void ApplyToneGradeControl(Renderer& renderer, float contrast, float saturation);
void ApplyCinematicPostControl(Renderer& renderer, float vignette, float lensDirt);
void ApplySSAOParamsControl(Renderer& renderer, float radius, float bias, float intensity);
void ApplySSRParamsControl(Renderer& renderer, float maxDistance, float thickness, float strength);
void ApplyGodRayIntensityControl(Renderer& renderer, float intensity);
void ApplySafeLightingRigControl(Renderer& renderer, bool enabled);
void ApplyWaterSteepnessControl(Renderer& renderer, float steepness);
void ApplyWaterStateControl(Renderer& renderer,
                            float levelY,
                            float waveAmplitude,
                            float waveLength,
                            float waveSpeed,
                            float secondaryAmplitude);
void ApplyWaterOpticsControl(Renderer& renderer, float roughness, float fresnelStrength);
void ApplyFogDensityControl(Renderer& renderer, float density);
void ApplyFogParamsControl(Renderer& renderer, float density, float height, float falloff);
void ApplyAreaLightSizeControl(Renderer& renderer, float scale);
void ApplySafeQualityPresetControl(Renderer& renderer);
void ApplyEnvironmentResidencyLoadControl(Renderer& renderer, int count);
void ApplyDebugViewModeControl(Renderer& renderer, int mode);
void ApplyDebugOverlayControl(Renderer& renderer, bool visible, int selectedSection);
void ApplyHZBDebugMipDeltaControl(Renderer& renderer, int delta);
void ApplyGPUCullingEnabledControl(Renderer& renderer, bool enabled);
bool ToggleGPUCullingFreezeControl(Renderer& renderer);
void ApplyVoxelBackendControl(Renderer& renderer, bool enabled);
void ApplyShadowPCFRadiusDeltaControl(Renderer& renderer, float delta);
void ApplyShadowBiasDeltaControl(Renderer& renderer, float delta);
void ApplyCascadeSplitLambdaDeltaControl(Renderer& renderer, float delta);
void ApplyCascadeResolutionScaleDeltaControl(Renderer& renderer, uint32_t cascadeIndex, float delta);
void CycleDebugViewControl(Renderer& renderer);
void CycleEnvironmentPresetControl(Renderer& renderer);

} // namespace Cortex::Graphics
