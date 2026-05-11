#pragma once

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialPresetRegistry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstdint>

namespace Cortex::Graphics {

enum class SurfaceClass : uint32_t {
    Default      = 0u,
    Glass        = 1u,
    Mirror       = 2u,
    Plastic      = 3u,
    Masonry      = 4u,
    Emissive     = 5u,
    BrushedMetal = 6u,
    Wood         = 7u,
    Water        = 8u,
};

[[nodiscard]] inline uint32_t ToSurfaceClassId(SurfaceClass surfaceClass) {
    return static_cast<uint32_t>(surfaceClass);
}

[[nodiscard]] inline bool SurfacePresetContains(const Scene::RenderableComponent& renderable,
                                                const char* token) {
    return token && MaterialPresetRegistry::ContainsToken(renderable.presetName, token);
}

[[nodiscard]] inline bool SurfacePresetContains(const MaterialModel& material,
                                                const char* token) {
    return token && MaterialPresetRegistry::ContainsToken(material.presetName, token);
}

[[nodiscard]] inline bool SurfaceMaterialTypeNear(const MaterialModel& material, float expectedType) {
    return material.materialType > expectedType - 0.5f && material.materialType < expectedType + 0.5f;
}

[[nodiscard]] inline SurfaceClass ClassifySurface(const MaterialModel& material) {
    const float emissiveLuminance =
        glm::dot(material.emissiveColor, glm::vec3(0.2126f, 0.7152f, 0.0722f)) *
        std::max(material.emissiveStrength, 0.0f);

    if (SurfacePresetContains(material, "water")) {
        return SurfaceClass::Water;
    }
    if (material.transmissionFactor > 0.01f ||
        SurfaceMaterialTypeNear(material, 1.0f) ||
        SurfacePresetContains(material, "glass")) {
        return SurfaceClass::Glass;
    }
    if (SurfaceMaterialTypeNear(material, 2.0f) ||
        SurfacePresetContains(material, "mirror")) {
        return SurfaceClass::Mirror;
    }
    if (SurfaceMaterialTypeNear(material, 6.0f) ||
        SurfacePresetContains(material, "brushed_metal") ||
        SurfacePresetContains(material, "polished_metal") ||
        SurfacePresetContains(material, "chrome") ||
        SurfacePresetContains(material, "gold")) {
        return SurfaceClass::BrushedMetal;
    }
    if (SurfaceMaterialTypeNear(material, 5.0f) ||
        SurfacePresetContains(material, "emissive") ||
        SurfacePresetContains(material, "neon") ||
        emissiveLuminance > 0.05f) {
        return SurfaceClass::Emissive;
    }
    if (SurfaceMaterialTypeNear(material, 3.0f) ||
        SurfacePresetContains(material, "plastic")) {
        return SurfaceClass::Plastic;
    }
    if (SurfaceMaterialTypeNear(material, 4.0f) ||
        SurfacePresetContains(material, "brick") ||
        SurfacePresetContains(material, "concrete") ||
        SurfacePresetContains(material, "stone") ||
        SurfacePresetContains(material, "masonry")) {
        return SurfaceClass::Masonry;
    }
    if (SurfaceMaterialTypeNear(material, 7.0f) ||
        SurfacePresetContains(material, "wood")) {
        return SurfaceClass::Wood;
    }
    if (material.metallic > 0.85f && material.roughness < 0.20f) {
        return SurfaceClass::Mirror;
    }
    return SurfaceClass::Default;
}

[[nodiscard]] inline SurfaceClass ClassifySurface(const Scene::RenderableComponent& renderable) {
    if (SurfacePresetContains(renderable, "water")) {
        return SurfaceClass::Water;
    }
    if (renderable.transmissionFactor > 0.01f || SurfacePresetContains(renderable, "glass")) {
        return SurfaceClass::Glass;
    }
    if (SurfacePresetContains(renderable, "mirror")) {
        return SurfaceClass::Mirror;
    }
    if (SurfacePresetContains(renderable, "brushed_metal")) {
        return SurfaceClass::BrushedMetal;
    }
    if (SurfacePresetContains(renderable, "polished_metal") ||
        SurfacePresetContains(renderable, "chrome") ||
        SurfacePresetContains(renderable, "gold")) {
        return SurfaceClass::BrushedMetal;
    }
    if (SurfacePresetContains(renderable, "emissive") ||
        SurfacePresetContains(renderable, "neon") ||
        SurfacePresetContains(renderable, "light")) {
        return SurfaceClass::Emissive;
    }
    if (SurfacePresetContains(renderable, "plastic")) {
        return SurfaceClass::Plastic;
    }
    if (SurfacePresetContains(renderable, "brick") ||
        SurfacePresetContains(renderable, "concrete") ||
        SurfacePresetContains(renderable, "stone") ||
        SurfacePresetContains(renderable, "masonry")) {
        return SurfaceClass::Masonry;
    }
    if (SurfacePresetContains(renderable, "wood")) {
        return SurfaceClass::Wood;
    }
    if (renderable.metallic > 0.85f && renderable.roughness < 0.20f) {
        return SurfaceClass::Mirror;
    }
    return SurfaceClass::Default;
}

[[nodiscard]] inline uint32_t ClassifyMaterialSurface(const Scene::RenderableComponent& renderable) {
    return ToSurfaceClassId(ClassifySurface(renderable));
}

[[nodiscard]] inline uint32_t ClassifyMaterialSurface(const MaterialModel& material) {
    return ToSurfaceClassId(ClassifySurface(material));
}

} // namespace Cortex::Graphics
