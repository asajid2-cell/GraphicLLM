#include "SceneCommands.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <limits>

using json = nlohmann::json;

namespace Cortex::LLM {
namespace {
constexpr float kWorldClamp = 50.0f;

float ReadNumber(const json& value, const char* fieldName, float fallback = 0.0f) {
    if (!value.is_number_float() && !value.is_number_integer()) {
        spdlog::warn("Command field '{}' is not numeric, using fallback {}", fieldName, fallback);
        return fallback;
    }
    float v = value.get<float>();
    if (!std::isfinite(v)) {
        spdlog::warn("Command field '{}' is not finite, using fallback {}", fieldName, fallback);
        return fallback;
    }
    return std::clamp(v, -kWorldClamp, kWorldClamp);
}

bool ReadVec3(const json& arr, const char* fieldName, glm::vec3& out) {
    if (!arr.is_array() || arr.size() < 3) {
        spdlog::warn("Command field '{}' expects 3 numbers", fieldName);
        return false;
    }
    out = glm::vec3(
        ReadNumber(arr[0], fieldName),
        ReadNumber(arr[1], fieldName),
        ReadNumber(arr[2], fieldName)
    );
    return true;
}

bool ReadVec4(const json& arr, const char* fieldName, glm::vec4& out) {
    if (!arr.is_array() || arr.size() < 4) {
        spdlog::warn("Command field '{}' expects 4 numbers", fieldName);
        return false;
    }
    out = glm::vec4(
        ReadNumber(arr[0], fieldName),
        ReadNumber(arr[1], fieldName),
        ReadNumber(arr[2], fieldName),
        ReadNumber(arr[3], fieldName, 1.0f)
    );
    return true;
}
} // namespace

std::string AddEntityCommand::ToString() const {
    return "AddEntity: " + name + " at (" +
           std::to_string(position.x) + ", " +
           std::to_string(position.y) + ", " +
           std::to_string(position.z) + ")";
}

std::string RemoveEntityCommand::ToString() const {
    return "RemoveEntity: " + targetName;
}

std::string ModifyTransformCommand::ToString() const {
    return "ModifyTransform: " + targetName;
}

std::string ModifyMaterialCommand::ToString() const {
    return "ModifyMaterial: " + targetName;
}

std::string ModifyCameraCommand::ToString() const {
    return "ModifyCamera";
}

std::vector<std::shared_ptr<SceneCommand>> CommandParser::ParseJSON(const std::string& jsonStr) {
    std::vector<std::shared_ptr<SceneCommand>> commands;

    try {
        auto j = json::parse(jsonStr);

        if (!j.contains("commands") || !j["commands"].is_array()) {
            spdlog::error("Invalid JSON: missing 'commands' array");
            return commands;
        }

        for (const auto& cmdJson : j["commands"]) {
            if (!cmdJson.contains("type")) {
                spdlog::warn("Command missing 'type' field, skipping");
                continue;
            }

            std::string type = cmdJson["type"];

            if (type == "add_entity") {
                auto cmd = std::make_shared<AddEntityCommand>();

                if (cmdJson.contains("entity_type")) {
                    std::string entityType = cmdJson["entity_type"];
                    if (entityType == "cube") cmd->entityType = AddEntityCommand::EntityType::Cube;
                    else if (entityType == "sphere") cmd->entityType = AddEntityCommand::EntityType::Sphere;
                    else if (entityType == "plane") cmd->entityType = AddEntityCommand::EntityType::Plane;
                    else if (entityType == "cylinder") cmd->entityType = AddEntityCommand::EntityType::Cylinder;
                    else if (entityType == "pyramid") cmd->entityType = AddEntityCommand::EntityType::Pyramid;
                    else if (entityType == "cone") cmd->entityType = AddEntityCommand::EntityType::Cone;
                    else if (entityType == "torus") cmd->entityType = AddEntityCommand::EntityType::Torus;
                }

                if (cmdJson.contains("name") && cmdJson["name"].is_string()) cmd->name = cmdJson["name"];
                if (cmdJson.contains("position")) {
                    ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("scale")) {
                    ReadVec3(cmdJson["scale"], "scale", cmd->scale);
                }
                if (cmdJson.contains("color")) {
                    ReadVec4(cmdJson["color"], "color", cmd->color);
                }
                if (cmdJson.contains("metallic")) {
                    cmd->metallic = std::clamp(ReadNumber(cmdJson["metallic"], "metallic"), 0.0f, 1.0f);
                }
                if (cmdJson.contains("roughness")) {
                    cmd->roughness = std::clamp(ReadNumber(cmdJson["roughness"], "roughness"), 0.0f, 1.0f);
                }
                if (cmdJson.contains("ao")) {
                    cmd->ao = std::clamp(ReadNumber(cmdJson["ao"], "ao"), 0.0f, 1.0f);
                }

                commands.push_back(cmd);
            }
            else if (type == "remove_entity") {
                auto cmd = std::make_shared<RemoveEntityCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = cmdJson["target"];
                } else {
                    spdlog::warn("remove_entity missing string 'target' field, skipping");
                    continue;
                }
                commands.push_back(cmd);
            }
            else if (type == "modify_transform") {
                auto cmd = std::make_shared<ModifyTransformCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) cmd->targetName = cmdJson["target"];

                if (cmdJson.contains("position")) {
                    cmd->setPosition = ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("rotation")) {
                    cmd->setRotation = ReadVec3(cmdJson["rotation"], "rotation", cmd->rotation);
                }
                if (cmdJson.contains("scale")) {
                    cmd->setScale = ReadVec3(cmdJson["scale"], "scale", cmd->scale);
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_material") {
                auto cmd = std::make_shared<ModifyMaterialCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) cmd->targetName = cmdJson["target"];

                if (cmdJson.contains("color")) {
                    cmd->setColor = ReadVec4(cmdJson["color"], "color", cmd->color);
                }
                if (cmdJson.contains("metallic")) {
                    cmd->setMetallic = true;
                    cmd->metallic = std::clamp(ReadNumber(cmdJson["metallic"], "metallic"), 0.0f, 1.0f);
                }
                if (cmdJson.contains("roughness")) {
                    cmd->setRoughness = true;
                    cmd->roughness = std::clamp(ReadNumber(cmdJson["roughness"], "roughness"), 0.0f, 1.0f);
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_camera") {
                auto cmd = std::make_shared<ModifyCameraCommand>();

                if (cmdJson.contains("position")) {
                    cmd->setPosition = ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("fov")) {
                    cmd->setFOV = true;
                    cmd->fov = ReadNumber(cmdJson["fov"], "fov", 60.0f);
                }

                commands.push_back(cmd);
            }
        }

        spdlog::info("Parsed {} commands from JSON", commands.size());
    }
    catch (const json::exception& e) {
        spdlog::error("JSON parsing error: {}", e.what());
    }

    return commands;
}

} // namespace Cortex::LLM
