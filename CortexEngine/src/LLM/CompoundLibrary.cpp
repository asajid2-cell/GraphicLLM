#include "CompoundLibrary.h"
#include <algorithm>

namespace Cortex::LLM {

namespace {

std::string ToLowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::vector<CompoundTemplate> BuildTemplates() {
    std::vector<CompoundTemplate> templates;
    templates.reserve(4);

    // Simple tree: brown cylinder trunk + green sphere canopy
    {
        CompoundTemplate t;
        t.name = "tree";
        t.defaultGroupPrefix = "Tree";

        CompoundPartTemplate trunk;
        trunk.type = AddEntityCommand::EntityType::Cylinder;
        trunk.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        trunk.localScale = glm::vec3(0.3f, 1.5f, 0.3f);
        trunk.color = glm::vec4(0.4f, 0.25f, 0.1f, 1.0f);
        trunk.partName = "Trunk";
        trunk.segmentsPrimary = 16;

        CompoundPartTemplate canopy;
        canopy.type = AddEntityCommand::EntityType::Sphere;
        canopy.localPosition = glm::vec3(0.0f, 2.7f, 0.0f);
        canopy.localScale = glm::vec3(1.5f, 1.2f, 1.5f);
        canopy.color = glm::vec4(0.1f, 0.6f, 0.2f, 1.0f);
        canopy.partName = "Canopy";
        canopy.segmentsPrimary = 24;
        canopy.segmentsSecondary = 16;

        t.parts.push_back(trunk);
        t.parts.push_back(canopy);
        templates.push_back(t);
    }

    // Simple pillar: tall cylinder
    {
        CompoundTemplate t;
        t.name = "pillar";
        t.defaultGroupPrefix = "Pillar";

        CompoundPartTemplate body;
        body.type = AddEntityCommand::EntityType::Cylinder;
        body.localPosition = glm::vec3(0.0f, 2.0f, 0.0f);
        body.localScale = glm::vec3(0.4f, 2.0f, 0.4f);
        body.color = glm::vec4(0.8f, 0.8f, 0.85f, 1.0f);
        body.partName = "Body";
        body.segmentsPrimary = 16;

        t.parts.push_back(body);
        templates.push_back(t);
    }

    // Simple house: cube base + pyramid roof
    {
        CompoundTemplate t;
        t.name = "house";
        t.defaultGroupPrefix = "House";

        CompoundPartTemplate base;
        base.type = AddEntityCommand::EntityType::Cube;
        base.localPosition = glm::vec3(0.0f, 0.5f, 0.0f);
        base.localScale = glm::vec3(2.0f, 1.0f, 2.0f);
        base.color = glm::vec4(0.75f, 0.65f, 0.55f, 1.0f);
        base.partName = "Base";

        CompoundPartTemplate roof;
        roof.type = AddEntityCommand::EntityType::Pyramid;
        roof.localPosition = glm::vec3(0.0f, 1.5f, 0.0f);
        roof.localScale = glm::vec3(2.2f, 1.0f, 2.2f);
        roof.color = glm::vec4(0.6f, 0.2f, 0.2f, 1.0f);
        roof.partName = "Roof";

        t.parts.push_back(base);
        t.parts.push_back(roof);
        templates.push_back(t);
    }

    // Simple bird built from spheres and thin planes
    {
        CompoundTemplate t;
        t.name = "bird";
        t.defaultGroupPrefix = "Bird";

        CompoundPartTemplate body;
        body.type = AddEntityCommand::EntityType::Sphere;
        body.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        body.localScale = glm::vec3(1.2f, 0.9f, 1.6f);
        body.color = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
        body.partName = "Body";
        body.segmentsPrimary = 24;
        body.segmentsSecondary = 16;

        CompoundPartTemplate head;
        head.type = AddEntityCommand::EntityType::Sphere;
        head.localPosition = glm::vec3(0.0f, 1.6f, 0.6f);
        head.localScale = glm::vec3(0.5f, 0.5f, 0.5f);
        head.color = glm::vec4(0.95f, 0.9f, 0.3f, 1.0f);
        head.partName = "Head";
        head.segmentsPrimary = 20;
        head.segmentsSecondary = 12;

        CompoundPartTemplate wingL;
        wingL.type = AddEntityCommand::EntityType::Plane;
        wingL.localPosition = glm::vec3(-0.9f, 1.0f, 0.0f);
        wingL.localScale = glm::vec3(0.2f, 1.0f, 1.8f);
        wingL.color = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
        wingL.partName = "WingL";

        CompoundPartTemplate wingR;
        wingR.type = AddEntityCommand::EntityType::Plane;
        wingR.localPosition = glm::vec3(0.9f, 1.0f, 0.0f);
        wingR.localScale = glm::vec3(0.2f, 1.0f, 1.8f);
        wingR.color = glm::vec4(0.9f, 0.8f, 0.2f, 1.0f);
        wingR.partName = "WingR";

        CompoundPartTemplate tail;
        tail.type = AddEntityCommand::EntityType::Plane;
        tail.localPosition = glm::vec3(0.0f, 0.9f, -1.1f);
        tail.localScale = glm::vec3(0.2f, 0.8f, 1.4f);
        tail.color = glm::vec4(0.85f, 0.75f, 0.2f, 1.0f);
        tail.partName = "Tail";

        t.parts.push_back(body);
        t.parts.push_back(head);
        t.parts.push_back(wingL);
        t.parts.push_back(wingR);
        t.parts.push_back(tail);
        templates.push_back(t);
    }

    // Grass blade: very thin plane, used mainly via patterns
    {
        CompoundTemplate t;
        t.name = "grass_blade";
        t.defaultGroupPrefix = "Grass";

        CompoundPartTemplate blade;
        blade.type = AddEntityCommand::EntityType::Plane;
        blade.localPosition = glm::vec3(0.0f, 0.5f, 0.0f);
        blade.localScale = glm::vec3(0.05f, 1.0f, 0.4f);
        blade.color = glm::vec4(0.1f, 0.6f, 0.2f, 1.0f);
        blade.partName = "Blade";

        t.parts.push_back(blade);
        templates.push_back(t);
    }

    return templates;
}

const std::vector<CompoundTemplate>& GetTemplatesStorage() {
    static std::vector<CompoundTemplate> templates = BuildTemplates();
    return templates;
}

} // namespace

const CompoundTemplate* CompoundLibrary::FindTemplate(const std::string& templateName) {
    std::string key = ToLowerCopy(templateName);
    const auto& templates = GetTemplatesStorage();
    for (const auto& t : templates) {
        if (ToLowerCopy(t.name) == key) {
            return &t;
        }
    }
    return nullptr;
}

const std::vector<CompoundTemplate>& CompoundLibrary::GetAllTemplates() {
    return GetTemplatesStorage();
}

} // namespace Cortex::LLM

