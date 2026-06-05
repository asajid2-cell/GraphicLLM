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
        {"painted_wall", "Painted Wall", "masonry", false, true},
        {"ceramic_tile", "Ceramic Tile", "masonry", true, true},
        {"matte", "Matte Ceramic", "default", false, true},
        {"brick", "Brick", "masonry", false, true},
        {"concrete", "Concrete", "masonry", false, true},
        {"wood_floor", "Wood Floor", "wood", true, true},
        {"backdrop", "Backdrop", "default", false, true},
        {"glass", "Glass", "glass", true, true},
        {"glass_panel", "Glass Panel", "glass", true, true},
        {"mirror", "Mirror", "mirror", true, true},
        {"water", "Water", "water", true, true},
        {"lava", "Lava", "water", true, true},
        {"honey", "Honey", "water", true, true},
        {"molasses", "Molasses", "water", true, true},
        {"emissive_panel", "Emissive Panel", "emissive", true, true},
        {"screen_panel", "Screen Panel", "emissive", true, true},
        {"rubber", "Rubber", "plastic", false, true},
        {"skin", "Skin", "default", true, true},
        {"skin_ish", "Skin-ish Wax", "default", true, true},
        {"cloth", "Cloth", "default", true, true},
        {"fabric", "Fabric", "wood", true, true},
        {"velvet", "Velvet", "default", true, true},
        {"foliage", "Foliage", "default", true, true},
        {"neon_tube", "Neon Tube", "emissive", true, true},
        {"brushed_gold", "Brushed Gold", "brushed_metal", true, true},
        {"wet_stone", "Wet Stone", "masonry", true, true},
        {"sand", "Sand", "default", true, true},
        {"anisotropic_car_paint", "Anisotropic Car Paint", "plastic", true, true},
        {"procedural_marble", "Procedural Marble", "masonry", true, true},
    };
    return presets;
}

std::string MaterialPresetRegistry::Normalize(std::string_view presetName) {
    std::string value;
    value.reserve(presetName.size());

    bool lastWasSeparator = true;
    for (unsigned char c : presetName) {
        if (std::isalnum(c)) {
            value.push_back(static_cast<char>(std::tolower(c)));
            lastWasSeparator = false;
        } else if (!lastWasSeparator) {
            value.push_back('_');
            lastWasSeparator = true;
        }
    }
    while (!value.empty() && value.back() == '_') {
        value.pop_back();
    }
    return value;
}

std::string MaterialPresetRegistry::Canonicalize(std::string_view presetName) {
    const std::string normalized = Normalize(presetName);
    if (normalized.empty()) {
        return {};
    }

    for (const auto& preset : CanonicalPresets()) {
        if (normalized == Normalize(preset.id) ||
            normalized == Normalize(preset.displayName)) {
            return preset.id;
        }
    }

    struct Alias {
        std::string_view alias;
        std::string_view id;
    };
    static constexpr Alias aliases[] = {
        {"car_paint", "anisotropic_car_paint"},
        {"auto_paint", "anisotropic_car_paint"},
        {"gold", "brushed_gold"},
        {"metal_gold", "brushed_gold"},
        {"stone_wet", "wet_stone"},
        {"marble", "procedural_marble"},
        {"shore_sand", "sand"},
        {"wet_sand", "sand"},
        {"dune", "sand"},
        {"leaf", "foliage"},
        {"leaves", "foliage"},
        {"plant", "foliage"},
        {"vegetation", "foliage"},
        {"neon", "neon_tube"},
        {"emissive", "emissive_panel"},
        {"light", "emissive_panel"},
        {"screen", "screen_panel"},
        {"display", "screen_panel"},
        {"monitor", "screen_panel"},
        {"paint", "painted_wall"},
        {"wall_paint", "painted_wall"},
        {"drywall", "painted_wall"},
        {"plaster", "painted_wall"},
        {"ceramic", "matte"},
        {"tile", "ceramic_tile"},
        {"matte_tile", "ceramic_tile"},
        {"floor_tile", "ceramic_tile"},
        {"porcelain", "ceramic_tile"},
        {"matte_ceramic", "matte"},
        {"cloth", "fabric"},
        {"upholstery", "fabric"},
        {"paper", "fabric"},
        {"fiber", "fabric"},
        {"turf", "fabric"},
        {"matte_black", "rubber"},
        {"gym_mat", "rubber"},
        {"metal", "brushed_metal"},
        {"painted_metal", "brushed_metal"},
        {"magma", "lava"},
        {"liquid_gold", "honey"},
        {"syrup", "molasses"},
    };

    for (const Alias alias : aliases) {
        if (normalized == alias.alias) {
            return std::string(alias.id);
        }
    }

    return normalized;
}

