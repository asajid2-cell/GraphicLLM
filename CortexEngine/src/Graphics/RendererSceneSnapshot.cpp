#include "Graphics/RendererSceneSnapshot.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace Cortex::Graphics {

namespace {

[[nodiscard]] float EstimateDielectricFresnel(const MaterialModel& material) {
    const float ior = std::max(material.ior, 1.0f);
    const float numerator = ior - 1.0f;
    const float denominator = std::max(ior + 1.0f, 1.0e-4f);
    const float f0 = (numerator / denominator) * (numerator / denominator);
    return std::clamp(f0, 0.0f, 1.0f);
}

[[nodiscard]] float EstimateReflectionCeiling(SurfaceClass surfaceClass,
                                              const MaterialModel& material) {
    const float roughness = std::clamp(material.roughness, 0.0f, 1.0f);
    const float smooth = std::clamp(1.0f - roughness, 0.0f, 1.0f);
    const float conductor = std::clamp(material.metallic, 0.0f, 1.0f);
    const float transmission = std::clamp(material.transmissionFactor, 0.0f, 1.0f);
    const float dielectricFresnel = EstimateDielectricFresnel(material);

    switch (surfaceClass) {
    case SurfaceClass::Mirror:
        return 0.68f + (0.88f - 0.68f) * smooth;
    case SurfaceClass::Water:
        return 0.52f + (0.74f - 0.52f) * smooth;
    case SurfaceClass::Glass: {
        const float glassReflectance = std::max(transmission, dielectricFresnel);
        const float glassBase = 0.18f + (0.50f - 0.18f) * glassReflectance;
        const float smoothScale = 0.70f + (1.0f - 0.70f) * smooth;
        return glassBase * smoothScale;
    }
    case SurfaceClass::BrushedMetal: {
        const float metalBase = 0.24f + (0.56f - 0.24f) * smooth;
        const float conductorScale = 0.70f + (1.0f - 0.70f) * conductor;
        return metalBase * conductorScale;
    }
    case SurfaceClass::Plastic:
        return 0.10f + (0.22f - 0.10f) * smooth;
    case SurfaceClass::Masonry:
    case SurfaceClass::Wood:
        return 0.06f + (0.12f - 0.06f) * smooth;
    case SurfaceClass::Default:
    case SurfaceClass::Emissive:
    default:
        break;
    }

    if (conductor > 0.85f) {
        return 0.22f + (0.48f - 0.22f) * smooth;
    }
    return 0.08f + (0.16f - 0.08f) * smooth;
}

[[nodiscard]] bool IsReflectionEligible(SurfaceClass surfaceClass,
                                        const MaterialModel& material,
                                        float ceilingEstimate) {
    if (surfaceClass == SurfaceClass::Mirror ||
        surfaceClass == SurfaceClass::Water ||
        surfaceClass == SurfaceClass::Glass ||
        surfaceClass == SurfaceClass::BrushedMetal) {
        return true;
    }
    return material.metallic > 0.85f ||
           material.transmissionFactor > 0.01f ||
           ceilingEstimate > 0.22f;
}

} // namespace

void RendererSceneSnapshot::Clear() {
    frameNumber = 0;
    renderableEntityCount = 0;
    renderables = {};
    materials = {};
    entries.clear();
    depthWritingIndices.clear();
    overlayIndices.clear();
    waterIndices.clear();
    transparentIndices.clear();
    rtCandidateIndices.clear();
}

bool RendererSceneSnapshot::IsValidForFrame(uint64_t frame) const {
    return frameNumber == frame;
}

