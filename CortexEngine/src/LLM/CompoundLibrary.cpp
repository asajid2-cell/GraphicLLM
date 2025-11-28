#include "CompoundLibrary.h"
#include <algorithm>
#include <optional>

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
    templates.reserve(8);

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

    // Lantern: simple hanging lantern with an internal emissive orb and an
    // attached warm point light, useful for shadow/occlusion tests.
    {
        CompoundTemplate t;
        t.name = "lantern";
        t.defaultGroupPrefix = "Lantern";

        // Support post
        CompoundPartTemplate post;
        post.type = AddEntityCommand::EntityType::Cylinder;
        post.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        post.localScale = glm::vec3(0.1f, 1.0f, 0.1f);
        post.color = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        post.partName = "Post";
        post.segmentsPrimary = 16;

        // Housing
        CompoundPartTemplate cage;
        cage.type = AddEntityCommand::EntityType::Cube;
        cage.localPosition = glm::vec3(0.0f, 1.6f, 0.0f);
        cage.localScale = glm::vec3(0.5f, 0.6f, 0.5f);
        cage.color = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
        cage.partName = "Cage";

        // Core emissive orb (uses PBR metallic/roughness but color still matters)
        CompoundPartTemplate core;
        core.type = AddEntityCommand::EntityType::Sphere;
        core.localPosition = glm::vec3(0.0f, 1.6f, 0.0f);
        core.localScale = glm::vec3(0.22f, 0.22f, 0.22f);
        core.color = glm::vec4(1.0f, 0.85f, 0.55f, 1.0f);
        core.partName = "Core";
        core.segmentsPrimary = 24;
        core.segmentsSecondary = 16;

        t.parts.push_back(post);
        t.parts.push_back(cage);
        t.parts.push_back(core);

        // Attached light positioned at the emissive core.
        t.hasAttachedLight = true;
        t.lightType = AddLightCommand::LightType::Point;
        t.lightLocalPosition = core.localPosition;
        t.lightColor = glm::vec3(1.0f, 0.9f, 0.7f);
        // Treat lanterns as primary environment lights: strong, warm,
        // and with a generous radius so they meaningfully illuminate
        // nearby geometry and cast visible shadows.
        t.lightIntensity = 22.0f;
        t.lightRange = 14.0f;
        t.lightInnerConeDegrees = 0.0f;
        t.lightOuterConeDegrees = 0.0f;
        t.lightCastsShadows = true;

        templates.push_back(t);
    }

    return templates;
}

std::vector<CompoundTemplate>& TemplatesStorage() {
    static std::vector<CompoundTemplate> templates = BuildTemplates();
    return templates;
}

