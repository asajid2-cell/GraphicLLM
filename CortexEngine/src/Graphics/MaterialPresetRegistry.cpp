#include "Graphics/MaterialPresetRegistry.h"

#include <algorithm>
#include <cctype>

namespace Cortex::Graphics {

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
    } else if (contains("plastic")) {
        info.materialType = 3.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.35f;
    } else if (contains("brick")) {
        info.materialType = 4.0f;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 0.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.78f;
    } else if (contains("brushed_metal")) {
        info.materialType = 6.0f;
        info.metallic = true;
        info.hasDefaultMetallic = true;
        info.defaultMetallic = 1.0f;
        info.hasDefaultRoughness = true;
        info.defaultRoughness = 0.32f;
    } else if (contains("wood_floor")) {
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

    if (contains("skin_ish")) {
        info.subsurfaceWrap = 0.25f;
    } else if (contains("skin")) {
        info.subsurfaceWrap = 0.35f;
    }

    return info;
}

} // namespace Cortex::Graphics
