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

std::string AddPatternCommand::ToString() const {
    std::string patternStr;
    switch (pattern) {
        case AddPatternCommand::PatternType::Row:   patternStr = "row"; break;
        case AddPatternCommand::PatternType::Grid:  patternStr = "grid"; break;
        case AddPatternCommand::PatternType::Ring:  patternStr = "ring"; break;
        case AddPatternCommand::PatternType::Random:patternStr = "random"; break;
    }
    return "AddPattern: " + patternStr + " of " + element;
}

std::string AddCompoundCommand::ToString() const {
    return "AddCompound: " + templateName + " as " + instanceName;
}

std::string ModifyGroupCommand::ToString() const {
    return "ModifyGroup: " + groupName;
}

std::string ScenePlanCommand::ToString() const {
    return "ScenePlan: " + std::to_string(regions.size()) + " regions";
}

std::string GenerateTextureCommand::ToString() const {
    return "GenerateTexture: " + targetName + " usage=" + usage + " preset=" + materialPreset;
}

std::string GenerateEnvmapCommand::ToString() const {
    return "GenerateEnvmap: " + name;
}

std::string SelectEntityCommand::ToString() const {
    return "SelectEntity: " + targetName;
}

std::string FocusCameraCommand::ToString() const {
    if (!targetName.empty()) {
        return "FocusCamera: " + targetName;
    }
    return "FocusCamera: position";
}

