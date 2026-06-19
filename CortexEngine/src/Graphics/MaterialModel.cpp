#include "Graphics/MaterialModel.h"

#include "Graphics/MaterialState.h"
#include "Graphics/MaterialPresetRegistry.h"
#include "Graphics/RHI/BindlessConstants.h"
#include "Graphics/VisibilityBuffer.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Cortex::Graphics {

namespace {

constexpr float kDefaultEpsilon = 1.0e-4f;

enum : uint32_t {
    kSurfaceClassDefault = 0u,
    kSurfaceClassGlass = 1u,
    kSurfaceClassMirror = 2u,
    kSurfaceClassPlastic = 3u,
    kSurfaceClassMasonry = 4u,
    kSurfaceClassEmissive = 5u,
    kSurfaceClassBrushedMetal = 6u,
    kSurfaceClassWood = 7u,
    kSurfaceClassWater = 8u,
};

bool HasAuthoredTexture(const std::shared_ptr<DX12Texture>& texture,
                        const DX12Texture* fallback,
                        const std::string& path) {
    if (!texture || texture.get() == fallback) {
        return false;
    }

    // Renderer placeholders keep descriptor tables safe, but an empty source
    // path means the material did not author this map. The shader should use
    // constant material values instead of sampling placeholder maps.
    return !path.empty();
}

[[nodiscard]] bool IsDefaultScalar(float value, float defaultValue) {
    return std::abs(value - defaultValue) <= kDefaultEpsilon;
}

[[nodiscard]] bool IsNearBlack(const glm::vec3& value) {
    return glm::all(glm::lessThanEqual(glm::abs(value), glm::vec3(kDefaultEpsilon)));
}

[[nodiscard]] bool IsNearWhite(const glm::vec3& value) {
    return glm::all(glm::lessThanEqual(glm::abs(value - glm::vec3(1.0f)),
                                       glm::vec3(kDefaultEpsilon)));
}

[[nodiscard]] bool PresetContains(const MaterialModel& model, const char* token) {
    if (!token) {
        return false;
    }
    if (MaterialPresetRegistry::ContainsToken(model.presetName, token)) {
        return true;
    }
    const std::string canonicalPreset = MaterialPresetRegistry::Canonicalize(model.presetName);
    return canonicalPreset != model.presetName &&
           MaterialPresetRegistry::ContainsToken(canonicalPreset, token);
}

[[nodiscard]] bool MaterialTypeNear(const MaterialModel& model, float expectedType) {
    return model.materialType > expectedType - 0.5f && model.materialType < expectedType + 0.5f;
}

[[nodiscard]] uint32_t ToId(SceneMaterialClassId id) {
    return static_cast<uint32_t>(id);
}

[[nodiscard]] uint32_t ToId(MaterialReflectionPreferenceId id) {
    return static_cast<uint32_t>(id);
}

[[nodiscard]] uint32_t ToId(MaterialTemporalPolicyId id) {
    return static_cast<uint32_t>(id);
}

[[nodiscard]] uint32_t ToId(MaterialPostSensitivityId id) {
    return static_cast<uint32_t>(id);
}

[[nodiscard]] SceneMaterialClassId ResolveSceneMaterialClass(const MaterialModel& model) {
    const float emissiveLuminance =
        glm::dot(model.emissiveColor, glm::vec3(0.2126f, 0.7152f, 0.0722f)) *
        std::max(model.emissiveStrength, 0.0f);

    if (PresetContains(model, "water") ||
        PresetContains(model, "lava") ||
        PresetContains(model, "honey") ||
        PresetContains(model, "molasses")) {
        return SceneMaterialClassId::Water;
    }
    if (PresetContains(model, "wet") ||
        PresetContains(model, "puddle") ||
        model.wetnessFactor > 0.05f) {
        return SceneMaterialClassId::WetSurface;
    }
    if (model.transmissionFactor > 0.01f ||
        MaterialTypeNear(model, 1.0f) ||
        PresetContains(model, "glass")) {
        return SceneMaterialClassId::GlassPane;
    }
    if (MaterialTypeNear(model, 2.0f) || PresetContains(model, "mirror")) {
        return SceneMaterialClassId::Mirror;
    }
    if (PresetContains(model, "screen") ||
        PresetContains(model, "display") ||
        PresetContains(model, "monitor") ||
        PresetContains(model, "television")) {
        return SceneMaterialClassId::ScreenPanel;
    }
    if (MaterialTypeNear(model, 5.0f) ||
        PresetContains(model, "emissive") ||
        PresetContains(model, "neon") ||
        emissiveLuminance > 0.05f) {
        return SceneMaterialClassId::EmissiveNeon;
    }
    if (PresetContains(model, "ceramic") ||
        PresetContains(model, "matte") ||
        PresetContains(model, "tile") ||
        PresetContains(model, "porcelain")) {
        return SceneMaterialClassId::CeramicTile;
    }
    if (PresetContains(model, "painted_wall") ||
        PresetContains(model, "backdrop") ||
        PresetContains(model, "drywall") ||
        PresetContains(model, "plaster") ||
        PresetContains(model, "wall_paint")) {
        return SceneMaterialClassId::PaintedWall;
    }
    if (PresetContains(model, "fabric") ||
        PresetContains(model, "cloth") ||
        PresetContains(model, "velvet") ||
        PresetContains(model, "skin") ||
        PresetContains(model, "foliage") ||
        PresetContains(model, "upholstery") ||
        PresetContains(model, "carpet")) {
        return SceneMaterialClassId::Fabric;
    }
    if (PresetContains(model, "rubber") ||
        PresetContains(model, "matte_black") ||
        PresetContains(model, "gym_mat")) {
        return SceneMaterialClassId::Rubber;
    }
    if (PresetContains(model, "polished_metal") ||
        PresetContains(model, "chrome") ||
        PresetContains(model, "gold")) {
        return SceneMaterialClassId::PolishedMetal;
    }
    if (MaterialTypeNear(model, 6.0f) ||
        PresetContains(model, "brushed_metal") ||
        PresetContains(model, "metal")) {
        return SceneMaterialClassId::BrushedMetal;
    }
    if (MaterialTypeNear(model, 3.0f) || PresetContains(model, "plastic")) {
        return SceneMaterialClassId::Plastic;
    }
    if (MaterialTypeNear(model, 4.0f) ||
        PresetContains(model, "sand") ||
        PresetContains(model, "brick") ||
        PresetContains(model, "concrete") ||
        PresetContains(model, "stone") ||
        PresetContains(model, "masonry")) {
        return SceneMaterialClassId::Concrete;
    }
    if (MaterialTypeNear(model, 7.0f) || PresetContains(model, "wood")) {
        return SceneMaterialClassId::PolishedWood;
    }
    if (model.metallic > 0.85f && model.roughness < 0.20f) {
        return SceneMaterialClassId::Mirror;
    }
    return SceneMaterialClassId::Default;
}

[[nodiscard]] uint32_t ResolvePolicySurfaceClass(SceneMaterialClassId sceneClass) {
    switch (sceneClass) {
        case SceneMaterialClassId::GlassPane:
            return kSurfaceClassGlass;
        case SceneMaterialClassId::Mirror:
            return kSurfaceClassMirror;
        case SceneMaterialClassId::Plastic:
        case SceneMaterialClassId::Rubber:
            return kSurfaceClassPlastic;
        case SceneMaterialClassId::PaintedWall:
            return kSurfaceClassMasonry;
        case SceneMaterialClassId::CeramicTile:
        case SceneMaterialClassId::Concrete:
            return kSurfaceClassMasonry;
        case SceneMaterialClassId::EmissiveNeon:
        case SceneMaterialClassId::ScreenPanel:
            return kSurfaceClassEmissive;
        case SceneMaterialClassId::BrushedMetal:
        case SceneMaterialClassId::PolishedMetal:
            return kSurfaceClassBrushedMetal;
        case SceneMaterialClassId::PolishedWood:
        case SceneMaterialClassId::Fabric:
            return kSurfaceClassWood;
        case SceneMaterialClassId::Water:
        case SceneMaterialClassId::WetSurface:
            return kSurfaceClassWater;
        case SceneMaterialClassId::Default:
        default:
            return kSurfaceClassDefault;
    }
}

struct MaterialClassPolicy {
    float roughnessFloor = 0.20f;
    float normalScaleCeiling = 0.42f;
    float proceduralMaskCeiling = 0.38f;
    float reflectionStabilityScale = 0.44f;
    float albedoLuminanceCeiling = 0.78f;
    float albedoChromaCeiling = 0.75f;
    float heightDetailStrength = 0.0f;
    float thinTransmission = 0.0f;
    MaterialReflectionPreferenceId reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
    MaterialTemporalPolicyId temporalPolicy = MaterialTemporalPolicyId::StableDiffuse;
    MaterialPostSensitivityId postSensitivity = MaterialPostSensitivityId::Normal;
    bool reflectionStabilityApplied = false;
    bool forceDielectric = false;
};

[[nodiscard]] MaterialClassPolicy ResolveMaterialClassPolicy(const MaterialModel& model,
                                                             SceneMaterialClassId sceneClassId,
                                                             uint32_t surfaceClassId) {
    MaterialClassPolicy policy{};
    switch (sceneClassId) {
        case SceneMaterialClassId::GlassPane:
            policy.roughnessFloor = 0.03f;
            policy.normalScaleCeiling = 0.18f;
            policy.proceduralMaskCeiling = 0.16f;
            policy.reflectionStabilityScale = 0.78f;
            policy.albedoLuminanceCeiling = 0.72f;
            policy.albedoChromaCeiling = 0.55f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::RTReflection;
            policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            policy.postSensitivity = MaterialPostSensitivityId::ExposureProtected;
            policy.reflectionStabilityApplied = true;
            policy.forceDielectric = true;
            break;
        case SceneMaterialClassId::Mirror:
            policy.roughnessFloor = 0.015f;
            policy.normalScaleCeiling = 0.08f;
            policy.proceduralMaskCeiling = 0.10f;
            policy.reflectionStabilityScale = 1.00f;
            policy.albedoLuminanceCeiling = 0.78f;
            policy.albedoChromaCeiling = 0.40f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::RTReflection;
            policy.temporalPolicy = MaterialTemporalPolicyId::MirrorLocked;
            policy.postSensitivity = MaterialPostSensitivityId::ExposureProtected;
            policy.reflectionStabilityApplied = true;
            break;
        case SceneMaterialClassId::Plastic:
            policy.roughnessFloor = 0.24f;
            policy.normalScaleCeiling = 0.45f;
            policy.proceduralMaskCeiling = 0.48f;
            policy.reflectionStabilityScale = 0.50f;
            policy.albedoLuminanceCeiling = 0.58f;
            policy.albedoChromaCeiling = 0.72f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::LocalProbe;
            policy.reflectionStabilityApplied = true;
            break;
        case SceneMaterialClassId::Rubber:
            policy.roughnessFloor = 0.52f;
            policy.normalScaleCeiling = 0.34f;
            policy.proceduralMaskCeiling = 0.36f;
            policy.reflectionStabilityScale = 0.28f;
            policy.albedoLuminanceCeiling = 0.42f;
            policy.albedoChromaCeiling = 0.50f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
            break;
        case SceneMaterialClassId::PaintedWall:
            policy.roughnessFloor = 0.20f;
            policy.normalScaleCeiling = 0.42f;
            policy.proceduralMaskCeiling = 0.38f;
            policy.heightDetailStrength = 0.20f;
            policy.reflectionStabilityScale = 0.28f;
            policy.albedoLuminanceCeiling = 0.56f;
            policy.albedoChromaCeiling = 0.62f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
            break;
        case SceneMaterialClassId::CeramicTile:
            policy.roughnessFloor = 0.18f;
            policy.normalScaleCeiling = 0.22f;
            policy.proceduralMaskCeiling = 0.30f;
            policy.heightDetailStrength = 0.30f;
            policy.reflectionStabilityScale = 0.58f;
            policy.albedoLuminanceCeiling = 0.68f;
            policy.albedoChromaCeiling = 0.66f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::SSR;
            policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            policy.reflectionStabilityApplied = true;
            break;
        case SceneMaterialClassId::Concrete:
            policy.roughnessFloor = 0.42f;
            policy.normalScaleCeiling = 0.68f;
            policy.proceduralMaskCeiling = 0.78f;
            policy.heightDetailStrength = 0.36f;
            policy.reflectionStabilityScale = 0.36f;
            policy.albedoLuminanceCeiling = 0.58f;
            policy.albedoChromaCeiling = 0.48f;
            break;
        case SceneMaterialClassId::EmissiveNeon:
            policy.roughnessFloor = 0.30f;
            policy.normalScaleCeiling = 0.22f;
            policy.proceduralMaskCeiling = 0.16f;
            policy.reflectionStabilityScale = 0.42f;
            policy.albedoLuminanceCeiling = 1.0f;
            policy.albedoChromaCeiling = 1.0f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
            policy.temporalPolicy = MaterialTemporalPolicyId::EmissiveLocked;
            policy.postSensitivity = MaterialPostSensitivityId::BloomEmitter;
            policy.forceDielectric = true;
            break;
        case SceneMaterialClassId::ScreenPanel:
            policy.roughnessFloor = 0.20f;
            policy.normalScaleCeiling = 0.12f;
            policy.proceduralMaskCeiling = 0.12f;
            policy.reflectionStabilityScale = 0.38f;
            policy.albedoLuminanceCeiling = 1.0f;
            policy.albedoChromaCeiling = 1.0f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
            policy.temporalPolicy = MaterialTemporalPolicyId::EmissiveLocked;
            policy.postSensitivity = MaterialPostSensitivityId::BloomEmitter;
            policy.forceDielectric = true;
            break;
        case SceneMaterialClassId::PolishedMetal:
            policy.roughnessFloor = 0.05f;
            policy.normalScaleCeiling = 0.18f;
            policy.proceduralMaskCeiling = 0.22f;
            policy.reflectionStabilityScale = 0.78f;
            policy.albedoLuminanceCeiling = 0.70f;
            policy.albedoChromaCeiling = 0.44f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::RTReflection;
            policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            policy.postSensitivity = MaterialPostSensitivityId::ExposureProtected;
            policy.reflectionStabilityApplied = true;
            break;
        case SceneMaterialClassId::BrushedMetal:
            policy.roughnessFloor = 0.24f;
            policy.normalScaleCeiling = 0.36f;
            policy.proceduralMaskCeiling = 0.36f;
            policy.reflectionStabilityScale = 0.66f;
            policy.albedoLuminanceCeiling = 0.60f;
            policy.albedoChromaCeiling = 0.52f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::LocalProbe;
            policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            policy.reflectionStabilityApplied = true;
            break;
        case SceneMaterialClassId::PolishedWood:
            policy.roughnessFloor = 0.30f;
            policy.normalScaleCeiling = 0.68f;
            policy.proceduralMaskCeiling = 0.78f;
            policy.heightDetailStrength = 0.34f;
            policy.reflectionStabilityScale = 0.36f;
            policy.albedoLuminanceCeiling = 0.58f;
            policy.albedoChromaCeiling = 0.65f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::LocalProbe;
            break;
        case SceneMaterialClassId::Fabric:
            policy.roughnessFloor = 0.58f;
            policy.normalScaleCeiling = 0.42f;
            policy.proceduralMaskCeiling = 0.58f;
            policy.heightDetailStrength = 0.22f;
            policy.thinTransmission = 0.055f;
            policy.reflectionStabilityScale = 0.24f;
            policy.albedoLuminanceCeiling = 0.54f;
            policy.albedoChromaCeiling = 0.68f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::NeutralFallback;
            break;
        case SceneMaterialClassId::WetSurface:
            policy.roughnessFloor = 0.06f;
            policy.normalScaleCeiling = 0.38f;
            policy.proceduralMaskCeiling = 0.26f;
            policy.reflectionStabilityScale = 0.88f;
            policy.albedoLuminanceCeiling = 0.58f;
            policy.albedoChromaCeiling = 0.55f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::PlanarProbe;
            policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            policy.postSensitivity = MaterialPostSensitivityId::WetHighlight;
            policy.reflectionStabilityApplied = true;
            policy.forceDielectric = true;
            break;
        case SceneMaterialClassId::Water:
            policy.roughnessFloor = 0.03f;
            policy.normalScaleCeiling = 0.45f;
            policy.proceduralMaskCeiling = 0.24f;
            policy.reflectionStabilityScale = 1.00f;
            policy.albedoLuminanceCeiling = 0.70f;
            policy.albedoChromaCeiling = 0.60f;
            policy.reflectionPreference = MaterialReflectionPreferenceId::PlanarProbe;
            policy.temporalPolicy = MaterialTemporalPolicyId::WaterViewDependent;
            policy.postSensitivity = MaterialPostSensitivityId::WetHighlight;
            policy.reflectionStabilityApplied = true;
            policy.forceDielectric = true;
            break;
        default:
            if (surfaceClassId == kSurfaceClassBrushedMetal || model.metallic > 0.85f) {
                policy.reflectionPreference = MaterialReflectionPreferenceId::LocalProbe;
                policy.temporalPolicy = MaterialTemporalPolicyId::StableGlossy;
            }
            break;
    }
    return policy;
}

[[nodiscard]] float AlbedoLuminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

[[nodiscard]] float AlbedoChromaSpan(const glm::vec3& color) {
    return std::max(color.r, std::max(color.g, color.b)) -
           std::min(color.r, std::min(color.g, color.b));
}

glm::vec3 ApplyAlbedoTonePolicy(const glm::vec3& color,
                                const MaterialClassPolicy& policy,
                                bool& luminanceClamped,
                                bool& chromaClamped) {
    glm::vec3 result = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    float luma = AlbedoLuminance(result);
    if (luma > policy.albedoLuminanceCeiling && luma > kDefaultEpsilon) {
        result *= policy.albedoLuminanceCeiling / luma;
        luminanceClamped = true;
        luma = AlbedoLuminance(result);
    }

    const float chroma = AlbedoChromaSpan(result);
    if (chroma > policy.albedoChromaCeiling) {
        const float t = 1.0f - (policy.albedoChromaCeiling / std::max(chroma, kDefaultEpsilon));
        result = glm::mix(result, glm::vec3(luma), glm::clamp(t, 0.0f, 1.0f));
        chromaClamped = true;
    }
    return glm::clamp(result, glm::vec3(0.0f), glm::vec3(1.0f));
}

void ApplyMaterialClassPolicy(MaterialModel& model) {
    const SceneMaterialClassId sceneClassId = ResolveSceneMaterialClass(model);
    const uint32_t surfaceClassId = ResolvePolicySurfaceClass(sceneClassId);
    const MaterialClassPolicy policy = ResolveMaterialClassPolicy(model, sceneClassId, surfaceClassId);

    MaterialClassPolicyEvidence evidence{};
    evidence.surfaceClassId = surfaceClassId;
    evidence.sceneMaterialClassId = ToId(sceneClassId);
    evidence.reflectionPreferenceId = ToId(policy.reflectionPreference);
    evidence.temporalPolicyId = ToId(policy.temporalPolicy);
    evidence.postSensitivityId = ToId(policy.postSensitivity);
    evidence.applied = true;
    evidence.roughnessFloor = policy.roughnessFloor;
    evidence.normalScaleCeiling = policy.normalScaleCeiling;
    evidence.proceduralMaskCeiling = policy.proceduralMaskCeiling;
    evidence.reflectionStabilityScale = policy.reflectionStabilityScale;
    evidence.albedoLuminanceCeiling = policy.albedoLuminanceCeiling;
    evidence.albedoChromaCeiling = policy.albedoChromaCeiling;
    evidence.heightDetailStrength = policy.heightDetailStrength;
    evidence.thinTransmission = policy.thinTransmission;
    evidence.reflectionStabilityApplied = policy.reflectionStabilityApplied;

    const float originalRoughness = model.roughness;
    const float originalNormalScale = model.normalScale;
    const float originalProceduralMask = model.proceduralMaskStrength;
    const float originalTransmission = model.transmissionFactor;
    const glm::vec3 originalAlbedo = glm::vec3(model.albedo);

    model.roughness = std::max(model.roughness, policy.roughnessFloor);
    model.normalScale = std::min(model.normalScale, policy.normalScaleCeiling);
    model.heightDetailStrength = std::max(model.heightDetailStrength, policy.heightDetailStrength);
    model.proceduralMaskStrength =
        std::min(std::max(model.proceduralMaskStrength, model.heightDetailStrength),
                 policy.proceduralMaskCeiling);
    model.thinTransmissionFactor = std::max(model.thinTransmissionFactor, policy.thinTransmission);
    model.transmissionFactor = std::max(model.transmissionFactor, model.thinTransmissionFactor);
    bool albedoLuminanceClamped = false;
    bool albedoChromaClamped = false;
    if (sceneClassId != SceneMaterialClassId::EmissiveNeon &&
        sceneClassId != SceneMaterialClassId::ScreenPanel) {
        model.albedo = glm::vec4(
            ApplyAlbedoTonePolicy(originalAlbedo,
                                  policy,
                                  albedoLuminanceClamped,
                                  albedoChromaClamped),
            model.albedo.a);
    }
    if (policy.forceDielectric) {
        model.metallic = 0.0f;
    }

    evidence.roughnessFloorApplied = model.roughness > originalRoughness + kDefaultEpsilon;
    evidence.normalScaleClamped = model.normalScale + kDefaultEpsilon < originalNormalScale;
    evidence.proceduralMaskClamped =
        model.proceduralMaskStrength + kDefaultEpsilon < originalProceduralMask;
    evidence.heightDetailBoostApplied =
        model.heightDetailStrength > kDefaultEpsilon ||
        model.proceduralMaskStrength > originalProceduralMask + kDefaultEpsilon;
    evidence.thinTransmissionApplied =
        model.transmissionFactor > originalTransmission + kDefaultEpsilon;
    evidence.albedoLuminanceClamped = albedoLuminanceClamped;
    evidence.albedoChromaClamped = albedoChromaClamped;
    model.classPolicy = evidence;
}

[[nodiscard]] bool MaterialNormalMapsDisabledForDiagnostics() {
    const char* value = std::getenv("CORTEX_DISABLE_MATERIAL_NORMAL_MAPS");
    return value && value[0] != '\0' && value[0] != '0';
}

void ApplyPresetDefaults(MaterialModel& model, const MaterialPresetInfo& preset) {
    if (preset.hasDefaultMetallic && !model.textures.metallic &&
        IsDefaultScalar(model.metallic, 0.0f)) {
        model.metallic = glm::clamp(preset.defaultMetallic, 0.0f, 1.0f);
    }

    if (preset.hasDefaultRoughness && !model.textures.roughness &&
        IsDefaultScalar(model.roughness, 0.5f)) {
        model.roughness = glm::clamp(preset.defaultRoughness, 0.0f, 1.0f);
    }

    if (preset.hasDefaultTransmission && !model.textures.transmission &&
        IsDefaultScalar(model.transmissionFactor, 0.0f)) {
        model.transmissionFactor = glm::clamp(preset.defaultTransmission, 0.0f, 1.0f);
    }

    if (preset.emissive && !model.textures.emissive && IsNearBlack(model.emissiveColor)) {
        const glm::vec3 albedo = glm::clamp(glm::vec3(model.albedo), glm::vec3(0.0f), glm::vec3(1.0f));
        const float albedoLuma = glm::dot(albedo, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        model.emissiveColor = (albedoLuma > 0.05f) ? albedo : glm::vec3(1.0f);
    }

    if (preset.hasDefaultEmissiveStrength && preset.emissive &&
        model.emissiveStrength <= 1.0f + kDefaultEpsilon) {
        model.emissiveStrength = std::max(model.emissiveStrength, preset.defaultEmissiveStrength);
    }

    if (preset.hasDefaultSpecularFactor && !model.textures.specular &&
        IsDefaultScalar(model.specularFactor, 1.0f)) {
        model.specularFactor = glm::clamp(preset.defaultSpecularFactor, 0.0f, 2.0f);
    }

    if (preset.hasDefaultSpecularColor && !model.textures.specularColor &&
        IsNearWhite(model.specularColorFactor)) {
        model.specularColorFactor =
            glm::clamp(preset.defaultSpecularColorFactor, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    if (preset.transmissive && model.transmissionFactor > 0.0f) {
        model.metallic = 0.0f;
    }
}

} // namespace

FullSceneMaterialModelEvidence BuildFullSceneMaterialModelEvidence(
    const FrameContract::MaterialStats& materials,
    uint32_t shaderMaterialTableRowCount,
    bool gbufferPolicyChannelReady) {
    FullSceneMaterialModelEvidence evidence;
    evidence.sampledMaterialCount = materials.sampled;
    evidence.shaderMaterialTableRowCount = shaderMaterialTableRowCount;
    evidence.shaderMaterialPolicyColumnCount = 4u;
    evidence.policyAppliedCount = materials.materialClassPolicyApplied;
    evidence.unknownMaterialFamilyCount = materials.sceneMaterialDefault;
    evidence.descriptorMissingCount = materials.descriptorTablesMissingAfterPrepare;
    evidence.descriptorRefreshFailureCount = materials.descriptorRefreshFailures;
    evidence.validationErrorCount = materials.validationErrors;
    evidence.missingHeroTextureEvidenceCount =
        materials.descriptorTablesMissingAfterPrepare +
        (materials.resourcePrepareCalls == 0 ? materials.sampled : 0u);

    const uint32_t sceneFamilyCount =
        materials.sceneMaterialDefault +
        materials.sceneMaterialPaintedWall +
        materials.sceneMaterialCeramicTile +
        materials.sceneMaterialPolishedWood +
        materials.sceneMaterialBrushedMetal +
        materials.sceneMaterialPolishedMetal +
        materials.sceneMaterialGlassPane +
        materials.sceneMaterialFabric +
        materials.sceneMaterialPlastic +
        materials.sceneMaterialWetSurface +
        materials.sceneMaterialEmissiveNeon +
        materials.sceneMaterialScreenPanel +
        materials.sceneMaterialConcrete +
        materials.sceneMaterialRubber +
        materials.sceneMaterialWater +
        materials.sceneMaterialMirror;
    const uint32_t reflectionPolicyCount =
        materials.materialReflectionNeutralFallback +
        materials.materialReflectionLocalProbe +
        materials.materialReflectionProbeGrid +
        materials.materialReflectionPlanarProbe +
        materials.materialReflectionSSR +
        materials.materialReflectionRT;
    const uint32_t temporalPolicyCount =
        materials.materialTemporalStableDiffuse +
        materials.materialTemporalStableGlossy +
        materials.materialTemporalMirrorLocked +
        materials.materialTemporalEmissiveLocked +
        materials.materialTemporalWaterViewDependent;
    const uint32_t postPolicyCount =
        materials.materialPostNormal +
        materials.materialPostBloomEmitter +
        materials.materialPostExposureProtected +
        materials.materialPostWetHighlight;

    evidence.enabled = materials.sampled > 0;
    evidence.familyCountsAvailable =
        evidence.enabled && sceneFamilyCount == materials.sampled;
    evidence.reflectionPoliciesAvailable =
        evidence.enabled && reflectionPolicyCount == materials.sampled;
    evidence.temporalPoliciesAvailable =
        evidence.enabled && temporalPolicyCount == materials.sampled;
    evidence.postPoliciesAvailable =
        evidence.enabled && postPolicyCount == materials.sampled;
    evidence.textureEvidenceAvailable =
        evidence.enabled &&
        materials.resourcePrepareCalls > 0 &&
        materials.descriptorTablesMissingAfterPrepare == 0 &&
        materials.descriptorRefreshFailures == 0;
    evidence.shaderFeatureFlagsAvailable =
        evidence.enabled &&
        materials.advancedFeatureMaterials <= materials.sampled &&
        materials.materialClassPolicyApplied <= materials.sampled;
    evidence.runtimePolicyBridgeReady =
        evidence.enabled &&
        materials.materialClassPolicyApplied == materials.sampled &&
        evidence.familyCountsAvailable &&
        evidence.reflectionPoliciesAvailable &&
        evidence.temporalPoliciesAvailable &&
        evidence.postPoliciesAvailable &&
        evidence.shaderFeatureFlagsAvailable;
    evidence.shaderMaterialPolicyRowsReady =
        evidence.runtimePolicyBridgeReady &&
        evidence.shaderMaterialTableRowCount > 0 &&
        evidence.shaderMaterialTableRowCount <= evidence.sampledMaterialCount &&
        evidence.shaderMaterialPolicyColumnCount == 4u;
    evidence.shaderMaterialTableReady =
        evidence.shaderMaterialPolicyRowsReady &&
        materials.resourcePrepareCalls > 0 &&
        materials.descriptorTablesMissingAfterPrepare == 0 &&
        materials.descriptorRefreshFailures == 0;
    evidence.gbufferPolicyChannelBackedByMaterialTable =
        evidence.shaderMaterialTableReady &&
        gbufferPolicyChannelReady;
    evidence.fullSceneMaterialModelReady =
        evidence.runtimePolicyBridgeReady &&
        evidence.shaderMaterialTableReady &&
        evidence.gbufferPolicyChannelBackedByMaterialTable &&
        evidence.textureEvidenceAvailable &&
        evidence.unknownMaterialFamilyCount == 0 &&
        evidence.validationErrorCount == 0;

    if (!evidence.enabled) {
        evidence.failureReason = "No sampled runtime materials";
    } else if (!evidence.runtimePolicyBridgeReady) {
        evidence.failureReason =
            "Runtime material policy bridge does not cover every sampled material";
    } else if (!evidence.shaderMaterialTableReady) {
        evidence.failureReason =
            "Shader-facing FullSceneMaterialTable is not populated with complete policy rows";
    } else if (!evidence.gbufferPolicyChannelBackedByMaterialTable) {
        evidence.failureReason =
            "GBuffer material policy channel is not backed by the shader material table";
    } else if (!evidence.textureEvidenceAvailable) {
        evidence.failureReason =
            "Runtime material texture/descriptor evidence is incomplete";
    } else if (evidence.unknownMaterialFamilyCount > 0) {
        evidence.failureReason =
            "Some sampled materials still resolve to the default/unknown scene material family";
    } else if (evidence.validationErrorCount > 0) {
        evidence.failureReason = "Material validation errors are present";
    } else {
        evidence.failureReason = "FullSceneMaterialTable runtime bridge is ready";
    }

    return evidence;
}

MaterialModel MaterialResolver::ResolveRenderable(
    const Scene::RenderableComponent& renderable,
    const MaterialTextureFallbacks& fallbacks) {
    MaterialModel model{};
    model.albedo = renderable.albedoColor;
    model.metallic = glm::clamp(renderable.metallic, 0.0f, 1.0f);
    model.roughness = glm::clamp(renderable.roughness, 0.0f, 1.0f);
    model.ao = glm::clamp(renderable.ao, 0.0f, 1.0f);
    model.emissiveColor = glm::max(renderable.emissiveColor, glm::vec3(0.0f));
    model.emissiveStrength = std::max(renderable.emissiveStrength, 0.0f);
    model.occlusionStrength = glm::clamp(renderable.occlusionStrength, 0.0f, 1.0f);
    model.normalScale = std::max(renderable.normalScale, 0.0f);
    model.transmissionFactor = glm::clamp(renderable.transmissionFactor, 0.0f, 1.0f);
    model.ior = glm::clamp(renderable.ior, 1.0f, 2.5f);
    model.specularFactor = glm::clamp(renderable.specularFactor, 0.0f, 2.0f);
    model.specularColorFactor = glm::clamp(renderable.specularColorFactor, glm::vec3(0.0f), glm::vec3(1.0f));
    model.clearcoatFactor = 0.0f;
    model.clearcoatRoughnessFactor = 0.2f;
    model.sheenWeight = glm::clamp(renderable.sheenWeight, 0.0f, 1.0f);
    model.subsurfaceWrap = glm::clamp(renderable.subsurfaceWrap, 0.0f, 1.0f);
    model.anisotropyStrength = glm::clamp(renderable.anisotropyStrength, 0.0f, 1.0f);
    model.wetnessFactor = glm::clamp(renderable.wetnessFactor, 0.0f, 1.0f);
    model.emissiveBloomFactor = glm::clamp(renderable.emissiveBloomFactor, 0.0f, 1.0f);
    model.proceduralMaskStrength = glm::clamp(renderable.proceduralMaskStrength, 0.0f, 1.0f);
    model.alphaMode = static_cast<MaterialAlphaMode>(renderable.alphaMode);
    model.alphaCutoff = glm::clamp(renderable.alphaCutoff, 0.0f, 1.0f);
    model.doubleSided = renderable.doubleSided;
    model.presetName = renderable.presetName;

    model.textures.albedo = HasAuthoredTexture(
        renderable.textures.albedo, fallbacks.albedo, renderable.textures.albedoPath);
    model.textures.normal = HasAuthoredTexture(
        renderable.textures.normal, fallbacks.normal, renderable.textures.normalPath);
    if (MaterialNormalMapsDisabledForDiagnostics()) {
        model.textures.normal = false;
        model.normalScale = 0.0f;
    }
    model.textures.metallic = HasAuthoredTexture(
        renderable.textures.metallic, fallbacks.metallic, renderable.textures.metallicPath);
    model.textures.roughness = HasAuthoredTexture(
        renderable.textures.roughness, fallbacks.roughness, renderable.textures.roughnessPath);
    model.textures.occlusion = HasAuthoredTexture(
        renderable.textures.occlusion, nullptr, renderable.textures.occlusionPath);
    model.textures.emissive = HasAuthoredTexture(
        renderable.textures.emissive, nullptr, renderable.textures.emissivePath);
    model.textures.transmission = HasAuthoredTexture(
        renderable.textures.transmission, nullptr, renderable.textures.transmissionPath);
    model.textures.clearcoat = HasAuthoredTexture(
        renderable.textures.clearcoat, nullptr, renderable.textures.clearcoatPath);
    model.textures.clearcoatRoughness = HasAuthoredTexture(
        renderable.textures.clearcoatRoughness, nullptr, renderable.textures.clearcoatRoughnessPath);
    model.textures.specular = HasAuthoredTexture(
        renderable.textures.specular, nullptr, renderable.textures.specularPath);
    model.textures.specularColor = HasAuthoredTexture(
        renderable.textures.specularColor, nullptr, renderable.textures.specularColorPath);

    if (!model.presetName.empty()) {
        const MaterialPresetInfo preset = MaterialPresetRegistry::Resolve(model.presetName);
        model.materialType = preset.materialType;
        model.clearcoatFactor = preset.clearcoatFactor;
        model.clearcoatRoughnessFactor = preset.clearcoatRoughnessFactor;
        if (model.sheenWeight <= kDefaultEpsilon) {
            model.sheenWeight = preset.sheenWeight;
        }
        if (model.subsurfaceWrap <= kDefaultEpsilon) {
            model.subsurfaceWrap = preset.subsurfaceWrap;
        }
        ApplyPresetDefaults(model, preset);
    }

    if (renderable.clearcoatFactor > 0.0f || renderable.clearcoatRoughnessFactor > 0.0f) {
        model.clearcoatFactor = glm::clamp(renderable.clearcoatFactor, 0.0f, 1.0f);
        model.clearcoatRoughnessFactor = glm::clamp(renderable.clearcoatRoughnessFactor, 0.0f, 1.0f);
    }

    if (model.alphaMode == MaterialAlphaMode::Blend && model.transmissionFactor > 0.0f) {
        // Current transparent overlays composite after the opaque G-buffer and
        // do not publish matching material/normal data for post refraction.
        // Keep alpha-blended glass visually transparent through opacity and
        // glass surface classification, but keep deferred/post transmission
        // disabled until the renderer has an OIT/transparent-G-buffer path.
        model.transmissionFactor = 0.0f;
    }

    ApplyMaterialClassPolicy(model);

    return model;
}

std::vector<MaterialValidationIssue> MaterialResolver::Validate(const MaterialModel& model) {
    std::vector<MaterialValidationIssue> issues;
    if (model.alphaMode == MaterialAlphaMode::Blend && model.transmissionFactor > 0.0f) {
        issues.push_back({
            MaterialValidationIssue::Severity::Warning,
            "Blend alpha and transmission are both enabled; sorting and refraction may diverge across render paths."
        });
    }
    if (model.metallic > 0.0f && model.transmissionFactor > 0.0f) {
        issues.push_back({
            MaterialValidationIssue::Severity::Warning,
            "Metallic transmission is physically ambiguous; dielectric transmission is expected."
        });
    }
    if (model.roughness <= 0.02f && model.textures.normal) {
        issues.push_back({
            MaterialValidationIssue::Severity::Info,
            "Very low roughness with a normal map can shimmer without enough normal filtering."
        });
    }
    return issues;
}

MaterialConstants MaterialResolver::BuildMaterialConstants(const MaterialModel& model) {
    MaterialConstants material{};
    material.albedo = model.albedo;
    material.metallic = model.metallic;
    material.roughness = model.roughness;
    material.ao = model.ao;
    material._pad0 = (model.alphaMode == MaterialAlphaMode::Mask) ? model.alphaCutoff : 0.0f;
    material.mapFlags = glm::uvec4(
        model.textures.albedo ? 1u : 0u,
        model.textures.normal ? 1u : 0u,
        model.textures.metallic ? 1u : 0u,
        model.textures.roughness ? 1u : 0u);
    material.mapFlags2 = glm::uvec4(
        model.textures.occlusion ? 1u : 0u,
        model.textures.emissive ? 1u : 0u,
        0u,
        0u);
    material.emissiveFactorStrength = glm::vec4(model.emissiveColor, model.emissiveStrength);
    material.extraParams = glm::vec4(
        model.occlusionStrength,
        model.normalScale,
        model.anisotropyStrength,
        model.wetnessFactor);
    material.fractalParams1.w = model.materialType;
    material.coatParams = glm::vec4(
        model.clearcoatFactor,
        model.clearcoatRoughnessFactor,
        model.sheenWeight,
        model.subsurfaceWrap);
    material.transmissionParams = glm::vec4(
        model.transmissionFactor,
        model.ior,
        model.emissiveBloomFactor,
        model.proceduralMaskStrength);
    material.specularParams = glm::vec4(model.specularColorFactor, model.specularFactor);
    return material;
}

void MaterialResolver::FillMaterialTextureIndices(const Scene::RenderableComponent& renderable,
                                                  MaterialConstants& materialData) {
    uint32_t texIndices[MaterialGPUState::kSlotCount] = {};
    for (uint32_t i = 0; i < MaterialGPUState::kSlotCount; ++i) {
        texIndices[i] = kInvalidBindlessIndex;
    }

    uint32_t effectiveMapFlags[6] = {
        materialData.mapFlags.x,
        materialData.mapFlags.y,
        materialData.mapFlags.z,
        materialData.mapFlags.w,
        materialData.mapFlags2.x,
        materialData.mapFlags2.y
    };

    if (renderable.textures.gpuState) {
        for (int i = 0; i < 6; ++i) {
            const bool hasMap = (effectiveMapFlags[i] != 0u);
            if (hasMap && renderable.textures.gpuState->descriptors[i].IsValid()) {
                texIndices[i] = renderable.textures.gpuState->descriptors[i].index;
            } else {
                effectiveMapFlags[i] = 0u;
                texIndices[i] = kInvalidBindlessIndex;
            }
        }

        const bool hasTransmission = static_cast<bool>(renderable.textures.transmission);
        const bool hasClearcoat = static_cast<bool>(renderable.textures.clearcoat);
        const bool hasClearcoatRoughness = static_cast<bool>(renderable.textures.clearcoatRoughness);
        const bool hasSpecular = static_cast<bool>(renderable.textures.specular);
        const bool hasSpecularColor = static_cast<bool>(renderable.textures.specularColor);

        const auto& desc = renderable.textures.gpuState->descriptors;
        texIndices[6] = (hasTransmission && desc[6].IsValid()) ? desc[6].index : kInvalidBindlessIndex;
        texIndices[7] = (hasClearcoat && desc[7].IsValid()) ? desc[7].index : kInvalidBindlessIndex;
        texIndices[8] = (hasClearcoatRoughness && desc[8].IsValid()) ? desc[8].index : kInvalidBindlessIndex;
        texIndices[9] = (hasSpecular && desc[9].IsValid()) ? desc[9].index : kInvalidBindlessIndex;
        texIndices[10] = (hasSpecularColor && desc[10].IsValid()) ? desc[10].index : kInvalidBindlessIndex;
    } else {
        for (int i = 0; i < 6; ++i) {
            effectiveMapFlags[i] = 0u;
            texIndices[i] = kInvalidBindlessIndex;
        }
    }

    materialData.mapFlags = glm::uvec4(
        effectiveMapFlags[0],
        effectiveMapFlags[1],
        effectiveMapFlags[2],
        effectiveMapFlags[3]);
    materialData.mapFlags2 = glm::uvec4(
        effectiveMapFlags[4],
        effectiveMapFlags[5],
        0u,
        0u);

    materialData.textureIndices = glm::uvec4(
        texIndices[0],
        texIndices[1],
        texIndices[2],
        texIndices[3]);
    materialData.textureIndices2 = glm::uvec4(
        texIndices[4],
        texIndices[5],
        kInvalidBindlessIndex,
        kInvalidBindlessIndex);

    materialData.textureIndices3 = glm::uvec4(
        texIndices[6],
        texIndices[7],
        texIndices[8],
        texIndices[9]);
    materialData.textureIndices4 = glm::uvec4(
        texIndices[10],
        kInvalidBindlessIndex,
        kInvalidBindlessIndex,
        kInvalidBindlessIndex);
}

VBMaterialConstants MaterialResolver::BuildVBMaterialConstants(
    const MaterialModel& model,
    const glm::uvec4& textureIndices,
    const glm::uvec4& textureIndices2,
    const glm::uvec4& textureIndices3,
    const glm::uvec4& textureIndices4,
    uint32_t materialClass) {
    VBMaterialConstants material{};
    material.albedo = model.albedo;
    material.metallic = model.metallic;
    material.roughness = model.roughness;
    material.ao = model.ao;
    material.textureIndices = textureIndices;
    material.textureIndices2 = textureIndices2;
    material.textureIndices3 = textureIndices3;
    material.textureIndices4 = textureIndices4;
    material.emissiveFactorStrength = glm::vec4(model.emissiveColor, model.emissiveStrength);
    material.extraParams = glm::vec4(
        model.occlusionStrength,
        model.normalScale,
        model.anisotropyStrength,
        model.wetnessFactor);
    material.coatParams = glm::vec4(
        model.clearcoatFactor,
        model.clearcoatRoughnessFactor,
        model.sheenWeight,
        model.subsurfaceWrap);
    material.transmissionParams = glm::vec4(
        model.transmissionFactor,
        model.ior,
        model.emissiveBloomFactor,
        model.proceduralMaskStrength);
    material.specularParams = glm::vec4(model.specularColorFactor, model.specularFactor);
    material.alphaCutoff = model.alphaCutoff;
    material.alphaMode = static_cast<uint32_t>(model.alphaMode);
    material.doubleSided = model.doubleSided ? 1u : 0u;
    material.materialClass = materialClass;
    material.policyParams = glm::uvec4(
        model.classPolicy.sceneMaterialClassId,
        model.classPolicy.reflectionPreferenceId,
        model.classPolicy.temporalPolicyId,
        model.classPolicy.postSensitivityId);
    return material;
}

} // namespace Cortex::Graphics
