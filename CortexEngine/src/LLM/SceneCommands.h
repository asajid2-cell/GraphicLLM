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

// Parse LLM response (JSON) into commands
class CommandParser {
public:
    static std::vector<std::shared_ptr<SceneCommand>> ParseJSON(const std::string& json);
};

} // namespace Cortex::LLM
