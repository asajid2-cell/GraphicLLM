#include "SceneCommands.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace Cortex::LLM {

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
                }

                if (cmdJson.contains("name")) cmd->name = cmdJson["name"];
                if (cmdJson.contains("position") && cmdJson["position"].is_array() && cmdJson["position"].size() >= 3) {
                    cmd->position = glm::vec3(
                        cmdJson["position"][0].get<float>(),
                        cmdJson["position"][1].get<float>(),
                        cmdJson["position"][2].get<float>()
                    );
                }
                if (cmdJson.contains("scale") && cmdJson["scale"].is_array() && cmdJson["scale"].size() >= 3) {
                    cmd->scale = glm::vec3(
                        cmdJson["scale"][0].get<float>(),
                        cmdJson["scale"][1].get<float>(),
                        cmdJson["scale"][2].get<float>()
                    );
                }
                if (cmdJson.contains("color") && cmdJson["color"].is_array() && cmdJson["color"].size() >= 4) {
                    cmd->color = glm::vec4(
                        cmdJson["color"][0].get<float>(),
                        cmdJson["color"][1].get<float>(),
                        cmdJson["color"][2].get<float>(),
                        cmdJson["color"][3].get<float>()
                    );
                }

                commands.push_back(cmd);
            }
            else if (type == "remove_entity") {
                auto cmd = std::make_shared<RemoveEntityCommand>();
                if (cmdJson.contains("target")) cmd->targetName = cmdJson["target"];
                commands.push_back(cmd);
            }
            else if (type == "modify_transform") {
                auto cmd = std::make_shared<ModifyTransformCommand>();
                if (cmdJson.contains("target")) cmd->targetName = cmdJson["target"];

                if (cmdJson.contains("position") && cmdJson["position"].is_array() && cmdJson["position"].size() >= 3) {
                    cmd->setPosition = true;
                    cmd->position = glm::vec3(
                        cmdJson["position"][0].get<float>(),
                        cmdJson["position"][1].get<float>(),
                        cmdJson["position"][2].get<float>()
                    );
                }
                if (cmdJson.contains("scale") && cmdJson["scale"].is_array() && cmdJson["scale"].size() >= 3) {
                    cmd->setScale = true;
                    cmd->scale = glm::vec3(
                        cmdJson["scale"][0].get<float>(),
                        cmdJson["scale"][1].get<float>(),
                        cmdJson["scale"][2].get<float>()
                    );
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_material") {
                auto cmd = std::make_shared<ModifyMaterialCommand>();
                if (cmdJson.contains("target")) cmd->targetName = cmdJson["target"];

                if (cmdJson.contains("color") && cmdJson["color"].is_array() && cmdJson["color"].size() >= 4) {
                    cmd->setColor = true;
                    cmd->color = glm::vec4(
                        cmdJson["color"][0].get<float>(),
                        cmdJson["color"][1].get<float>(),
                        cmdJson["color"][2].get<float>(),
                        cmdJson["color"][3].get<float>()
                    );
                }
                if (cmdJson.contains("metallic")) {
                    cmd->setMetallic = true;
                    cmd->metallic = cmdJson["metallic"].get<float>();
                }
                if (cmdJson.contains("roughness")) {
                    cmd->setRoughness = true;
                    cmd->roughness = cmdJson["roughness"].get<float>();
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_camera") {
                auto cmd = std::make_shared<ModifyCameraCommand>();

                if (cmdJson.contains("position") && cmdJson["position"].is_array() && cmdJson["position"].size() >= 3) {
                    cmd->setPosition = true;
                    cmd->position = glm::vec3(
                        cmdJson["position"][0].get<float>(),
                        cmdJson["position"][1].get<float>(),
                        cmdJson["position"][2].get<float>()
                    );
                }
                if (cmdJson.contains("fov")) {
                    cmd->setFOV = true;
                    cmd->fov = cmdJson["fov"].get<float>();
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
