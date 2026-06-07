#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Cortex::Graphics {

class Renderer;

struct SceneEnvironmentProfile {
    std::string preset = "neutral_procedural";
    std::string ownership = "scene_local_neutral";
    bool iblEnabled = false;
    float iblDiffuse = 0.0f;
    float iblSpecular = 0.0f;
    bool backgroundVisible = false;
    float backgroundExposure = 0.0f;
    float backgroundBlur = 1.0f;
    float rotationDegrees = 0.0f;
};

struct SceneLightingProfile {
    std::string rigId = "scene_local_default";
    std::string source = "scene_cinematic_profile";
    std::string shadowPolicyId = "scene_local_soft_stable_shadows_v1";
    bool safeVariantActive = false;
    glm::vec3 sunDirection{ -0.38f, 0.72f, 0.28f };
    glm::vec3 sunColor{ 1.0f, 0.90f, 0.78f };
    float sunIntensity = 1.5f;
    glm::vec3 ambientColor{ 0.045f, 0.045f, 0.050f };
    float ambientIntensity = 1.0f;
    bool shadowsEnabled = true;
    float shadowBias = 0.0005f;
    float shadowPCFRadius = 1.6f;
    float cascadeSplitLambda = 0.55f;
    bool fogEnabled = true;
    float fogDensity = 0.010f;
    float fogHeight = 0.0f;
    float fogFalloff = 0.55f;
    float fogStartDistance = 0.0f;
    float godRayIntensity = 0.0f;
};

struct SceneLightingBalanceProfile {
    std::string policyId = "scene_local_lighting_balance_v1";
    bool active = false;
    float sunScale = 1.0f;
    float ambientScale = 1.0f;
    float localFixtureScale = 1.0f;
    float localProbeDiffuseScale = 1.0f;
    float localProbeSpecularScale = 1.0f;
    float exposureScale = 1.0f;
    float ssaoScale = 1.0f;
};

struct SceneReflectionProfile {
    std::string ownership = "scene_local";
    std::string localProbeRigId = "none";
    bool localProbeEnabled = false;
    float localProbeDiffuse = 0.0f;
    float localProbeSpecular = 0.0f;
    bool ssrEnabled = true;
    float ssrMaxDistance = 45.0f;
    float ssrThickness = 0.18f;
    float ssrStrength = 0.72f;
    bool rayTracingEnabled = true;
    bool rtReflectionsEnabled = true;
    bool rtGIEnabled = true;
    float rtReflectionDenoiseAlpha = 0.22f;
    float rtReflectionComposition = 0.72f;
    float rtReflectionRoughnessThreshold = 0.72f;
    float rtReflectionHistoryMaxBlend = 0.22f;
    float rtReflectionFireflyClampLuma = 14.0f;
    float rtReflectionSignalScale = 1.0f;
    float rtGIStrength = 0.55f;
    float rtGIRayDistance = 18.0f;
};

struct SceneReflectionProbeProfile {
    std::string id = "scene_local_probe";
    glm::vec3 center{0.0f, 1.35f, 0.0f};
    glm::vec3 extents{4.0f, 2.0f, 4.0f};
    float blendDistance = 1.5f;
    uint32_t environmentIndex = 0;
    bool enabled = true;
};

struct SceneLightFixtureProfile {
    std::string id = "scene_local_light";
    std::string type = "point";
    std::string semanticClass = "practical";
    glm::vec3 position{0.0f, 1.5f, 0.0f};
    glm::vec3 target{0.0f, 1.0f, 0.0f};
    glm::vec3 color{1.0f, 0.85f, 0.65f};
    float intensity = 1.0f;
    float range = 4.0f;
    float innerConeDegrees = 26.0f;
    float outerConeDegrees = 48.0f;
    glm::vec2 areaSize{1.0f, 1.0f};
    bool castsShadows = false;
    bool twoSided = false;
    bool enabled = true;
};

struct SceneTemporalProfile {
    std::string policyId = "stable_default";
    bool taaEnabled = true;
    bool fxaaEnabled = true;
    bool ssaoEnabled = true;
    float ssaoRadius = 0.22f;
    float ssaoBias = 0.035f;
    float ssaoIntensity = 0.22f;
};

struct ScenePostProfile {
    std::string policyId = "cinematic_soft";
    std::string qualitySetId = "scene_local_cinematic_post_quality_v1";
    std::string exposurePolicyId = "scene_local_manual_exposure_v1";
    float renderScale = 0.85f;
    float exposure = 1.12f;
    float bloomIntensity = 0.14f;
    float bloomThreshold = 0.95f;
    float bloomSoftKnee = 0.50f;
    float bloomMaxContribution = 3.0f;
    bool cinematicEnabled = true;
    float vignette = 0.08f;
    float lensDirt = 0.04f;
    float motionBlur = 0.0f;
    float depthOfField = 0.0f;
    float dofFocusDistance = 18.0f;
    float dofAperture = 2.2f;
    bool motionBlurEnabled = false;
    bool depthOfFieldEnabled = false;
    std::string toneMapperPreset = "filmic_soft";
    float warm = 0.08f;
    float cool = 0.04f;
    float contrast = 1.02f;
    float saturation = 1.02f;
};

struct SceneMaterialProfile {
    std::string worldPaletteId = "scene_local_neutral";
    std::string lightingScriptId = "scene_local_neutral";
    std::string materialClassSetId = "scene_local_named_material_classes_v1";
    std::string materialLayerSetId = "scene_local_cinematic_material_layers_v1";
};

struct SceneWaterProfile {
    float levelY = -10.0f;
    float amplitude = 0.0f;
    float waveLength = 8.0f;
    float speed = 0.0f;
    float dirX = 1.0f;
    float dirZ = 0.0f;
    float secondaryAmplitude = 0.0f;
    float steepness = 0.0f;
    float roughness = 0.18f;
    float fresnelStrength = 0.35f;
};

struct SceneCinematicProfile {
    std::string id = "scene_local_default";
    std::string family = "default";
    bool enclosedScene = true;
    bool visibilityBufferEnabled = true;
    bool particlesEnabled = false;
    SceneEnvironmentProfile environment;
    SceneLightingProfile lighting;
    SceneLightingBalanceProfile lightingBalance;
    SceneReflectionProfile reflections;
    std::vector<SceneReflectionProbeProfile> reflectionProbes;
    std::vector<SceneLightFixtureProfile> lightFixtures;
    SceneTemporalProfile temporal;
    ScenePostProfile post;
    SceneMaterialProfile material;
    SceneWaterProfile water;
};

SceneCinematicProfile BuildSceneLocalCinematicProfile(std::string_view sceneFamily);
SceneCinematicProfile BuildGalleryCinematicProfile(bool conservativeMode);
void ApplySceneCinematicProfile(Renderer& renderer, const SceneCinematicProfile& profile);

} // namespace Cortex::Graphics
