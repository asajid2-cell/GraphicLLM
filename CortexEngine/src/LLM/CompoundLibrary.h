#pragma once

#include "SceneCommands.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Cortex::LLM {

// Template describing a single part within a compound prefab.
struct CompoundPartTemplate {
    AddEntityCommand::EntityType type = AddEntityCommand::EntityType::Cube;
    glm::vec3 localPosition = glm::vec3(0.0f);
    glm::vec3 localScale = glm::vec3(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    std::string partName; // "Body", "WingL", etc.

    // Optional detail hints (segments). Zero means "use default".
    uint32_t segmentsPrimary = 0;
    uint32_t segmentsSecondary = 0;
};

// High-level prefab like "tree", "house", or "bird".
struct CompoundTemplate {
    std::string name;                 // canonical template name, e.g. "tree"
    std::string defaultGroupPrefix;   // e.g. "Tree", "Bird"
    std::vector<CompoundPartTemplate> parts;

    // Optional attached light used for emissive props like lanterns,
    // streetlights, and torches. When present, the compound executor
    // will spawn a LightComponent at basePosition + lightLocalPosition.
    bool hasAttachedLight = false;
    AddLightCommand::LightType lightType = AddLightCommand::LightType::Point;
    glm::vec3 lightLocalPosition{0.0f};
    glm::vec3 lightColor{1.0f};
    float lightIntensity = 8.0f;
    float lightRange = 8.0f;
    float lightInnerConeDegrees = 20.0f; // for spot
    float lightOuterConeDegrees = 30.0f; // for spot
    bool lightCastsShadows = true;
};

class CompoundLibrary {
public:
    // Find a template by (case-insensitive) name. Returns nullptr if not found.
    static const CompoundTemplate* FindTemplate(const std::string& templateName);

    // Optionally synthesize a template for an unknown name (e.g., "pig", "car")
    // using simple category heuristics. Returns nullptr if no reasonable
    // approximation can be built.
    static const CompoundTemplate* SynthesizeTemplate(
        const std::string& templateName,
        const glm::vec4* bodyColor = nullptr,
        const glm::vec4* accentColor = nullptr);

    // Access all registered templates.
    static const std::vector<CompoundTemplate>& GetAllTemplates();
};

} // namespace Cortex::LLM
