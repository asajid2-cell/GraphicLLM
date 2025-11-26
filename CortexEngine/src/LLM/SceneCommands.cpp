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

std::string AddLightCommand::ToString() const {
    return "AddLight: " + name + " at (" +
           std::to_string(position.x) + ", " +
           std::to_string(position.y) + ", " +
           std::to_string(position.z) + ")";
}

std::string ModifyLightCommand::ToString() const {
    return "ModifyLight: " + targetName;
}

std::string ModifyRendererCommand::ToString() const {
    return "ModifyRenderer";
}

std::vector<std::shared_ptr<SceneCommand>> CommandParser::ParseJSON(const std::string& jsonStr) {
    std::vector<std::shared_ptr<SceneCommand>> commands;

    auto parseFromJson = [&](const json& j) {
        if (!j.contains("commands") || !j["commands"].is_array()) {
            spdlog::error("Invalid JSON: missing 'commands' array");
            return;
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
            else if (type == "modify_light") {
                auto cmd = std::make_shared<ModifyLightCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = cmdJson["target"];
                }

                if (cmdJson.contains("position")) {
                    cmd->setPosition = ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("direction")) {
                    cmd->setDirection = ReadVec3(cmdJson["direction"], "direction", cmd->direction);
                }
                if (cmdJson.contains("color")) {
                    glm::vec4 color4;
                    if (ReadVec4(cmdJson["color"], "color", color4)) {
                        cmd->setColor = true;
                        cmd->color = glm::vec3(color4);
                    }
                }
                if (cmdJson.contains("intensity")) {
                    cmd->setIntensity = true;
                    cmd->intensity = std::max(ReadNumber(cmdJson["intensity"], "intensity", 5.0f), 0.0f);
                }
                if (cmdJson.contains("range")) {
                    cmd->setRange = true;
                    cmd->range = std::max(ReadNumber(cmdJson["range"], "range", 10.0f), 0.0f);
                }
                if (cmdJson.contains("inner_cone")) {
                    cmd->setInnerCone = true;
                    cmd->innerConeDegrees = ReadNumber(cmdJson["inner_cone"], "inner_cone", 20.0f);
                }
                if (cmdJson.contains("outer_cone")) {
                    cmd->setOuterCone = true;
                    cmd->outerConeDegrees = ReadNumber(cmdJson["outer_cone"], "outer_cone", 30.0f);
                }
                if (cmdJson.contains("light_type") && cmdJson["light_type"].is_string()) {
                    std::string lt = cmdJson["light_type"];
                    cmd->setType = true;
                    if (lt == "directional") cmd->lightType = AddLightCommand::LightType::Directional;
                    else if (lt == "spot")   cmd->lightType = AddLightCommand::LightType::Spot;
                    else                     cmd->lightType = AddLightCommand::LightType::Point;
                }
                if (cmdJson.contains("casts_shadows") && cmdJson["casts_shadows"].is_boolean()) {
                    cmd->setCastsShadows = true;
                    cmd->castsShadows = cmdJson["casts_shadows"];
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_renderer") {
                auto cmd = std::make_shared<ModifyRendererCommand>();

                if (cmdJson.contains("exposure")) {
                    cmd->setExposure = true;
                    cmd->exposure = std::max(ReadNumber(cmdJson["exposure"], "exposure", 1.0f), 0.01f);
                }
                if (cmdJson.contains("shadows") && cmdJson["shadows"].is_boolean()) {
                    cmd->setShadowsEnabled = true;
                    cmd->shadowsEnabled = cmdJson["shadows"];
                }
                if (cmdJson.contains("debug_mode")) {
                    cmd->setDebugMode = true;
                    float v = ReadNumber(cmdJson["debug_mode"], "debug_mode", 0.0f);
                    cmd->debugMode = static_cast<int>(std::round(std::clamp(v, 0.0f, 5.0f)));
                }
                if (cmdJson.contains("shadow_bias")) {
                    cmd->setShadowBias = true;
                    float v = ReadNumber(cmdJson["shadow_bias"], "shadow_bias", 0.0005f);
                    cmd->shadowBias = std::clamp(v, 0.00001f, 0.01f);
                }
                if (cmdJson.contains("shadow_pcf_radius")) {
                    cmd->setShadowPCFRadius = true;
                    float v = ReadNumber(cmdJson["shadow_pcf_radius"], "shadow_pcf_radius", 1.5f);
                    cmd->shadowPCFRadius = std::clamp(v, 0.5f, 8.0f);
                }
                if (cmdJson.contains("cascade_lambda")) {
                    cmd->setCascadeSplitLambda = true;
                    float v = ReadNumber(cmdJson["cascade_lambda"], "cascade_lambda", 0.5f);
                    cmd->cascadeSplitLambda = std::clamp(v, 0.0f, 1.0f);
                }

                commands.push_back(cmd);
            }
            else if (type == "add_light") {
                auto cmd = std::make_shared<AddLightCommand>();

                if (cmdJson.contains("light_type") && cmdJson["light_type"].is_string()) {
                    std::string lt = cmdJson["light_type"];
                    if (lt == "directional") cmd->lightType = AddLightCommand::LightType::Directional;
                    else if (lt == "spot")   cmd->lightType = AddLightCommand::LightType::Spot;
                    else                     cmd->lightType = AddLightCommand::LightType::Point;
                }

                if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                    cmd->name = cmdJson["name"];
                }

                if (cmdJson.contains("position")) {
                    ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("direction")) {
                    ReadVec3(cmdJson["direction"], "direction", cmd->direction);
                }
                if (cmdJson.contains("color")) {
                    glm::vec4 color4;
                    if (ReadVec4(cmdJson["color"], "color", color4)) {
                        cmd->color = glm::vec3(color4);
                    }
                }
                if (cmdJson.contains("intensity")) {
                    cmd->intensity = std::max(ReadNumber(cmdJson["intensity"], "intensity", 5.0f), 0.0f);
                }
                if (cmdJson.contains("range")) {
                    cmd->range = std::max(ReadNumber(cmdJson["range"], "range", 10.0f), 0.0f);
                }
                if (cmdJson.contains("inner_cone")) {
                    cmd->innerConeDegrees = ReadNumber(cmdJson["inner_cone"], "inner_cone", 20.0f);
                }
                if (cmdJson.contains("outer_cone")) {
                    cmd->outerConeDegrees = ReadNumber(cmdJson["outer_cone"], "outer_cone", 30.0f);
                }
                if (cmdJson.contains("casts_shadows") && cmdJson["casts_shadows"].is_boolean()) {
                    cmd->castsShadows = cmdJson["casts_shadows"];
                }

                commands.push_back(cmd);
            }
        }
    };

    try {
        auto j = json::parse(jsonStr);
        parseFromJson(j);
        spdlog::info("Parsed {} commands from JSON", commands.size());
    }
    catch (const json::exception& e) {
        spdlog::error("JSON parsing error: {}", e.what());

        // Heuristic salvage for truncated or slightly malformed JSON coming from the LLM.
        // Common failure: missing closing ]}] at the end of the commands array.
        try {
            std::string fixed = jsonStr;

            // Trim trailing whitespace
            auto lastNonWs = fixed.find_last_not_of(" \t\r\n");
            if (lastNonWs != std::string::npos) {
                fixed.resize(lastNonWs + 1);
            }

            bool attemptedFix = false;

            auto commandsPos = fixed.find("\"commands\"");
            if (commandsPos != std::string::npos) {
                auto arrayStart = fixed.find('[', commandsPos);
                if (arrayStart != std::string::npos) {
                    auto arrayEnd = fixed.find(']', arrayStart);
                    if (arrayEnd == std::string::npos) {
                        // No closing ']' for the commands array – likely truncated after the last object.
                        // Most robust salvage: assume we ended after a complete object and append "]}".
                        fixed.append("]}");
                        attemptedFix = true;
                    } else {
                        // There is a closing ']', but maybe the root object '}' is missing.
                        auto last = fixed.find_last_not_of(" \t\r\n");
                        if (last != std::string::npos && fixed[last] != '}') {
                            fixed.push_back('}');
                            attemptedFix = true;
                        }
                    }
                }
            }

            if (attemptedFix) {
                spdlog::warn("Attempting JSON salvage on LLM response");
                auto jFixed = json::parse(fixed);
                commands.clear();
                parseFromJson(jFixed);
                spdlog::info("Parsed {} commands from salvaged JSON", commands.size());
            }
        }
        catch (const json::exception& e2) {
            spdlog::error("JSON salvage parse failed: {}", e2.what());
        }
    }

    return commands;
}

} // namespace Cortex::LLM
