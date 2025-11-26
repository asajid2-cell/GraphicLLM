#pragma once

#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace Cortex::LLM {

// Command types for scene manipulation
enum class CommandType {
    AddEntity,
    RemoveEntity,
    ModifyTransform,
    ModifyMaterial,
    ModifyCamera,
    AddLight,
    ModifyLight,
    ModifyRenderer,
    AddPattern,
    AddCompound,
    ModifyGroup,
    ScenePlan,
    Unknown
};

// Base command class
struct SceneCommand {
    CommandType type = CommandType::Unknown;
    virtual ~SceneCommand() = default;
    virtual std::string ToString() const = 0;
};

// Add a new entity to the scene
struct AddEntityCommand : public SceneCommand {
    enum class EntityType { Cube, Sphere, Plane, Cylinder, Pyramid, Cone, Torus };

    EntityType entityType = EntityType::Cube;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    std::string name;
    bool autoPlace = false;       // let the executor pick a spawn position if true

    // Geometry detail controls for high/low poly variants
    // Used primarily for spheres, cylinders, cones, and tori.
    // Interpreted as "segments around" and "segments along" (or minor segments).
    uint32_t segmentsPrimary = 32;
    uint32_t segmentsSecondary = 16;

    // When false, the executor will not add random jitter around the requested
    // position. Patterns/compounds use this to keep layouts crisp.
    bool allowPlacementJitter = true;

    // When true, the executor will skip collision avoidance for this entity
    // and place it exactly at the requested position (clamped to world bounds).
    bool disableCollisionAvoidance = false;

    AddEntityCommand() { type = CommandType::AddEntity; }
    std::string ToString() const override;
};

// Add a high-level spatial pattern of repeated elements (row, grid, ring, random scatter)
struct AddPatternCommand : public SceneCommand {
    enum class PatternType { Row, Grid, Ring, Random };

    PatternType pattern = PatternType::Row;
    std::string element;          // "cube", "sphere", "tree", "grass_blade", etc.
    int count = 1;

    // Optional region hint. If hasRegionBox is false, regionMin is treated as a center.
    glm::vec3 regionMin = glm::vec3(0.0f);
    glm::vec3 regionMax = glm::vec3(0.0f);
    bool hasRegionBox = false;

    // Optional spacing hint for rows/grids.
    glm::vec3 spacing = glm::vec3(1.0f);
    bool hasSpacing = false;

    // Optional naming/group hints so the LLM can later modify groups.
    std::string namePrefix;       // e.g. "Lantern", "GrassBlade"
    std::string groupName;        // e.g. "Row_Lanterns", "Field_Grass"
    std::string kind;             // optional semantic kind, e.g. "herd", "traffic"

    // Optional per-element scale for compounds/primitives spawned by this pattern.
    // If not set, compounds default to scale 1 and primitives keep their own defaults.
    glm::vec3 elementScale = glm::vec3(1.0f);
    bool hasElementScale = false;

    AddPatternCommand() { type = CommandType::AddPattern; }
    std::string ToString() const override;
};

// Add a compound prefab like "tree", "house", or "bird"
struct AddCompoundCommand : public SceneCommand {
    std::string templateName;     // e.g. "tree", "house", "bird"
    std::string instanceName;     // Optional user-facing name, e.g. "BigBird"
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    // Optional motif metadata for synthesized compounds
    bool hasBodyColor = false;
    bool hasAccentColor = false;
    glm::vec4 bodyColor{1.0f};
    glm::vec4 accentColor{1.0f};

    AddCompoundCommand() { type = CommandType::AddCompound; }
    std::string ToString() const override;
};

// Remove an entity by name or ID
struct RemoveEntityCommand : public SceneCommand {
    std::string targetName;

    RemoveEntityCommand() { type = CommandType::RemoveEntity; }
    std::string ToString() const override;
};

// Modify entity transform
struct ModifyTransformCommand : public SceneCommand {
    std::string targetName;
    bool setPosition = false;
    bool setRotation = false;
    bool setScale = false;
    glm::vec3 position;
    glm::vec3 rotation;  // Euler angles
    glm::vec3 scale;

