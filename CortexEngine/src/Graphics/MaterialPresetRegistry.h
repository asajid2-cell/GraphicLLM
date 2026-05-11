#pragma once

#include <glm/vec3.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Cortex::Graphics {

struct MaterialPresetInfo {
    float materialType = 0.0f;
    float clearcoatFactor = 0.0f;
    float clearcoatRoughnessFactor = 0.2f;
    float sheenWeight = 0.0f;
    float subsurfaceWrap = 0.0f;
    float defaultMetallic = 0.0f;
    float defaultRoughness = 0.5f;
    float defaultTransmission = 0.0f;
    float defaultEmissiveStrength = 0.0f;
    float defaultSpecularFactor = 1.0f;
    glm::vec3 defaultSpecularColorFactor = glm::vec3(1.0f);
    bool hasDefaultMetallic = false;
    bool hasDefaultRoughness = false;
    bool hasDefaultTransmission = false;
    bool hasDefaultEmissiveStrength = false;
    bool hasDefaultSpecularFactor = false;
    bool hasDefaultSpecularColor = false;
    bool emissive = false;
    bool metallic = false;
    bool transmissive = false;
    bool clearcoat = false;
};

struct MaterialPresetDescriptor {
    std::string id;
    std::string displayName;
    std::string surfaceClass;
    bool advanced = false;
    bool publicAuthoring = true;
};

class MaterialPresetRegistry {
public:
    [[nodiscard]] static const std::vector<MaterialPresetDescriptor>& CanonicalPresets();
    [[nodiscard]] static std::string Normalize(std::string_view presetName);
    [[nodiscard]] static bool ContainsToken(std::string_view presetName, std::string_view token);
    [[nodiscard]] static MaterialPresetInfo Resolve(std::string_view presetName);
};

} // namespace Cortex::Graphics
