#pragma once

#include "Graphics/FrameContract.h"
#include "Graphics/ShaderTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Scene {
struct RenderableComponent;
}

namespace Cortex::Graphics {

class DX12Texture;
struct VBMaterialConstants;

struct MaterialTextureFallbacks {
    const DX12Texture* albedo = nullptr;
    const DX12Texture* normal = nullptr;
    const DX12Texture* metallic = nullptr;
    const DX12Texture* roughness = nullptr;
};

struct MaterialTexturePresence {
    bool albedo = false;
    bool normal = false;
    bool metallic = false;
    bool roughness = false;
    bool occlusion = false;
    bool emissive = false;
    bool transmission = false;
    bool clearcoat = false;
    bool clearcoatRoughness = false;
    bool specular = false;
    bool specularColor = false;
};

enum class SceneMaterialClassId : uint32_t {
    Default = 0u,
    PaintedWall = 1u,
    CeramicTile = 2u,
    PolishedWood = 3u,
    BrushedMetal = 4u,
    PolishedMetal = 5u,
    GlassPane = 6u,
    Fabric = 7u,
    Plastic = 8u,
    WetSurface = 9u,
    EmissiveNeon = 10u,
    ScreenPanel = 11u,
    Concrete = 12u,
    Rubber = 13u,
    Water = 14u,
    Mirror = 15u,
};

enum class MaterialReflectionPreferenceId : uint32_t {
    NeutralFallback = 0u,
    LocalProbe = 1u,
    ProbeGrid = 2u,
    PlanarProbe = 3u,
    SSR = 4u,
    RTReflection = 5u,
};

enum class MaterialTemporalPolicyId : uint32_t {
    StableDiffuse = 0u,
    StableGlossy = 1u,
    MirrorLocked = 2u,
    EmissiveLocked = 3u,
    WaterViewDependent = 4u,
};

enum class MaterialPostSensitivityId : uint32_t {
    Normal = 0u,
    BloomEmitter = 1u,
    ExposureProtected = 2u,
    WetHighlight = 3u,
};

struct MaterialClassPolicyEvidence {
    uint32_t surfaceClassId = 0;
    uint32_t sceneMaterialClassId = 0;
    uint32_t reflectionPreferenceId = 0;
    uint32_t temporalPolicyId = 0;
    uint32_t postSensitivityId = 0;
    bool applied = false;
    bool roughnessFloorApplied = false;
    bool normalScaleClamped = false;
    bool proceduralMaskClamped = false;
    bool reflectionStabilityApplied = false;
    bool albedoLuminanceClamped = false;
    bool albedoChromaClamped = false;
    float roughnessFloor = 0.0f;
    float normalScaleCeiling = 1.0f;
    float proceduralMaskCeiling = 1.0f;
    float reflectionStabilityScale = 1.0f;
    float albedoLuminanceCeiling = 1.0f;
    float albedoChromaCeiling = 1.0f;
};

enum class MaterialAlphaMode : uint32_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2,
};

struct MaterialModel {
    glm::vec4 albedo = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f);
    float emissiveStrength = 1.0f;
    float occlusionStrength = 1.0f;
    float normalScale = 1.0f;
    float transmissionFactor = 0.0f;
    float ior = 1.5f;
    float specularFactor = 1.0f;
    glm::vec3 specularColorFactor = glm::vec3(1.0f);
    float clearcoatFactor = 0.0f;
    float clearcoatRoughnessFactor = 0.0f;
    float sheenWeight = 0.0f;
    float subsurfaceWrap = 0.0f;
    float anisotropyStrength = 0.0f;
    float wetnessFactor = 0.0f;
    float emissiveBloomFactor = 0.0f;
    float proceduralMaskStrength = 0.0f;
    float materialType = 0.0f;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    std::string presetName;
    MaterialTexturePresence textures{};
    MaterialClassPolicyEvidence classPolicy{};
};

struct MaterialValidationIssue {
    enum class Severity : uint8_t {
        Info,
        Warning,
        Error,
    };

    Severity severity = Severity::Warning;
    std::string message;
};

struct FullSceneMaterialModelEvidence {
    bool enabled = false;
    bool runtimePolicyBridgeReady = false;
    bool familyCountsAvailable = false;
    bool reflectionPoliciesAvailable = false;
    bool temporalPoliciesAvailable = false;
    bool postPoliciesAvailable = false;
    bool textureEvidenceAvailable = false;
    bool shaderFeatureFlagsAvailable = false;
    bool fullSceneMaterialModelReady = false;
    uint32_t sampledMaterialCount = 0;
    uint32_t policyAppliedCount = 0;
    uint32_t unknownMaterialFamilyCount = 0;
    uint32_t missingHeroTextureEvidenceCount = 0;
    uint32_t descriptorMissingCount = 0;
    uint32_t descriptorRefreshFailureCount = 0;
    uint32_t validationErrorCount = 0;
    std::string owner = "MaterialResolver/FullSceneMaterialModelEvidence";
    std::string failureReason = "No sampled runtime materials";
};

[[nodiscard]] FullSceneMaterialModelEvidence BuildFullSceneMaterialModelEvidence(
    const FrameContract::MaterialStats& materials);

class MaterialResolver {
public:
    [[nodiscard]] static MaterialModel ResolveRenderable(
        const Scene::RenderableComponent& renderable,
        const MaterialTextureFallbacks& fallbacks);

    [[nodiscard]] static std::vector<MaterialValidationIssue> Validate(const MaterialModel& model);

    [[nodiscard]] static MaterialConstants BuildMaterialConstants(const MaterialModel& model);

    static void FillMaterialTextureIndices(const Scene::RenderableComponent& renderable,
                                           MaterialConstants& materialData);

    [[nodiscard]] static VBMaterialConstants BuildVBMaterialConstants(
        const MaterialModel& model,
        const glm::uvec4& textureIndices,
        const glm::uvec4& textureIndices2,
        const glm::uvec4& textureIndices3,
        const glm::uvec4& textureIndices4,
        uint32_t materialClass);
};

} // namespace Cortex::Graphics
