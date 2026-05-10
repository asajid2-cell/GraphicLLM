#include "Graphics/MaterialModel.h"

#include "Graphics/MaterialPresetRegistry.h"
#include "Graphics/VisibilityBuffer.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

namespace {

constexpr float kDefaultEpsilon = 1.0e-4f;

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
    model.alphaMode = static_cast<MaterialAlphaMode>(renderable.alphaMode);
    model.alphaCutoff = glm::clamp(renderable.alphaCutoff, 0.0f, 1.0f);
    model.doubleSided = renderable.doubleSided;
    model.presetName = renderable.presetName;

    model.textures.albedo = HasAuthoredTexture(
        renderable.textures.albedo, fallbacks.albedo, renderable.textures.albedoPath);
    model.textures.normal = HasAuthoredTexture(
        renderable.textures.normal, fallbacks.normal, renderable.textures.normalPath);
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
        model.sheenWeight = preset.sheenWeight;
        model.subsurfaceWrap = preset.subsurfaceWrap;
        ApplyPresetDefaults(model, preset);
    }

    if (renderable.clearcoatFactor > 0.0f || renderable.clearcoatRoughnessFactor > 0.0f) {
        model.clearcoatFactor = glm::clamp(renderable.clearcoatFactor, 0.0f, 1.0f);
        model.clearcoatRoughnessFactor = glm::clamp(renderable.clearcoatRoughnessFactor, 0.0f, 1.0f);
    }

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
    material.extraParams = glm::vec4(model.occlusionStrength, model.normalScale, 0.0f, 0.0f);
    material.fractalParams1.w = model.materialType;
    material.coatParams = glm::vec4(
        model.clearcoatFactor,
        model.clearcoatRoughnessFactor,
        model.sheenWeight,
        model.subsurfaceWrap);
    material.transmissionParams = glm::vec4(model.transmissionFactor, model.ior, 0.0f, 0.0f);
    material.specularParams = glm::vec4(model.specularColorFactor, model.specularFactor);
    return material;
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
    material.extraParams = glm::vec4(model.occlusionStrength, model.normalScale, 0.0f, 0.0f);
    material.coatParams = glm::vec4(
        model.clearcoatFactor,
        model.clearcoatRoughnessFactor,
        model.sheenWeight,
        model.subsurfaceWrap);
    material.transmissionParams = glm::vec4(model.transmissionFactor, model.ior, 0.0f, 0.0f);
    material.specularParams = glm::vec4(model.specularColorFactor, model.specularFactor);
    material.alphaCutoff = model.alphaCutoff;
    material.alphaMode = static_cast<uint32_t>(model.alphaMode);
    material.doubleSided = model.doubleSided ? 1u : 0u;
    material.materialClass = materialClass;
    return material;
}

} // namespace Cortex::Graphics
