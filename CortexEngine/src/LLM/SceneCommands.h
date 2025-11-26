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
    bool autoPlace = false; // let the executor pick a spawn position if true

    AddEntityCommand() { type = CommandType::AddEntity; }
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

// Parse LLM response (JSON) into commands
class CommandParser {
public:
    static std::vector<std::shared_ptr<SceneCommand>> ParseJSON(const std::string& json);
};

} // namespace Cortex::LLM