const std::vector<CompoundTemplate>& GetTemplatesStorage() {
    return TemplatesStorage();
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

const CompoundTemplate* CompoundLibrary::SynthesizeTemplate(const std::string& templateName,
                                                            const glm::vec4* bodyColor,
                                                            const glm::vec4* accentColor) {
    // If we already have a template (static or previously synthesized), reuse it.
    if (const auto* existing = FindTemplate(templateName)) {
        return existing;
    }

    std::string key = ToLowerCopy(templateName);
    auto& templates = TemplatesStorage();

    auto contains = [&](const char* token) {
        return key.find(token) != std::string::npos;
    };

    enum class Category { Quadruped, Vehicle, Structure, Blob };

    std::optional<Category> category;

    // Explicit motif names first
    if (key == "quadruped") {
        category = Category::Quadruped;
    } else if (key == "vehicle") {
        category = Category::Vehicle;
    } else if (key == "tower") {
        category = Category::Structure;
    } else if (key == "blob") {
        category = Category::Blob;
    } else if (contains("pig") || contains("cow") || contains("horse") ||
               contains("dog") || contains("cat") || contains("dragon") ||
               contains("monster") || contains("animal") || contains("creature")) {
        category = Category::Quadruped;
    } else if (contains("car") || contains("truck") || contains("bus") ||
               contains("tank") || contains("vehicle") || contains("spaceship") ||
               contains("ship") || contains("plane") || contains("rocket")) {
        category = Category::Vehicle;
    } else if (contains("tower") || contains("castle") || contains("bridge") ||
               contains("arch") || contains("gate") || contains("portal")) {
        category = Category::Structure;
    } else {
        category = Category::Blob;
    }

    CompoundTemplate t;
    t.name = key;
    t.defaultGroupPrefix = templateName.empty() ? "Compound" : templateName;

    // Resolve motif colors (if any)
    auto bodyCol = bodyColor ? *bodyColor : glm::vec4(0.0f);
    auto accentCol = accentColor ? *accentColor : glm::vec4(0.0f);
    bool hasBody = bodyColor != nullptr;
    bool hasAccent = accentColor != nullptr;

    switch (*category) {
        case Category::Quadruped: {
            // Normalized quadruped: body, head, 4 legs, tail.
            CompoundPartTemplate body;
            body.type = AddEntityCommand::EntityType::Sphere;
            body.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
            body.localScale = glm::vec3(1.2f, 0.9f, 1.6f);
            body.color = hasBody ? bodyCol : glm::vec4(0.8f, 0.6f, 0.6f, 1.0f);
            body.partName = "Body";
            body.segmentsPrimary = 24;
            body.segmentsSecondary = 16;

            CompoundPartTemplate head;
            head.type = AddEntityCommand::EntityType::Sphere;
            head.localPosition = glm::vec3(0.0f, 1.45f, 0.95f);
            head.localScale = glm::vec3(0.65f, 0.65f, 0.65f);
            head.color = hasBody ? bodyCol : glm::vec4(0.9f, 0.7f, 0.7f, 1.0f);
            head.partName = "Head";
            head.segmentsPrimary = 20;
            head.segmentsSecondary = 12;

            CompoundPartTemplate legFL;
            legFL.type = AddEntityCommand::EntityType::Cylinder;
            legFL.localPosition = glm::vec3(-0.65f, 0.1f, 0.75f);
            legFL.localScale = glm::vec3(0.18f, 0.75f, 0.18f);
            legFL.color = hasAccent ? accentCol : glm::vec4(0.7f, 0.5f, 0.5f, 1.0f);
            legFL.partName = "LegFL";
            legFL.segmentsPrimary = 12;

            CompoundPartTemplate legFR = legFL;
            legFR.localPosition.x = 0.8f;
            legFR.partName = "LegFR";

            CompoundPartTemplate legBL = legFL;
            legBL.localPosition.z = -0.75f;
            legBL.partName = "LegBL";

            CompoundPartTemplate legBR = legFR;
            legBR.localPosition.z = -0.75f;
            legBR.partName = "LegBR";

            CompoundPartTemplate tail;
            tail.type = AddEntityCommand::EntityType::Cylinder;
            tail.localPosition = glm::vec3(0.0f, 1.1f, -1.2f);
            tail.localScale = glm::vec3(0.14f, 0.55f, 0.14f);
            tail.color = hasAccent ? accentCol : glm::vec4(0.7f, 0.5f, 0.5f, 1.0f);
            tail.partName = "Tail";
            tail.segmentsPrimary = 12;

            t.parts = {body, head, legFL, legFR, legBL, legBR, tail};
            break;
        }
        case Category::Vehicle: {
            // Simple car/truck: body + four wheels.
            CompoundPartTemplate body;
            body.type = AddEntityCommand::EntityType::Cube;
            body.localPosition = glm::vec3(0.0f, 0.5f, 0.0f);
            body.localScale = glm::vec3(3.0f, 0.7f, 1.6f);
            body.color = hasBody ? bodyCol : glm::vec4(0.8f, 0.6f, 0.4f, 1.0f);
            body.partName = "Body";

            CompoundPartTemplate cabin;
            cabin.type = AddEntityCommand::EntityType::Cube;
            cabin.localPosition = glm::vec3(-0.6f, 1.0f, 0.0f);
            cabin.localScale = glm::vec3(1.4f, 0.6f, 1.4f);
            cabin.color = hasBody ? bodyCol : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
            cabin.partName = "Cabin";

            CompoundPartTemplate wheel;
            wheel.type = AddEntityCommand::EntityType::Cylinder;
            wheel.localScale = glm::vec3(0.4f, 0.4f, 0.4f);
            wheel.color = hasAccent ? accentCol : glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
            wheel.partName = "WheelFL";

            CompoundPartTemplate wheelFR = wheel;
            wheelFR.partName = "WheelFR";
            CompoundPartTemplate wheelBL = wheel;
            wheelBL.partName = "WheelBL";
            CompoundPartTemplate wheelBR = wheel;
            wheelBR.partName = "WheelBR";

            const float bx = 1.4f;
            const float bz = 0.9f;
            const float wy = 0.2f;
            wheel.localPosition = glm::vec3(-bx, wy,  bz);
            wheelFR.localPosition = glm::vec3(bx, wy,  bz);
            wheelBL.localPosition = glm::vec3(-bx, wy, -bz);
            wheelBR.localPosition = glm::vec3(bx, wy, -bz);

            t.parts = {body, cabin, wheel, wheelFR, wheelBL, wheelBR};
            break;
        }
        case Category::Structure: {
            // Simple tower: base + shaft + top.
            CompoundPartTemplate base;
            base.type = AddEntityCommand::EntityType::Cube;
            base.localPosition = glm::vec3(0.0f, 0.3f, 0.0f);
            base.localScale = glm::vec3(1.8f, 0.6f, 1.8f);
            base.color = hasBody ? bodyCol : glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
            base.partName = "Base";

            CompoundPartTemplate shaft;
            shaft.type = AddEntityCommand::EntityType::Cylinder;
            shaft.localPosition = glm::vec3(0.0f, 1.9f, 0.0f);
            shaft.localScale = glm::vec3(0.6f, 1.9f, 0.6f);
            shaft.color = hasBody ? bodyCol : glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
            shaft.partName = "Shaft";
            shaft.segmentsPrimary = 16;

            CompoundPartTemplate top;
            top.type = AddEntityCommand::EntityType::Sphere;
            top.localPosition = glm::vec3(0.0f, 3.5f, 0.0f);
            top.localScale = glm::vec3(0.8f, 0.8f, 0.8f);
            top.color = hasAccent ? accentCol : glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
            top.partName = "Top";
            top.segmentsPrimary = 20;
            top.segmentsSecondary = 12;

            t.parts = {base, shaft, top};
            break;
        }
        case Category::Blob: {
            // Generic blob: two offset spheres.
            CompoundPartTemplate lower;
            lower.type = AddEntityCommand::EntityType::Sphere;
            lower.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
            lower.localScale = glm::vec3(1.2f, 1.0f, 1.2f);
            lower.color = hasBody ? bodyCol : glm::vec4(0.7f, 0.7f, 0.9f, 1.0f);
            lower.partName = "Lower";
            lower.segmentsPrimary = 20;
            lower.segmentsSecondary = 12;

            CompoundPartTemplate upper;
            upper.type = AddEntityCommand::EntityType::Sphere;
            upper.localPosition = glm::vec3(0.2f, 1.8f, 0.1f);
            upper.localScale = glm::vec3(0.7f, 0.7f, 0.7f);
            upper.color = hasAccent ? accentCol : glm::vec4(0.8f, 0.8f, 1.0f, 1.0f);
            upper.partName = "Upper";
            upper.segmentsPrimary = 18;
            upper.segmentsSecondary = 10;

            t.parts = {lower, upper};
            break;
        }
    }

    templates.push_back(t);
    return &templates.back();
}

} // namespace Cortex::LLM