std::vector<std::shared_ptr<SceneCommand>> CommandParser::ParseJSON(const std::string& jsonStr,
                                                                    const std::string& focusTargetName) {
    std::vector<std::shared_ptr<SceneCommand>> commands;

    auto resolveTargetName = [&](const std::string& raw) -> std::string {
        if (raw.empty()) return raw;

        std::string lowered = raw;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!focusTargetName.empty()) {
            if (lowered == "it" || lowered == "this" || lowered == "that" ||
                lowered == "recentobject" || lowered == "recent_object") {
                return focusTargetName;
            }
        }
        return raw;
    };

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

            if (type == "select_entity") {
                auto cmd = std::make_shared<SelectEntityCommand>();
                if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["name"]);
                }
                if (cmdJson.contains("clear_others") && cmdJson["clear_others"].is_boolean()) {
                    cmd->clearOthers = cmdJson["clear_others"];
                }
                commands.push_back(cmd);
            }
            else if (type == "focus_camera") {
                auto cmd = std::make_shared<FocusCameraCommand>();
                if (cmdJson.contains("target_entity") && cmdJson["target_entity"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["target_entity"]);
                }
                if (cmdJson.contains("target_position") && cmdJson["target_position"].is_array()) {
                    glm::vec3 pos;
                    if (ReadVec3(cmdJson["target_position"], "target_position", pos)) {
                        cmd->hasTargetPosition = true;
                        cmd->targetPosition = pos;
                    }
                }
                commands.push_back(cmd);
            }
            else if (type == "add_entity") {
                // If the model uses entity_type equal to a compound prefab like "house",
                // reinterpret this as an add_compound instead of a single primitive so
                // that houses stay rich multi-part objects.
                if (cmdJson.contains("entity_type") && cmdJson["entity_type"].is_string()) {
                    std::string entityType = cmdJson["entity_type"];
                    std::string lowered = entityType;
                    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (lowered == "house") {
                        auto house = std::make_shared<AddCompoundCommand>();
                        house->templateName = "house";
                        if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                            house->instanceName = cmdJson["name"];
                        }
                        if (cmdJson.contains("position")) {
                            ReadVec3(cmdJson["position"], "position", house->position);
                        }
                        if (cmdJson.contains("scale")) {
                            ReadVec3(cmdJson["scale"], "scale", house->scale);
                        }
                        commands.push_back(house);
                        continue;
                    }
                }

                auto cmd = std::make_shared<AddEntityCommand>();

                bool recognizedPrimitive = false;
                std::string entityType;
                std::string lowered;

                if (cmdJson.contains("entity_type") && cmdJson["entity_type"].is_string()) {
                    entityType = cmdJson["entity_type"];
                    lowered = entityType;
                    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    // Allow richer vocabulary by mapping synonyms onto existing primitives.
                    if (lowered == "cube" || lowered == "box" || lowered == "rounded_box") {
                        cmd->entityType = AddEntityCommand::EntityType::Cube;
                        recognizedPrimitive = true;
                    } else if (lowered == "sphere" || lowered == "ball" ||
                               lowered == "lowpoly_sphere" || lowered == "highpoly_sphere") {
                        cmd->entityType = AddEntityCommand::EntityType::Sphere;
                        recognizedPrimitive = true;
                        // Heuristic detail hints from name
                        if (lowered == "lowpoly_sphere") {
                            cmd->segmentsPrimary = 16;
                            cmd->segmentsSecondary = 8;
                        } else if (lowered == "highpoly_sphere") {
                            cmd->segmentsPrimary = 48;
                            cmd->segmentsSecondary = 32;
                        }
                    } else if (lowered == "plane" || lowered == "thin_plane" ||
                               lowered == "leaf" || lowered == "wing") {
                        cmd->entityType = AddEntityCommand::EntityType::Plane;
                        recognizedPrimitive = true;
                    } else if (lowered == "cylinder" || lowered == "capsule" || lowered == "pillar") {
                        cmd->entityType = AddEntityCommand::EntityType::Cylinder;
                        recognizedPrimitive = true;
                    } else if (lowered == "pyramid" || lowered == "wedge") {
                        cmd->entityType = AddEntityCommand::EntityType::Pyramid;
                        recognizedPrimitive = true;
                    } else if (lowered == "cone") {
                        cmd->entityType = AddEntityCommand::EntityType::Cone;
                        recognizedPrimitive = true;
                    } else if (lowered == "torus" || lowered == "arch") {
                        cmd->entityType = AddEntityCommand::EntityType::Torus;
                        recognizedPrimitive = true;
                    } else if (lowered == "model") {
                        cmd->entityType = AddEntityCommand::EntityType::Model;
                        recognizedPrimitive = true;
                        if (cmdJson.contains("asset") && cmdJson["asset"].is_string()) {
                            cmd->asset = cmdJson["asset"];
                        }
                    }
                }

                // If the entity_type is not a known primitive shape, interpret
                // this as a request for a compound motif instead of silently
                // falling back to a cube. This lets things like "monkey",
                // "fridge", or "godzilla" produce structured approximations via
                // the compound synthesis pipeline.
                if (!recognizedPrimitive && !entityType.empty()) {
                    auto compound = std::make_shared<AddCompoundCommand>();
                    compound->templateName = entityType;
                    if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                        compound->instanceName = cmdJson["name"];
                    } else {
                        compound->instanceName = entityType;
                    }
                    if (cmdJson.contains("position")) {
                        ReadVec3(cmdJson["position"], "position", compound->position);
                    }
                    if (cmdJson.contains("scale")) {
                        ReadVec3(cmdJson["scale"], "scale", compound->scale);
                    }
                    if (cmdJson.contains("color")) {
                        glm::vec4 color4;
                        if (ReadVec4(cmdJson["color"], "color", color4)) {
                            compound->hasBodyColor = true;
                            compound->bodyColor = color4;
                        }
                    }
                    commands.push_back(compound);
                    continue;
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
                if (cmdJson.contains("position_offset") && cmdJson["position_offset"].is_array()) {
                    cmd->hasPositionOffset = ReadVec3(cmdJson["position_offset"], "position_offset", cmd->positionOffset);
                }
                // Optional geometry detail knobs
                if (cmdJson.contains("segments")) {
                    float s = ReadNumber(cmdJson["segments"], "segments", 32.0f);
                    uint32_t seg = static_cast<uint32_t>(std::clamp(s, 8.0f, 96.0f));
                    cmd->segmentsPrimary = seg;
                    cmd->segmentsSecondary = std::max<uint32_t>(8u, seg / 2u);
                }
                if (cmdJson.contains("segments_primary")) {
                    float s = ReadNumber(cmdJson["segments_primary"], "segments_primary", static_cast<float>(cmd->segmentsPrimary));
                    cmd->segmentsPrimary = static_cast<uint32_t>(std::clamp(s, 8.0f, 96.0f));
                }
                if (cmdJson.contains("segments_secondary")) {
                    float s = ReadNumber(cmdJson["segments_secondary"], "segments_secondary", static_cast<float>(cmd->segmentsSecondary));
                    cmd->segmentsSecondary = static_cast<uint32_t>(std::clamp(s, 4.0f, 64.0f));
                }
                if (cmdJson.contains("detail") && cmdJson["detail"].is_string()) {
                    std::string d = cmdJson["detail"];
                    std::transform(d.begin(), d.end(), d.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (d == "low") {
                        cmd->segmentsPrimary = 16;
                        cmd->segmentsSecondary = 8;
                    } else if (d == "medium") {
                        cmd->segmentsPrimary = 24;
                        cmd->segmentsSecondary = 12;
                    } else if (d == "high" || d == "smooth") {
                        cmd->segmentsPrimary = 48;
                        cmd->segmentsSecondary = 32;
                    }
                }
                // Macros may set this flag later; JSON field is intentionally not exposed.
                if (cmdJson.contains("metallic")) {
                    float raw = ReadNumber(cmdJson["metallic"], "metallic");
                    float clamped = std::clamp(raw, 0.0f, 1.0f);
                    if (raw < 0.0f || raw > 1.0f) {
                        spdlog::warn("add_entity metallic clamped from {} to {}", raw, clamped);
                    }
                    cmd->metallic = clamped;
                }
                if (cmdJson.contains("roughness")) {
                    float raw = ReadNumber(cmdJson["roughness"], "roughness");
                    float clamped = std::clamp(raw, 0.0f, 1.0f);
                    if (raw < 0.0f || raw > 1.0f) {
                        spdlog::warn("add_entity roughness clamped from {} to {}", raw, clamped);
                    }
                    cmd->roughness = clamped;
                }
                if (cmdJson.contains("ao")) {
                    float raw = ReadNumber(cmdJson["ao"], "ao");
                    float clamped = std::clamp(raw, 0.0f, 1.0f);
                    if (raw < 0.0f || raw > 1.0f) {
                        spdlog::warn("add_entity ao clamped from {} to {}", raw, clamped);
                    }
                    cmd->ao = clamped;
                }
                if (cmdJson.contains("preset") && cmdJson["preset"].is_string()) {
                    cmd->hasPreset = true;
                    cmd->presetName = cmdJson["preset"];
                }

                commands.push_back(cmd);
            }
            else if (type == "add_pattern") {
                auto cmd = std::make_shared<AddPatternCommand>();
                if (cmdJson.contains("pattern") && cmdJson["pattern"].is_string()) {
                    std::string p = cmdJson["pattern"];
                    std::transform(p.begin(), p.end(), p.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (p == "row") cmd->pattern = AddPatternCommand::PatternType::Row;
                    else if (p == "grid") cmd->pattern = AddPatternCommand::PatternType::Grid;
                    else if (p == "ring") cmd->pattern = AddPatternCommand::PatternType::Ring;
                    else if (p == "random") cmd->pattern = AddPatternCommand::PatternType::Random;
                }
                if (cmdJson.contains("kind") && cmdJson["kind"].is_string()) {
                    cmd->kind = cmdJson["kind"];
                }
                if (cmdJson.contains("element") && cmdJson["element"].is_string()) {
                    cmd->element = cmdJson["element"];
                }
                if (cmdJson.contains("count")) {
                    int c = static_cast<int>(ReadNumber(cmdJson["count"], "count", 1.0f));
                    cmd->count = std::max(1, c);
                }
                if (cmdJson.contains("region") && cmdJson["region"].is_array()) {
                    const auto& arr = cmdJson["region"];
                    if (arr.size() >= 6) {
                        cmd->hasRegionBox = true;
                        cmd->regionMin = glm::vec3(
                            ReadNumber(arr[0], "region_x0"),
                            ReadNumber(arr[1], "region_y0"),
                            ReadNumber(arr[2], "region_z0"));
                        cmd->regionMax = glm::vec3(
                            ReadNumber(arr[3], "region_x1"),
                            ReadNumber(arr[4], "region_y1"),
                            ReadNumber(arr[5], "region_z1"));
                    } else if (arr.size() >= 3) {
                        cmd->hasRegionBox = false;
                        ReadVec3(arr, "region_center", cmd->regionMin);
                        cmd->regionMax = cmd->regionMin;
                    }
                }
                if (cmdJson.contains("spacing") && cmdJson["spacing"].is_array()) {
                    cmd->hasSpacing = ReadVec3(cmdJson["spacing"], "spacing", cmd->spacing);
                }
                if (cmdJson.contains("name_prefix") && cmdJson["name_prefix"].is_string()) {
                    cmd->namePrefix = cmdJson["name_prefix"];
                }
                if (cmdJson.contains("group") && cmdJson["group"].is_string()) {
                    cmd->groupName = cmdJson["group"];
                }
                if (cmdJson.contains("element_scale") && cmdJson["element_scale"].is_array()) {
                    cmd->hasElementScale = ReadVec3(cmdJson["element_scale"], "element_scale", cmd->elementScale);
                }
                if (cmdJson.contains("jitter") && cmdJson["jitter"].is_boolean()) {
                    cmd->jitter = cmdJson["jitter"];
                }
                if (cmdJson.contains("jitter_amount")) {
                    cmd->jitterAmount = ReadNumber(cmdJson["jitter_amount"], "jitter_amount", 0.5f);
                }
                commands.push_back(cmd);
            }
            else if (type == "add_compound") {
                auto cmd = std::make_shared<AddCompoundCommand>();
                if (cmdJson.contains("template") && cmdJson["template"].is_string()) {
                    cmd->templateName = cmdJson["template"];
                }
                if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                    cmd->instanceName = cmdJson["name"];
                }
                if (cmdJson.contains("position")) {
                    ReadVec3(cmdJson["position"], "position", cmd->position);
                }
                if (cmdJson.contains("scale")) {
                    ReadVec3(cmdJson["scale"], "scale", cmd->scale);
                }
                if (cmdJson.contains("body_color")) {
                    glm::vec4 c;
                    if (ReadVec4(cmdJson["body_color"], "body_color", c)) {
                        cmd->hasBodyColor = true;
                        cmd->bodyColor = c;
                    }
                }
                if (cmdJson.contains("accent_color")) {
                    glm::vec4 c;
                    if (ReadVec4(cmdJson["accent_color"], "accent_color", c)) {
                        cmd->hasAccentColor = true;
                        cmd->accentColor = c;
                    }
                }
                commands.push_back(cmd);
            }
            else if (type == "scene_plan") {
                auto cmd = std::make_shared<ScenePlanCommand>();
                if (cmdJson.contains("regions") && cmdJson["regions"].is_array()) {
                    for (const auto& r : cmdJson["regions"]) {
                        ScenePlanCommand::Region reg;
                        if (r.contains("name") && r["name"].is_string()) {
                            reg.name = r["name"];
                        }
                        if (r.contains("kind") && r["kind"].is_string()) {
                            reg.kind = r["kind"];
                        }
                        if (r.contains("center")) {
                            ReadVec3(r["center"], "region_center", reg.center);
                        }
                        if (r.contains("size")) {
                            ReadVec3(r["size"], "region_size", reg.size);
                        }
                        if (r.contains("attach_to_group") && r["attach_to_group"].is_string()) {
                            reg.attachToGroup = r["attach_to_group"];
                        }
                        if (r.contains("offset") && r["offset"].is_array()) {
                            reg.hasOffset = ReadVec3(r["offset"], "region_offset", reg.offset);
                        }
                        cmd->regions.push_back(reg);
                    }
                }
                commands.push_back(cmd);
            }
            else if (type == "remove_entity") {
                auto cmd = std::make_shared<RemoveEntityCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["target"]);
                } else {
                    spdlog::warn("remove_entity missing string 'target' field, skipping");
                    continue;
                }
                commands.push_back(cmd);
            }
            else if (type == "modify_transform") {
                auto cmd = std::make_shared<ModifyTransformCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["target"]);
                }

                // Optional relative mode: when enabled, position/scale are treated
                // as offsets/multipliers relative to the current transform instead
                // of absolute values.
                if (cmdJson.contains("mode") && cmdJson["mode"].is_string()) {
                    std::string mode = cmdJson["mode"];
                    std::transform(mode.begin(), mode.end(), mode.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (mode == "relative") {
                        cmd->isRelative = true;
                    } else if (mode == "absolute") {
                        cmd->isRelative = false;
                    }
                } else if (cmdJson.contains("relative") && cmdJson["relative"].is_boolean()) {
                    cmd->isRelative = cmdJson["relative"];
                }

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
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["target"]);
                }

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
                if (cmdJson.contains("ao")) {
                    cmd->setAO = true;
                    cmd->ao = std::clamp(ReadNumber(cmdJson["ao"], "ao", 1.0f), 0.0f, 1.0f);
                }
                if (cmdJson.contains("preset") && cmdJson["preset"].is_string()) {
                    cmd->setPreset = true;
                    cmd->presetName = cmdJson["preset"];
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
                    cmd->targetName = resolveTargetName(cmdJson["target"]);
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
                    cmd->debugMode = static_cast<int>(std::round(std::clamp(v, 0.0f, 17.0f)));
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
                if (cmdJson.contains("environment") && cmdJson["environment"].is_string()) {
                    cmd->setEnvironment = true;
                    cmd->environment = cmdJson["environment"];
                }
                if (cmdJson.contains("ibl_enabled") && cmdJson["ibl_enabled"].is_boolean()) {
                    cmd->setIBLEnabled = true;
                    cmd->iblEnabled = cmdJson["ibl_enabled"];
                }
                if (cmdJson.contains("ibl_intensity")) {
                    const auto& val = cmdJson["ibl_intensity"];
                    cmd->setIBLIntensity = true;
                    if (val.is_array() && val.size() >= 2) {
                        cmd->iblDiffuseIntensity = std::max(ReadNumber(val[0], "ibl_intensity[0]", 1.0f), 0.0f);
                        cmd->iblSpecularIntensity = std::max(ReadNumber(val[1], "ibl_intensity[1]", 1.0f), 0.0f);
                    } else {
                        float v = std::max(ReadNumber(val, "ibl_intensity", 1.0f), 0.0f);
                        cmd->iblDiffuseIntensity = v;
                        cmd->iblSpecularIntensity = v;
                    }
                }
                if (cmdJson.contains("grade_warm")) {
                    cmd->setColorGrade = true;
                    float v = ReadNumber(cmdJson["grade_warm"], "grade_warm", 0.0f);
                    cmd->colorGradeWarm = std::clamp(v, -1.0f, 1.0f);
                }
                if (cmdJson.contains("grade_cool")) {
                    cmd->setColorGrade = true;
                    float v = ReadNumber(cmdJson["grade_cool"], "grade_cool", 0.0f);
                    cmd->colorGradeCool = std::clamp(v, -1.0f, 1.0f);
                }
                if (cmdJson.contains("lighting_rig") && cmdJson["lighting_rig"].is_string()) {
                    cmd->setLightingRig = true;
                    cmd->lightingRig = cmdJson["lighting_rig"];
                }
                if (cmdJson.contains("fog_enabled") && cmdJson["fog_enabled"].is_boolean()) {
                    cmd->setFogEnabled = true;
                    cmd->fogEnabled = cmdJson["fog_enabled"];
                }
                if (cmdJson.contains("fog_density") || cmdJson.contains("fog_height") || cmdJson.contains("fog_falloff")) {
                    cmd->setFogParams = true;
                    if (cmdJson.contains("fog_density")) {
                        float v = ReadNumber(cmdJson["fog_density"], "fog_density", cmd->fogDensity);
                        cmd->fogDensity = std::max(v, 0.0f);
                    }
                    if (cmdJson.contains("fog_height")) {
                        cmd->fogHeight = ReadNumber(cmdJson["fog_height"], "fog_height", cmd->fogHeight);
                    }
                    if (cmdJson.contains("fog_falloff")) {
                        float v = ReadNumber(cmdJson["fog_falloff"], "fog_falloff", cmd->fogFalloff);
                        cmd->fogFalloff = std::max(v, 0.0f);
                    }
                }
                if (cmdJson.contains("sun_direction")) {
                    glm::vec3 dir;
                    if (ReadVec3(cmdJson["sun_direction"], "sun_direction", dir)) {
                        // Treat nearly-zero vectors as "no-op" and ignore them.
                        float len2 = glm::dot(dir, dir);
                        if (std::isfinite(len2) && len2 > 1e-4f) {
                            cmd->setSunDirection = true;
                            cmd->sunDirection = dir;
                        }
                    }
                }
                if (cmdJson.contains("sun_color")) {
                    glm::vec4 color4;
                    if (ReadVec4(cmdJson["sun_color"], "sun_color", color4)) {
                        cmd->setSunColor = true;
                        cmd->sunColor = glm::vec3(color4);
                    }
                }
                if (cmdJson.contains("sun_intensity")) {
                    cmd->setSunIntensity = true;
                    float v = ReadNumber(cmdJson["sun_intensity"], "sun_intensity", cmd->sunIntensity);
                    cmd->sunIntensity = std::max(v, 0.0f);
                }
                if (cmdJson.contains("ssao_enabled") && cmdJson["ssao_enabled"].is_boolean()) {
                    cmd->setSSAOEnabled = true;
                    cmd->ssaoEnabled = cmdJson["ssao_enabled"];
                }
                if (cmdJson.contains("ssao_radius")) {
                    cmd->setSSAOParams = true;
                    float v = ReadNumber(cmdJson["ssao_radius"], "ssao_radius", 0.5f);
                    cmd->ssaoRadius = std::clamp(v, 0.05f, 5.0f);
                }
                if (cmdJson.contains("ssao_bias")) {
                    cmd->setSSAOParams = true;
                    float v = ReadNumber(cmdJson["ssao_bias"], "ssao_bias", 0.025f);
                    cmd->ssaoBias = std::clamp(v, 0.0f, 0.1f);
                }
                if (cmdJson.contains("ssao_intensity")) {
                    cmd->setSSAOParams = true;
                    float v = ReadNumber(cmdJson["ssao_intensity"], "ssao_intensity", 1.0f);
                    cmd->ssaoIntensity = std::clamp(v, 0.0f, 4.0f);
                }

                commands.push_back(cmd);
            }
            else if (type == "modify_group" || type == "modify_pattern") {
                auto cmd = std::make_shared<ModifyGroupCommand>();
                if (cmdJson.contains("group") && cmdJson["group"].is_string()) {
                    cmd->groupName = cmdJson["group"];
                } else if (cmdJson.contains("pattern") && cmdJson["pattern"].is_string()) {
                    cmd->groupName = cmdJson["pattern"];
                }
                if (cmdJson.contains("position_offset")) {
                    cmd->hasPositionOffset = ReadVec3(cmdJson["position_offset"], "position_offset", cmd->positionOffset);
                }
                if (cmdJson.contains("scale_multiplier")) {
                    cmd->hasScaleMultiplier = ReadVec3(cmdJson["scale_multiplier"], "scale_multiplier", cmd->scaleMultiplier);
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
                if (cmdJson.contains("auto_place") && cmdJson["auto_place"].is_boolean()) {
                    cmd->autoPlace = cmdJson["auto_place"];
                }
                if (cmdJson.contains("anchor") && cmdJson["anchor"].is_string()) {
                    std::string anchor = cmdJson["anchor"];
                    std::transform(anchor.begin(), anchor.end(), anchor.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (anchor == "camera") {
                        cmd->anchorMode = AddLightCommand::AnchorMode::Camera;
                    } else if (anchor == "camera_forward" || anchor == "camera-forward" ||
                               anchor == "view" || anchor == "forward") {
                        cmd->anchorMode = AddLightCommand::AnchorMode::CameraForward;
                    }
                }
                if (cmdJson.contains("forward_distance")) {
                    cmd->forwardDistance = std::max(ReadNumber(cmdJson["forward_distance"], "forward_distance", 5.0f), 0.0f);
                }

                commands.push_back(cmd);
            }
            else if (type == "generate_texture") {
                auto cmd = std::make_shared<GenerateTextureCommand>();
                if (cmdJson.contains("target") && cmdJson["target"].is_string()) {
                    cmd->targetName = resolveTargetName(cmdJson["target"]);
                }
                if (cmdJson.contains("prompt") && cmdJson["prompt"].is_string()) {
                    cmd->prompt = cmdJson["prompt"];
                }
                if (cmdJson.contains("usage") && cmdJson["usage"].is_string()) {
                    cmd->usage = cmdJson["usage"];
                }
                if (cmdJson.contains("preset") && cmdJson["preset"].is_string()) {
                    cmd->materialPreset = cmdJson["preset"];
                }
                if (cmdJson.contains("width")) {
                    cmd->width = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["width"], "width", 0.0f)));
                }
                if (cmdJson.contains("height")) {
                    cmd->height = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["height"], "height", 0.0f)));
                }
                if (cmdJson.contains("seed")) {
                    cmd->seed = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["seed"], "seed", 0.0f)));
                }
                commands.push_back(cmd);
            }
            else if (type == "generate_envmap" || type == "generate_environment") {
                auto cmd = std::make_shared<GenerateEnvmapCommand>();
                if (cmdJson.contains("name") && cmdJson["name"].is_string()) {
                    cmd->name = cmdJson["name"];
                }
                if (cmdJson.contains("prompt") && cmdJson["prompt"].is_string()) {
                    cmd->prompt = cmdJson["prompt"];
                }
                if (cmdJson.contains("width")) {
                    cmd->width = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["width"], "width", 0.0f)));
                }
                if (cmdJson.contains("height")) {
                    cmd->height = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["height"], "height", 0.0f)));
                }
                if (cmdJson.contains("seed")) {
                    cmd->seed = static_cast<uint32_t>(std::max(0.0f, ReadNumber(cmdJson["seed"], "seed", 0.0f)));
                }
                commands.push_back(cmd);
            }
        }
    };

    // Pre-flight salvage for a common truncation pattern: the LLM starts a
    // multi-command "commands" array but the response is cut before the final
    // closing ']'. In that case, keep all complete objects and drop the
    // partial tail so we can still execute at least the first commands.
    std::string jsonToParse = jsonStr;
    {
        auto commandsPos = jsonToParse.find("\"commands\"");
        if (commandsPos != std::string::npos) {
            auto arrayStart = jsonToParse.find('[', commandsPos);
            if (arrayStart != std::string::npos) {
                auto arrayEnd = jsonToParse.find(']', arrayStart);
                if (arrayEnd == std::string::npos) {
                    auto lastObjEnd = jsonToParse.find_last_of('}');
                    if (lastObjEnd != std::string::npos && lastObjEnd > arrayStart) {
                        jsonToParse = jsonToParse.substr(0, lastObjEnd + 1);
                        jsonToParse.append("]}");
                    }
                }
            }
        }
    }

    try {
        auto j = json::parse(jsonToParse);
        parseFromJson(j);
        spdlog::info("Parsed {} commands from JSON", commands.size());
    }
    catch (const json::exception& e) {
        spdlog::error("JSON parsing error: {}", e.what());

        // First try a conservative salvage that keeps only fully closed
        // command objects inside the "commands" array. If that succeeds we
        // return immediately and skip the older fallback logic below.
        bool conservativelySalvaged = false;
        try {
            std::string fixed = jsonStr;

            auto lastNonWs = fixed.find_last_not_of(" \t\r\n");
            if (lastNonWs != std::string::npos) {
                fixed.resize(lastNonWs + 1);
            }

            auto commandsPos2 = fixed.find("\"commands\"");
            if (commandsPos2 != std::string::npos) {
                auto arrayStart2 = fixed.find('[', commandsPos2);
                if (arrayStart2 != std::string::npos) {
                    int depth = 0;
                    size_t lastObjEnd = std::string::npos;
                    for (size_t i = arrayStart2 + 1; i < fixed.size(); ++i) {
                        char c = fixed[i];
                        if (c == '{') {
                            ++depth;
                        } else if (c == '}') {
                            if (depth > 0) {
                                --depth;
                                if (depth == 0) {
                                    lastObjEnd = i;
                                }
                            }
                        }
                    }

                    if (lastObjEnd != std::string::npos && lastObjEnd > arrayStart2) {
                        std::string rebuilt = fixed.substr(0, lastObjEnd + 1);
                        if (rebuilt.find(']', arrayStart2) == std::string::npos) {
                            rebuilt.append("]}");
                        } else {
                            auto last = rebuilt.find_last_not_of(" \t\r\n");
                            if (last != std::string::npos && rebuilt[last] != '}') {
                                rebuilt.push_back('}');
                            }
                        }

                        spdlog::warn("Attempting conservative JSON salvage on LLM response");
                        auto jFixed = json::parse(rebuilt);
                        commands.clear();
                        parseFromJson(jFixed);
                        spdlog::info("Parsed {} commands from conservatively salvaged JSON", commands.size());
                        conservativelySalvaged = true;
                    }
                }
            }
        } catch (const json::exception& e2) {
            spdlog::error("Conservative JSON salvage parse failed: {}", e2.what());
        } catch (...) {
            // Ignore and fall back to legacy salvage below.
        }

        if (conservativelySalvaged) {
            return commands;
        }

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
                        // No closing ']' for the commands array - likely truncated after the last object.
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

    // As a final fallback, if no commands were parsed but the text clearly
    // contains a "commands" array, reconstruct a minimal JSON wrapper that
    // keeps only fully closed command objects and discards any partial tail.
    if (commands.empty() && jsonStr.find("\"commands\"") != std::string::npos) {
        try {
            std::string fixed = jsonStr;

            auto lastNonWs = fixed.find_last_not_of(" \t\r\n");
            if (lastNonWs != std::string::npos) {
                fixed.resize(lastNonWs + 1);
            }

            auto commandsPos = fixed.find("\"commands\"");
            if (commandsPos != std::string::npos) {
                auto arrayStart = fixed.find('[', commandsPos);
                if (arrayStart != std::string::npos) {
                    int depth = 0;
                    size_t lastObjEnd = std::string::npos;
                    for (size_t i = arrayStart + 1; i < fixed.size(); ++i) {
                        char c = fixed[i];
                        if (c == '{') {
                            ++depth;
                        } else if (c == '}') {
                            if (depth > 0) {
                                --depth;
                                if (depth == 0) {
                                    lastObjEnd = i;
                                }
                            }
                        }
                    }

                    if (lastObjEnd != std::string::npos && lastObjEnd > arrayStart) {
                        std::string elements =
                            fixed.substr(arrayStart + 1, lastObjEnd - (arrayStart + 1) + 1);
                        std::string rebuilt = "{\"commands\":[";
                        rebuilt.append(elements);
                        rebuilt.append("]}");

                        spdlog::warn("Attempting brace-based JSON salvage on LLM response");
                        auto jFixed = json::parse(rebuilt);
                        commands.clear();
                        parseFromJson(jFixed);
                        spdlog::info("Parsed {} commands from brace-salvaged JSON", commands.size());
                    }
                }
            }
        } catch (const json::exception& e2) {
            spdlog::error("Brace-based JSON salvage parse failed: {}", e2.what());
        }
    }

    return commands;
}

} // namespace Cortex::LLM