bool MaterialPresetRegistry::ContainsToken(std::string_view presetName, std::string_view token) {
    if (presetName.empty() || token.empty()) {
        return false;
    }
    return Normalize(presetName).find(Normalize(token)) != std::string::npos;
}

MaterialPresetInfo MaterialPresetRegistry::Resolve(std::string_view presetName) {
    MaterialPresetInfo info{};
    if (presetName.empty()) {
        return info;
    }

    const std::string presetLower = Canonicalize(presetName);
    auto contains = [&](std::string_view token) {
        return presetLower.find(token) != std::string::npos;
    };

    if (contains("sand")) {
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.86f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.62f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.82f, 0.72f, 0.54f);
    } else if (contains("foliage")) {
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.64f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.52f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.58f, 0.72f, 0.46f);
        info.subsurfaceWrap = 0.28f;
    } else if (contains("glass")) {
        info.materialType = 1.0f;
        info.transmissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.012f;
        info.hasDefaultTransmission = true;
        info.defaultTransmission = 0.82f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.40f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.92f, 0.97f, 1.0f);
    } else if (contains("mirror")) {
        info.materialType = 2.0f;
        info.metallic = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.02f;
    } else if (contains("screen_panel")) {
        info.materialType = 5.0f;
        info.emissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.22f;
        info.hasDefaultEmissiveStrength = true;
        info.defaultEmissiveStrength = 2.2f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.80f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.62f, 0.82f, 1.0f);
    } else if (contains("plastic") || contains("car_paint")) {
        info.materialType = 3.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("car_paint") ? 0.18f : 0.35f;
    } else if (contains("painted_wall")) {
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.72f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.42f;
    } else if (contains("ceramic_tile")) {
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.38f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.72f;
        info.clearcoat = true;
        info.clearcoatFactor = 0.22f;
        info.clearcoatRoughnessFactor = 0.24f;
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
    } else if (contains("lava")) {
        info.materialType = 5.0f;
        info.emissive = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.28f;
        info.hasDefaultEmissiveStrength = true;
        info.defaultEmissiveStrength = 4.5f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.85f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(1.0f, 0.45f, 0.12f);
    } else if (contains("honey")) {
        info.transmissive = true;
        info.clearcoat = true;
        info.clearcoatFactor = 0.65f;
        info.clearcoatRoughnessFactor = 0.10f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.18f;
        info.hasDefaultTransmission = true;
        info.defaultTransmission = 0.35f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.15f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(1.0f, 0.72f, 0.28f);
    } else if (contains("molasses")) {
        info.clearcoat = true;
        info.clearcoatFactor = 0.85f;
        info.clearcoatRoughnessFactor = 0.08f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.12f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 1.05f;
        info.hasDefaultSpecularColor = true;
        info.defaultSpecularColorFactor = glm::vec3(0.42f, 0.22f, 0.10f);
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
        info.clearcoatFactor = 0.72f;
        info.clearcoatRoughnessFactor = contains("chrome") ? 0.035f : 0.06f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = contains("chrome") ? 0.045f : 0.09f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = contains("chrome") ? 1.30f : 1.18f;
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

    if (contains("fabric") || contains("cloth") || contains("velvet")) {
        info.clearcoat = false;
        info.clearcoatFactor = 0.0f;
        info.sheenWeight = 1.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.82f;
    }
    if (contains("rubber")) {
        info.clearcoat = false;
        info.clearcoatFactor = 0.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.72f;
        info.hasDefaultSpecularFactor = true;
        info.defaultSpecularFactor = 0.30f;
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

    if (contains("foliage")) {
        info.subsurfaceWrap = std::max(info.subsurfaceWrap, 0.28f);
    } else if (contains("skin_ish")) {
        info.subsurfaceWrap = 0.25f;
    } else if (contains("skin")) {
        info.subsurfaceWrap = 0.35f;
    }

    return info;
}

} // namespace Cortex::Graphics
