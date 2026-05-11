#include "Graphics/MaterialPresetRegistry.h"

#include <algorithm>
#include <cctype>

namespace Cortex::Graphics {

const std::vector<MaterialPresetDescriptor>& MaterialPresetRegistry::CanonicalPresets() {
    static const std::vector<MaterialPresetDescriptor> presets = {
        {"chrome", "Chrome", "brushed_metal", true, true},
        {"polished_metal", "Polished Metal", "brushed_metal", true, true},
        {"brushed_metal", "Brushed Metal", "brushed_metal", true, true},
        {"plastic", "Plastic", "plastic", false, true},
        {"painted_plastic", "Painted Plastic", "plastic", true, true},
        {"matte", "Matte Ceramic", "default", false, true},
        {"brick", "Brick", "masonry", false, true},
        {"concrete", "Concrete", "masonry", false, true},
        {"wood_floor", "Wood Floor", "wood", true, true},
        {"backdrop", "Backdrop", "default", false, true},
        {"glass", "Glass", "glass", true, true},
        {"glass_panel", "Glass Panel", "glass", true, true},
        {"mirror", "Mirror", "mirror", true, true},
        {"water", "Water", "water", true, true},
        {"emissive_panel", "Emissive Panel", "emissive", true, true},
        {"skin", "Skin", "default", true, true},
        {"skin_ish", "Skin-ish Wax", "default", true, true},
        {"cloth", "Cloth", "default", true, true},
        {"velvet", "Velvet", "default", true, true},
        {"neon_tube", "Neon Tube", "emissive", true, true},
        {"brushed_gold", "Brushed Gold", "brushed_metal", true, true},
        {"wet_stone", "Wet Stone", "masonry", true, true},
        {"anisotropic_car_paint", "Anisotropic Car Paint", "plastic", true, true},
        {"procedural_marble", "Procedural Marble", "masonry", true, true},
    };
    return presets;
}

std::string MaterialPresetRegistry::Normalize(std::string_view presetName) {
    std::string value(presetName);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool MaterialPresetRegistry::ContainsToken(std::string_view presetName, std::string_view token) {
    if (presetName.empty() || token.empty()) {
        return false;
    }
    return Normalize(presetName).find(token) != std::string::npos;
}

MaterialPresetInfo MaterialPresetRegistry::Resolve(std::string_view presetName) {
    MaterialPresetInfo info{};
    if (presetName.empty()) {
        return info;
    }

    const std::string presetLower = Normalize(presetName);
    auto contains = [&](std::string_view token) {
        return presetLower.find(token) != std::string::npos;
    };

    if (contains("glass")) {
        info.materialType = 1.0f;
        info.transmissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.02f;
        info.hasDefaultTransmission = true;
        info.defaultTransmission = 0.85f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.25f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.92f, 0.97f, 1.0f);
    } else if (contains("mirror")) {
        info.materialType = 2.0f;
        info.metallic = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.02f;
    } else if (contains("plastic") || contains("car_paint")) {
        info.materialType = 3.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("car_paint") ? 0.18f : 0.35f;
    } else if (contains("brick") || contains("concrete") || contains("stone") || contains("marble")) {
        info.materialType = 4.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("wet_stone") ? 0.24f : (contains("marble") ? 0.22f : 0.78f);
    } else if (contains("brushed_metal") || contains("brushed_gold")) {
        info.materialType = 6.0f;
        info.metallic = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("gold") ? 0.20f : 0.32f;
    } else if (contains("wood_floor") || contains("wood")) {
        info.materialType = 7.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.46f;
    } else if (contains("emissive") || contains("neon") || contains("light")) {
        info.materialType = 5.0f;
        info.emissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.30f;
        info.hasDefaultEmissiveStrength = true;
        info.defaultEmissiveStrength = contains("neon") ? 5.0f : 3.0f;
    } else if (contains("water")) {
        info.transmissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.03f;
        info.hasDefaultTransmission = true;
        info.defaultTransmission = 0.55f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.35f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.75f, 0.92f, 1.0f);
    }

    if (contains("painted_plastic") || contains("plastic")) {
        info.clearcoat = true;
        info.clearcoatFactor = 1.0f;
        info.clearcoatRoughnessFactor = 0.15f;
    } else if (contains("polished_metal") || contains("chrome")) {
        if (info.materialType == 0.0f) {
            info.materialType = 6.0f;
        }
        info.metallic = true;
        info.clearcoat = true;
        info.clearcoatFactor = 0.6f;
        info.clearcoatRoughnessFactor = 0.08f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("chrome") ? 0.08f : 0.12f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.15f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = contains("gold")
            ? glm::vec3(1.0f, 0.82f, 0.42f)
            : glm::vec3(0.88f, 0.90f, 0.96f);
    }

    if (contains("metal") || contains("gold")) {
        if (info.materialType == 0.0f) {
            info.materialType = 6.0f;
        }
        info.metallic = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        if (!info.hasDefaultRoughness) {
            info.hasDefaultRoughness = true;
            info.defaultRoughness = contains("gold") ? 0.18f : 0.24f;
        }
    }

    if (contains("cloth") || contains("velvet")) {
        info.clearcoat = false;
        info.clearcoatFactor = 0.0f;
        info.sheenWeight = 1.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.82f;
    }

    if (contains("car_paint")) {
        info.clearcoat = true;
        info.clearcoatFactor = 1.0f;
        info.clearcoatRoughnessFactor = 0.06f;
    }
    if (contains("wet_stone")) {
        info.clearcoat = true;
        info.clearcoatFactor = 0.75f;
        info.clearcoatRoughnessFactor = 0.18f;
    }
    if (contains("procedural_marble")) {
        info.clearcoat = true;
        info.clearcoatFactor = 0.35f;
        info.clearcoatRoughnessFactor = 0.12f;
    }

    if (contains("skin_ish")) {
        info.subsurfaceWrap = 0.25f;
    } else if (contains("skin")) {
        info.subsurfaceWrap = 0.35f;
    }

    return info;
}

} // namespace Cortex::Graphics