RendererSceneSnapshot BuildRendererSceneSnapshot(Scene::ECS_Registry* registry,
                                                 uint64_t frameNumber) {
    RendererSceneSnapshot snapshot{};
    snapshot.frameNumber = frameNumber;
    if (!registry) {
        return snapshot;
    }

    auto view = registry->View<Scene::RenderableComponent>();
    std::vector<entt::entity> entities;
    for (auto entity : view) {
        entities.push_back(entity);
    }
    std::sort(entities.begin(),
              entities.end(),
              [](entt::entity a, entt::entity b) {
                  return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
              });

    snapshot.renderableEntityCount = static_cast<uint32_t>(entities.size());
    snapshot.entries.reserve(entities.size());
    snapshot.depthWritingIndices.reserve(entities.size());

    double albedoLumSum = 0.0;
    double roughnessSum = 0.0;
    double metallicSum = 0.0;
    double reflectionCeilingSum = 0.0;

    for (auto entity : entities) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        Scene::TransformComponent* transform = nullptr;
        if (registry->HasComponent<Scene::TransformComponent>(entity)) {
            transform = &registry->GetComponent<Scene::TransformComponent>(entity);
        }

        RendererSceneRenderable entry{};
        entry.entity = entity;
        entry.renderable = &renderable;
        entry.transform = transform;
        entry.visible = renderable.visible;
        entry.hasMesh = static_cast<bool>(renderable.mesh);
        entry.hasTransform = transform != nullptr;
        entry.depthClass = ClassifyRenderableDepth(registry, entity, renderable);
        if (transform) {
            entry.worldMatrix = transform->GetMatrix();
            entry.normalMatrix = transform->GetNormalMatrix();
        }
        if (renderable.mesh && renderable.mesh->gpuBuffers) {
            const auto& gpu = *renderable.mesh->gpuBuffers;
            entry.hasGpuBuffers = gpu.vertexBuffer && gpu.indexBuffer;
            entry.hasRawMeshSrvs =
                gpu.vbRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex &&
                gpu.ibRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex;
        }

        ++snapshot.renderables.total;
        if (!renderable.visible) {
            ++snapshot.renderables.invisible;
            snapshot.entries.push_back(entry);
            continue;
        }

        ++snapshot.renderables.visible;
        if (!renderable.mesh) {
            ++snapshot.renderables.meshless;
            snapshot.entries.push_back(entry);
            continue;
        }

        if (entry.depthClass == RenderableDepthClass::WaterDepthTestedNoWrite) {
            ++snapshot.renderables.waterDepthTestedNoWrite;
        } else if (entry.depthClass == RenderableDepthClass::OverlayDepthTestedNoWrite) {
            ++snapshot.renderables.overlay;
        } else if (entry.depthClass == RenderableDepthClass::TransparentDepthTested) {
            ++snapshot.renderables.transparentDepthTested;
        } else if (entry.depthClass == RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting) {
            ++snapshot.renderables.doubleSidedAlphaTestedDepthWriting;
        } else if (entry.depthClass == RenderableDepthClass::AlphaTestedDepthWriting) {
            ++snapshot.renderables.alphaTestedDepthWriting;
        } else if (entry.depthClass == RenderableDepthClass::DoubleSidedOpaqueDepthWriting) {
            ++snapshot.renderables.doubleSidedOpaqueDepthWriting;
        } else if (entry.depthClass == RenderableDepthClass::OpaqueDepthWriting) {
            ++snapshot.renderables.opaqueDepthWriting;
        }

        const bool hasEmissive =
            renderable.emissiveStrength > 0.0f &&
            (glm::length(renderable.emissiveColor) > 0.001f || renderable.textures.emissive);
        if (hasEmissive ||
            SurfacePresetContains(renderable, "emissive") ||
            SurfacePresetContains(renderable, "neon")) {
            ++snapshot.renderables.emissive;
        }
        if (renderable.metallic > 0.5f ||
            SurfacePresetContains(renderable, "metal") ||
            SurfacePresetContains(renderable, "chrome") ||
            SurfacePresetContains(renderable, "gold")) {
            ++snapshot.renderables.metallic;
        }
        if (renderable.transmissionFactor > 0.001f ||
            SurfacePresetContains(renderable, "glass")) {
            ++snapshot.renderables.transmissive;
        }
        if (renderable.clearcoatFactor > 0.001f) {
            ++snapshot.renderables.clearcoat;
        }

        const MaterialModel materialModel =
            MaterialResolver::ResolveRenderable(renderable, MaterialTextureFallbacks{});
        const glm::vec3 resolvedAlbedo = glm::vec3(materialModel.albedo);
        const float albedoLuminance =
            glm::dot(resolvedAlbedo, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        const float emissiveLuminance =
            glm::dot(materialModel.emissiveColor, glm::vec3(0.2126f, 0.7152f, 0.0722f)) *
            std::max(materialModel.emissiveStrength, 0.0f);
        const uint32_t sampleIndex = snapshot.materials.sampled++;
        if (sampleIndex == 0) {
            snapshot.materials.minAlbedoLuminance = albedoLuminance;
            snapshot.materials.maxAlbedoLuminance = albedoLuminance;
            snapshot.materials.minRoughness = materialModel.roughness;
            snapshot.materials.maxRoughness = materialModel.roughness;
            snapshot.materials.minMetallic = materialModel.metallic;
            snapshot.materials.maxMetallic = materialModel.metallic;
        } else {
            snapshot.materials.minAlbedoLuminance =
                std::min(snapshot.materials.minAlbedoLuminance, albedoLuminance);
            snapshot.materials.maxAlbedoLuminance =
                std::max(snapshot.materials.maxAlbedoLuminance, albedoLuminance);
            snapshot.materials.minRoughness =
                std::min(snapshot.materials.minRoughness, materialModel.roughness);
            snapshot.materials.maxRoughness =
                std::max(snapshot.materials.maxRoughness, materialModel.roughness);
            snapshot.materials.minMetallic =
                std::min(snapshot.materials.minMetallic, materialModel.metallic);
            snapshot.materials.maxMetallic =
                std::max(snapshot.materials.maxMetallic, materialModel.metallic);
        }

        albedoLumSum += albedoLuminance;
        roughnessSum += materialModel.roughness;
        metallicSum += materialModel.metallic;

        const bool resolvedEmissive = emissiveLuminance > 0.001f || materialModel.textures.emissive;
        if (albedoLuminance < 0.08f && materialModel.albedo.a >= 0.95f && !resolvedEmissive) {
            ++snapshot.materials.veryDarkAlbedo;
        }
        if (albedoLuminance > 1.0f && !resolvedEmissive) {
            ++snapshot.materials.veryBrightAlbedo;
        }
        if (renderable.roughness < 0.0f || renderable.roughness > 1.0f) {
            ++snapshot.materials.roughnessOutOfRange;
        }
        if (renderable.metallic < 0.0f || renderable.metallic > 1.0f) {
            ++snapshot.materials.metallicOutOfRange;
        }
        if (renderable.alphaMode == Scene::RenderableComponent::AlphaMode::Blend) {
            ++snapshot.materials.alphaBlend;
        } else if (renderable.alphaMode == Scene::RenderableComponent::AlphaMode::Mask) {
            ++snapshot.materials.alphaMask;
        }

        if (!renderable.presetName.empty()) {
            ++snapshot.materials.presetNamed;
            constexpr float kDefaultEpsilon = 1.0e-4f;
            if (!materialModel.textures.metallic &&
                std::abs(renderable.metallic - materialModel.metallic) > kDefaultEpsilon) {
                ++snapshot.materials.presetDefaultMetallic;
            }
            if (!materialModel.textures.roughness &&
                std::abs(renderable.roughness - materialModel.roughness) > kDefaultEpsilon) {
                ++snapshot.materials.presetDefaultRoughness;
            }
            if (!materialModel.textures.transmission &&
                std::abs(renderable.transmissionFactor - materialModel.transmissionFactor) > kDefaultEpsilon) {
                ++snapshot.materials.presetDefaultTransmission;
            }
            if (!materialModel.textures.emissive && resolvedEmissive &&
                !hasEmissive) {
                ++snapshot.materials.presetDefaultEmission;
            }
        }

        if (materialModel.metallic > 0.5f) {
            ++snapshot.materials.resolvedMetallic;
        }
        if (materialModel.metallic > 0.85f && materialModel.transmissionFactor <= 0.01f) {
            ++snapshot.materials.resolvedConductor;
        }
        if (materialModel.transmissionFactor > 0.01f) {
            ++snapshot.materials.resolvedTransmissive;
        }
        if (resolvedEmissive) {
            ++snapshot.materials.resolvedEmissive;
        }
        if (materialModel.clearcoatFactor > 0.001f) {
            ++snapshot.materials.resolvedClearcoat;
        }

        const std::vector<MaterialValidationIssue> materialIssues =
            MaterialResolver::Validate(materialModel);
        snapshot.materials.validationIssues += static_cast<uint32_t>(materialIssues.size());
        for (const MaterialValidationIssue& issue : materialIssues) {
            switch (issue.severity) {
            case MaterialValidationIssue::Severity::Error:
                ++snapshot.materials.validationErrors;
                break;
            case MaterialValidationIssue::Severity::Warning:
                ++snapshot.materials.validationWarnings;
                break;
            case MaterialValidationIssue::Severity::Info:
            default:
                break;
            }
        }
        if (materialModel.alphaMode == MaterialAlphaMode::Blend &&
            materialModel.transmissionFactor > 0.0f) {
            ++snapshot.materials.blendTransmission;
        }
        if (materialModel.metallic > 0.0f && materialModel.transmissionFactor > 0.0f) {
            ++snapshot.materials.metallicTransmission;
        }
        if (materialModel.roughness <= 0.02f && materialModel.textures.normal) {
            ++snapshot.materials.lowRoughnessNormal;
        }

        const SurfaceClass surfaceClass = ClassifySurface(materialModel);
        const float reflectionCeiling = EstimateReflectionCeiling(surfaceClass, materialModel);
        if (IsReflectionEligible(surfaceClass, materialModel, reflectionCeiling)) {
            ++snapshot.materials.reflectionEligible;
            reflectionCeilingSum += reflectionCeiling;
            snapshot.materials.maxReflectionCeilingEstimate =
                std::max(snapshot.materials.maxReflectionCeilingEstimate, reflectionCeiling);
            if (reflectionCeiling >= 0.45f) {
                ++snapshot.materials.reflectionHighCeiling;
            }
            if (surfaceClass == SurfaceClass::Mirror) {
                ++snapshot.materials.reflectionMirror;
            }
            if (surfaceClass == SurfaceClass::BrushedMetal || materialModel.metallic > 0.85f) {
                ++snapshot.materials.reflectionConductor;
            }
            if (surfaceClass == SurfaceClass::Glass || materialModel.transmissionFactor > 0.01f) {
                ++snapshot.materials.reflectionTransmissive;
            }
            if (surfaceClass == SurfaceClass::Water) {
                ++snapshot.materials.reflectionWater;
            }
        }

        switch (surfaceClass) {
        case SurfaceClass::Glass:
            ++snapshot.materials.surfaceGlass;
            break;
        case SurfaceClass::Mirror:
            ++snapshot.materials.surfaceMirror;
            break;
        case SurfaceClass::Plastic:
            ++snapshot.materials.surfacePlastic;
            break;
        case SurfaceClass::Masonry:
            ++snapshot.materials.surfaceMasonry;
            break;
        case SurfaceClass::Emissive:
            ++snapshot.materials.surfaceEmissive;
            break;
        case SurfaceClass::BrushedMetal:
            ++snapshot.materials.surfaceBrushedMetal;
            break;
        case SurfaceClass::Wood:
            ++snapshot.materials.surfaceWood;
            break;
        case SurfaceClass::Water:
            ++snapshot.materials.surfaceWater;
            break;
        case SurfaceClass::Default:
        default:
            ++snapshot.materials.surfaceDefault;
            break;
        }

        const uint32_t entryIndex = static_cast<uint32_t>(snapshot.entries.size());
        if (WritesSceneDepth(entry.depthClass) && entry.hasTransform) {
            snapshot.depthWritingIndices.push_back(entryIndex);
        } else if (entry.depthClass == RenderableDepthClass::OverlayDepthTestedNoWrite && entry.hasTransform) {
            snapshot.overlayIndices.push_back(entryIndex);
        } else if (entry.depthClass == RenderableDepthClass::WaterDepthTestedNoWrite && entry.hasTransform) {
            snapshot.waterIndices.push_back(entryIndex);
        } else if (entry.depthClass == RenderableDepthClass::TransparentDepthTested && entry.hasTransform) {
            snapshot.transparentIndices.push_back(entryIndex);
        }
        if (entry.visible && entry.hasMesh && entry.hasTransform) {
            snapshot.rtCandidateIndices.push_back(entryIndex);
        }

        snapshot.entries.push_back(entry);
    }

    if (snapshot.materials.sampled > 0) {
        const double invCount = 1.0 / static_cast<double>(snapshot.materials.sampled);
        snapshot.materials.avgAlbedoLuminance = static_cast<float>(albedoLumSum * invCount);
        snapshot.materials.avgRoughness = static_cast<float>(roughnessSum * invCount);
        snapshot.materials.avgMetallic = static_cast<float>(metallicSum * invCount);
    }
    if (snapshot.materials.reflectionEligible > 0) {
        const double invReflectionCount =
            1.0 / static_cast<double>(snapshot.materials.reflectionEligible);
        snapshot.materials.avgReflectionCeilingEstimate =
            static_cast<float>(reflectionCeilingSum * invReflectionCount);
    }

    return snapshot;
}

} // namespace Cortex::Graphics
