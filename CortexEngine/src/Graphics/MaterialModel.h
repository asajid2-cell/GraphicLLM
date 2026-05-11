#pragma once

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
