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
    GenerateTexture,
    GenerateEnvmap,
    SelectEntity,
    FocusCamera,
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
    enum class EntityType { Cube, Sphere, Plane, Cylinder, Pyramid, Cone, Torus, Model };

    EntityType entityType = EntityType::Cube;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec4 color = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    bool hasPreset = false;
    std::string presetName;
    glm::vec3 positionOffset = glm::vec3(0.0f);
    bool hasPositionOffset = false;
    std::string name;
    bool autoPlace = false;       // let the executor pick a spawn position if true

    // For entityType == Model, this names the asset from the
    // glTF-Sample-Models library (e.g., "DamagedHelmet", "DragonAttenuation").
    std::string asset;

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

    // Optional jitter to add small random offsets to rows/grids/rings.
    bool jitter = false;
    float jitterAmount = 0.0f;

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
    // Optional spin/orbit controls. When setSpin is true the executor will
    // add or update a RotationComponent on the resolved target so that it
    // continuously spins around the given axis at the requested speed
    // (radians per second). When stopSpin is true, any existing spin on the
    // target is removed.
    bool setSpin = false;
    bool stopSpin = false;
    // Optional parenting controls used for "orbit around X" style commands.
    // When setParent is true, the executor will try to attach this entity to
    // the named parent (so it inherits the parent's motion). When
    // clearParent is true, any existing parent relationship is removed.
    bool setParent = false;
    bool clearParent = false;
    std::string parentName;
    // When true, position/scale are interpreted as deltas
    // relative to the current transform instead of absolute.
    bool isRelative = false;
    glm::vec3 position;
    glm::vec3 rotation;  // Euler angles
    glm::vec3 scale;
    glm::vec3 spinAxis{0.0f, 1.0f, 0.0f};
    float spinSpeed = 1.0f;

    // Optional simple orbit setup, equivalent to the editor's Shift+O helper.
    // When setOrbit is true, the executor will attach this entity as a child
    // of orbitCenterName (resolved like other targets), place it at the given
    // radius in the parent's local +X direction, and ensure the center has a
    // RotationComponent with the requested angular speed.
    bool setOrbit = false;
    std::string orbitCenterName;
    float orbitRadius = 3.0f;
    float orbitSpeed = 0.6f;

    ModifyTransformCommand() { type = CommandType::ModifyTransform; }
    std::string ToString() const override;
};

// Modify entity material
struct ModifyMaterialCommand : public SceneCommand {
    std::string targetName;
    bool setColor = false;
    bool setMetallic = false;
    bool setRoughness = false;
    bool setAO = false;
    bool setPreset = false;
    glm::vec4 color;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    std::string presetName;

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

    // Optional auto-placement helpers. When autoPlace is true and position is
    // omitted or near zero, the executor will position the light relative to
    // the active camera instead of requiring an explicit world position.
    bool autoPlace = false;
    enum class AnchorMode { None, Camera, CameraForward };
    AnchorMode anchorMode = AnchorMode::None;
    // Distance in world units along the camera forward direction when
    // anchorMode == CameraForward (default if autoPlace is true and anchor
    // is unspecified).
    float forwardDistance = 5.0f;

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
    bool setColorGrade = false;
    bool setSSAOEnabled = false;
    bool setSSAOParams = false;
    bool setEnvironment = false;
    bool setIBLEnabled = false;
    bool setIBLIntensity = false;
    bool setLightingRig = false;
    bool setFogEnabled = false;
    bool setFogParams = false;
    bool setSunDirection = false;
    bool setSunColor = false;
    bool setSunIntensity = false;

    float exposure = 1.0f;
    bool shadowsEnabled = true;
    int debugMode = 0;
    float shadowBias = 0.0005f;
    float shadowPCFRadius = 1.5f;
    float cascadeSplitLambda = 0.5f;
    float colorGradeWarm = 0.0f;
    float colorGradeCool = 0.0f;
    bool ssaoEnabled = true;
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    float ssaoIntensity = 1.0f;
    std::string environment;       // "studio", "sunset", "night"
    bool iblEnabled = true;
    float iblDiffuseIntensity = 1.0f;
    float iblSpecularIntensity = 1.0f;
    // Lighting rig identifiers understood by the renderer/command queue, e.g.:
    // "studio_three_point", "warehouse", "horror_side", "street_lanterns".
    std::string lightingRig;
    bool fogEnabled = false;
    float fogDensity = 0.02f;
    float fogHeight = 0.0f;
    float fogFalloff = 0.5f;
    glm::vec3 sunDirection{ -0.3f, -1.0f, 0.1f };
    glm::vec3 sunColor{ 1.0f, 0.96f, 0.9f };
    float sunIntensity = 5.0f;

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

// Generate a texture via Dreamer for a specific entity or logical target.
struct GenerateTextureCommand : public SceneCommand {
    std::string targetName;     // Tag / group to target
    std::string prompt;         // High-level texture description
    std::string usage;          // "albedo", "normal", "roughness", "metalness"
    std::string materialPreset; // e.g. "wet_cobblestone"
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t seed   = 0;

    GenerateTextureCommand() { type = CommandType::GenerateTexture; }
    std::string ToString() const override;
};

// Generate an environment map / skybox via Dreamer.
struct GenerateEnvmapCommand : public SceneCommand {
    std::string name;    // logical name (e.g. "CyberpunkNight")
    std::string prompt;  // description of the environment
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t seed   = 0;

    GenerateEnvmapCommand() { type = CommandType::GenerateEnvmap; }
    std::string ToString() const override;
};

// Select an entity by name/tag so the editor can highlight and manipulate it.
struct SelectEntityCommand : public SceneCommand {
    std::string targetName;
    bool clearOthers = true;

    SelectEntityCommand() { type = CommandType::SelectEntity; }
    std::string ToString() const override;
};

// Focus / frame the camera on a specific entity or explicit world position.
struct FocusCameraCommand : public SceneCommand {
    std::string targetName;      // preferred: resolve to entity
    bool hasTargetPosition = false;
    glm::vec3 targetPosition{0.0f};

    FocusCameraCommand() { type = CommandType::FocusCamera; }
    std::string ToString() const override;
};

// Parse LLM response (JSON) into commands
class CommandParser {
public:
    // Optional focusTargetName is used to resolve symbolic targets like
    // "RecentObject" or pronouns such as "it" into a concrete group/entity name.
    static std::vector<std::shared_ptr<SceneCommand>> ParseJSON(const std::string& json,
                                                                const std::string& focusTargetName = std::string{});
};

} // namespace Cortex::LLM