    ModifyTransformCommand() { type = CommandType::ModifyTransform; }
    std::string ToString() const override;
};

// Modify entity material
struct ModifyMaterialCommand : public SceneCommand {
    std::string targetName;
    bool setColor = false;
    bool setMetallic = false;
    bool setRoughness = false;
    glm::vec4 color;
    float metallic = 0.0f;
    float roughness = 0.5f;

    ModifyMaterialCommand() { type = CommandType::ModifyMaterial; }
    std::string ToString() const override;
};

// Modify camera
struct ModifyCameraCommand : public SceneCommand {
    bool setPosition = false;
    bool setTarget = false;
    bool setFOV = false;
    glm::vec3 position;
    glm::vec3 target;
    float fov = 45.0f;

    ModifyCameraCommand() { type = CommandType::ModifyCamera; }
    std::string ToString() const override;
};

// Add a new light to the scene
struct AddLightCommand : public SceneCommand {
    enum class LightType { Directional, Point, Spot };

    LightType lightType = LightType::Point;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // for dir/spot
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float range = 10.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 30.0f;
    bool castsShadows = false;
    std::string name;

    AddLightCommand() { type = CommandType::AddLight; }
    std::string ToString() const override;
};

// Modify an existing light
struct ModifyLightCommand : public SceneCommand {
    std::string targetName;
    bool setPosition = false;
    bool setDirection = false;
    bool setColor = false;
    bool setIntensity = false;
    bool setRange = false;
    bool setInnerCone = false;
    bool setOuterCone = false;
    bool setType = false;
    bool setCastsShadows = false;

    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 5.0f;
    float range = 10.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 30.0f;
    AddLightCommand::LightType lightType = AddLightCommand::LightType::Point;
    bool castsShadows = false;

    ModifyLightCommand() { type = CommandType::ModifyLight; }
    std::string ToString() const override;
};

// Modify global renderer settings (exposure, shadows, cascades)
struct ModifyRendererCommand : public SceneCommand {
    bool setExposure = false;
    bool setShadowsEnabled = false;
    bool setDebugMode = false;
    bool setShadowBias = false;
    bool setShadowPCFRadius = false;
    bool setCascadeSplitLambda = false;

    float exposure = 1.0f;
    bool shadowsEnabled = true;
    int debugMode = 0;
    float shadowBias = 0.0005f;
    float shadowPCFRadius = 1.5f;
    float cascadeSplitLambda = 0.5f;

    ModifyRendererCommand() { type = CommandType::ModifyRenderer; }
    std::string ToString() const override;
};

// Modify a logical group or pattern of entities identified by a shared prefix
// in their TagComponent (e.g. "Bird_A.", "Field_Grass").
struct ModifyGroupCommand : public SceneCommand {
    std::string groupName;        // prefix or exact name of the group

    // Offsets / multipliers applied to every member of the group.
    bool hasPositionOffset = false;
    bool hasScaleMultiplier = false;
    glm::vec3 positionOffset = glm::vec3(0.0f);   // additive offset
    glm::vec3 scaleMultiplier = glm::vec3(1.0f);  // multiplicative scale

    ModifyGroupCommand() { type = CommandType::ModifyGroup; }
    std::string ToString() const override;
};

// High-level description of scene regions (fields, roads, yards, etc.)
struct ScenePlanCommand : public SceneCommand {
    struct Region {
        std::string name;
        glm::vec3   center{0.0f};
        glm::vec3   size{0.0f};   // extents in x/y/z
        std::string kind;         // "field", "road", "yard", etc.
        std::string attachToGroup; // optional: anchor region to existing group
        glm::vec3   offset{0.0f};  // optional offset from attached group center
        bool        hasOffset = false;
    };

    std::vector<Region> regions;

    ScenePlanCommand() { type = CommandType::ScenePlan; }
    std::string ToString() const override;
};

// Parse LLM response (JSON) into commands
class CommandParser {
public:
    static std::vector<std::shared_ptr<SceneCommand>> ParseJSON(const std::string& json);
};

} // namespace Cortex::LLM
