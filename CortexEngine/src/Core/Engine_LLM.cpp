#include "Engine.h"
#include "LLM/SceneCommands.h"
#include "AI/Vision/DreamerService.h"
#include "Graphics/Renderer.h"
#include "Scene/Components.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Cortex {

std::vector<std::shared_ptr<LLM::SceneCommand>> Engine::BuildHeuristicCommands(const std::string& text) {
    std::vector<std::shared_ptr<LLM::SceneCommand>> out;

    // Lowercase copy for keyword checks
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    auto contains = [&lower](const std::string& token) {
        return lower.find(token) != std::string::npos;
    };

    const bool wantsAdd = contains("add") || contains("spawn") || contains("create") || contains("place") || contains("drop");
    const bool wantsColorChange = contains("color") || contains("make it") || contains("turn it") || contains("turn") || contains("paint");
    const bool refersToIt = contains(" it") || lower.rfind("it", 0) == 0 || contains("that") || contains("them");
    auto lastName = m_commandQueue ? m_commandQueue->GetLastSpawnedName(m_registry.get()) : std::nullopt;

    auto typeFromText = [&]() {
        using Type = LLM::AddEntityCommand::EntityType;
        if (contains("sphere")) return Type::Sphere;
        if (contains("plane")) return Type::Plane;
        if (contains("cylinder")) return Type::Cylinder;
        if (contains("pyramid")) return Type::Pyramid;
        if (contains("cone")) return Type::Cone;
        if (contains("torus")) return Type::Torus;
        return Type::Cube;
    };
    auto typeToString = [](LLM::AddEntityCommand::EntityType t) {
        switch (t) {
            case LLM::AddEntityCommand::EntityType::Sphere: return "Sphere";
            case LLM::AddEntityCommand::EntityType::Plane: return "Plane";
            case LLM::AddEntityCommand::EntityType::Cylinder: return "Cylinder";
            case LLM::AddEntityCommand::EntityType::Pyramid: return "Pyramid";
            case LLM::AddEntityCommand::EntityType::Cone: return "Cone";
            case LLM::AddEntityCommand::EntityType::Torus: return "Torus";
            default: return "Cube";
        }
    };
    auto patternElementFromType = [](LLM::AddEntityCommand::EntityType t) {
        switch (t) {
            case LLM::AddEntityCommand::EntityType::Sphere:   return std::string("sphere");
            case LLM::AddEntityCommand::EntityType::Plane:    return std::string("plane");
            case LLM::AddEntityCommand::EntityType::Cylinder: return std::string("cylinder");
            case LLM::AddEntityCommand::EntityType::Pyramid:  return std::string("pyramid");
            case LLM::AddEntityCommand::EntityType::Cone:     return std::string("cone");
            case LLM::AddEntityCommand::EntityType::Torus:    return std::string("torus");
            default:                                          return std::string("cube");
        }
    };

    auto colorFromText = [&]() -> std::optional<glm::vec4> {
        if (contains("red")) return glm::vec4(1,0,0,1);
        if (contains("green")) return glm::vec4(0,1,0,1);
        if (contains("blue")) return glm::vec4(0,0,1,1);
        if (contains("orange")) return glm::vec4(1.0f, 0.5f, 0.1f, 1);
        if (contains("purple")) return glm::vec4(0.5f, 0.2f, 0.8f, 1);
        if (contains("yellow")) return glm::vec4(1.0f, 0.9f, 0.2f, 1);
        if (contains("white")) return glm::vec4(1,1,1,1);
        if (contains("black")) return glm::vec4(0.1f,0.1f,0.1f,1);
        return std::nullopt;
    };

    auto parseCount = [&]() -> int {
        // Cap to avoid flooding the scene, but allow reasonably large counts.
        const int maxCount = 20;
        for (int digit = maxCount; digit >= 2; --digit) {
            if (lower.find(std::to_string(digit)) != std::string::npos) return std::min(digit, maxCount);
        }
        if (contains("twenty")) return 20;
        if (contains("nineteen")) return 19;
        if (contains("eighteen")) return 18;
        if (contains("seventeen")) return 17;
        if (contains("sixteen")) return 16;
        if (contains("fifteen")) return 15;
        if (contains("fourteen")) return 14;
        if (contains("thirteen")) return 13;
        if (contains("twelve")) return 12;
        if (contains("eleven")) return 11;
        if (contains("ten")) return 10;
        if (contains("nine")) return 9;
        if (contains("eight")) return 8;
        if (contains("seven")) return 7;
        if (contains("six")) return 6;
        if (contains("five")) return 5;
        if (contains("four")) return 4;
        if (contains("three")) return 3;
        if (contains("pair") || contains("two") || contains("couple")) return 2;
        return 1;
    };

    // Heuristics for global renderer tweaks when the user talks about brightness or shadows
    const bool wantsBrighter = contains("brighter") || contains("too dark") || contains("increase brightness") || contains("more light");
    const bool wantsDarker  = contains("darker") || contains("too bright") || contains("dim it") || contains("less bright");
    const bool wantsShadowsOff = contains("no shadows") || contains("turn off shadows") || contains("disable shadows");
    const bool wantsShadowsOn  = contains("cast shadows") || contains("turn on shadows") || contains("enable shadows");
    const bool mentionsWater   = contains("water");

    if (m_renderer && !wantsAdd && (wantsBrighter || wantsDarker || wantsShadowsOff || wantsShadowsOn)) {
        auto cmd = std::make_shared<LLM::ModifyRendererCommand>();
        if (wantsBrighter || wantsDarker) {
            cmd->setExposure = true;
            float current = m_renderer->GetQualityState().exposure;
            if (wantsBrighter) {
                cmd->exposure = std::max(current * 1.5f, current + 0.25f);
            } else {
                cmd->exposure = std::max(current * 0.65f, 0.1f);
            }
        }
        if (wantsShadowsOff || wantsShadowsOn) {
            cmd->setShadowsEnabled = true;
            cmd->shadowsEnabled = wantsShadowsOn;
        }
        out.push_back(cmd);
        return out;
    }

    // Simple water controls: raise/lower level or make waves calmer/rougher.
    if (m_renderer && !wantsAdd && mentionsWater) {
        auto cmd = std::make_shared<LLM::ModifyRendererCommand>();
        const auto water = m_renderer->GetWaterState();
        float level = water.levelY;
        float amp   = water.waveAmplitude;
        bool any = false;

        if (contains("raise") || contains("higher") || contains("deeper")) {
            cmd->setWaterLevel = true;
            cmd->waterLevel = level + 0.05f;
            any = true;
        } else if (contains("lower") || contains("shallower") || contains("less deep")) {
            cmd->setWaterLevel = true;
            cmd->waterLevel = level - 0.05f;
            any = true;
        }

        if (contains("calmer") || contains("still") || contains("smooth") || contains("less wavy")) {
            cmd->setWaterWaveAmplitude = true;
            cmd->waterWaveAmplitude = std::max(amp * 0.5f, 0.02f);
            any = true;
        } else if (contains("rougher") || contains("choppy") || contains("stronger waves") || contains("bigger waves")) {
            cmd->setWaterWaveAmplitude = true;
            cmd->waterWaveAmplitude = std::min(amp * 1.5f, 0.6f);
            any = true;
        }

        if (any) {
            out.push_back(cmd);
            return out;
        }
    }

    // If the user is not clearly asking to add, prefer to modify the existing showcase cube
    if (!wantsAdd && wantsColorChange) {
        auto cmd = std::make_shared<LLM::ModifyMaterialCommand>();
        if (refersToIt) {
            cmd->targetName = lastName.value_or("it");
        } else {
            cmd->targetName = "SpinningCube";
        }
        cmd->setColor = true;
        if (auto color = colorFromText()) cmd->color = *color;
        else cmd->color = {0.8f, 0.8f, 0.8f, 1};
        out.push_back(cmd);
        return out;
    }

    // Default path: add new entity or light if user hinted at creation
    if (!wantsAdd) {
        return out;
    }

    // Heuristic spotlight helper ("add a spotlight")
    if (contains("spotlight") || contains("spot light")) {
        auto cmd = std::make_shared<LLM::AddLightCommand>();
        cmd->lightType = LLM::AddLightCommand::LightType::Spot;
        cmd->name = "HeuristicSpotLight";
        cmd->position = glm::vec3(0.0f, 4.0f, -3.0f);
        cmd->direction = glm::vec3(0.0f, -1.0f, 0.3f);
        cmd->color = glm::vec3(1.0f, 0.95f, 0.8f);
        cmd->intensity = 12.0f;
        cmd->range = 20.0f;
        cmd->innerConeDegrees = 20.0f;
        cmd->outerConeDegrees = 35.0f;
        cmd->castsShadows = false;
        out.push_back(cmd);
        return out;
    }

    // If the user asks to "add" something that sounds like an animal,
    // vehicle, or structure but did not mention a primitive shape, route
    // this through the compound/motif system so we avoid spawning plain
    // cubes for things like "pig", "monster", or "fridge".
    auto emitCompound = [&](const std::string& templ, const std::string& baseName) {
        auto cmd = std::make_shared<LLM::AddCompoundCommand>();
        cmd->templateName = templ;
        cmd->instanceName = baseName + "_" + std::to_string(++m_heuristicCounter);
        cmd->position = glm::vec3(0.0f, 1.0f, -3.0f);
        float scale = (contains("giant") || contains("huge") || contains("massive") || contains("big")) ? 2.5f : 1.0f;
        cmd->scale = glm::vec3(scale);
        out.push_back(cmd);
    };

    auto maybeEmitCompound = [&]() -> bool {
        // Creatures / animals
        if (contains("pig"))       { emitCompound("pig", "Pig"); return true; }
        if (contains("cow"))       { emitCompound("cow", "Cow"); return true; }
        if (contains("horse"))     { emitCompound("horse", "Horse"); return true; }
        if (contains("dragon"))    { emitCompound("dragon", "Dragon"); return true; }
        if (contains("monster") || contains("godzilla")) {
            emitCompound("monster", "Monster"); return true;
        }
        if (contains("dog"))       { emitCompound("dog", "Dog"); return true; }
        if (contains("cat"))       { emitCompound("cat", "Cat"); return true; }
        if (contains("monkey"))    { emitCompound("monkey", "Monkey"); return true; }

        // Vehicles
        if (contains("car"))       { emitCompound("car", "Car"); return true; }
        if (contains("truck"))     { emitCompound("truck", "Truck"); return true; }
        if (contains("bus"))       { emitCompound("bus", "Bus"); return true; }
        if (contains("tank"))      { emitCompound("tank", "Tank"); return true; }
        if (contains("spaceship") || contains("ship") || contains("rocket")) {
            emitCompound("spaceship", "Spaceship"); return true;
        }
        if (contains("vehicle"))   { emitCompound("vehicle", "Vehicle"); return true; }

        // Structures / objects
        if (contains("tower"))     { emitCompound("tower", "Tower"); return true; }
        if (contains("castle"))    { emitCompound("castle", "Castle"); return true; }
        if (contains("arch"))      { emitCompound("arch", "Arch"); return true; }
        if (contains("bridge"))    { emitCompound("bridge", "Bridge"); return true; }
        if (contains("house"))     { emitCompound("house", "House"); return true; }
        if (contains("fridge"))    { emitCompound("fridge", "Fridge"); return true; }

        return false;
    };

    if (maybeEmitCompound()) {
        return out;
    }

    // Heuristic patterns for "messy/scattered row/grid/ring of X"
    const bool mentionsRow   = contains("row");
    const bool mentionsGrid  = contains("grid");
    const bool mentionsRing  = contains("ring") || contains("circle");
    const bool mentionsMessy =
        contains("messy") || contains("scattered") || contains("uneven") || contains("a bit random");

    if (mentionsMessy && (mentionsRow || mentionsGrid || mentionsRing)) {
        auto type = typeFromText();
        auto elementName = patternElementFromType(type);
        int count = std::max(1, parseCount());

        auto pattern = std::make_shared<LLM::AddPatternCommand>();
        if (mentionsGrid)      pattern->pattern = LLM::AddPatternCommand::PatternType::Grid;
        else if (mentionsRing) pattern->pattern = LLM::AddPatternCommand::PatternType::Ring;
        else                   pattern->pattern = LLM::AddPatternCommand::PatternType::Row;

        pattern->element = elementName;
        pattern->count = count;
        // Center around origin-ish; executor will handle spacing
        pattern->regionMin = glm::vec3(0.0f, 0.0f, -4.0f);
        pattern->regionMax = pattern->regionMin;
        pattern->hasRegionBox = false;
        pattern->spacing = glm::vec3(2.0f, 0.0f, 2.0f);
        pattern->hasSpacing = true;
        pattern->groupName = "HeuristicPattern_" + std::to_string(++m_heuristicCounter);
        pattern->jitter = true;
        pattern->jitterAmount = mentionsGrid ? 0.8f : 0.5f;
        out.push_back(pattern);
        return out;
    }

    // Heuristic "next to it / beside it" helper
    const bool mentionsNextTo = contains("next to") || contains("beside");
    if (refersToIt && mentionsNextTo) {
        auto type = typeFromText();
        std::string typeName = typeToString(type);

        glm::vec3 offset(1.0f, 0.0f, 0.0f);
        if (contains("left")) {
            offset = glm::vec3(-1.0f, 0.0f, 0.0f);
        } else if (contains("right")) {
            offset = glm::vec3(1.0f, 0.0f, 0.0f);
        } else if (contains("front") || contains("in front")) {
            offset = glm::vec3(0.0f, 0.0f, 1.0f);
        } else if (contains("behind") || contains("back")) {
            offset = glm::vec3(0.0f, 0.0f, -1.0f);
        }

        auto cmd = std::make_shared<LLM::AddEntityCommand>();
        cmd->entityType = type;
        cmd->name = "LLM_" + typeName + "_" + std::to_string(++m_heuristicCounter);
        cmd->autoPlace = true;
        cmd->hasPositionOffset = true;
        cmd->positionOffset = offset;
        if (auto color = colorFromText()) {
            cmd->color = *color;
        }
        out.push_back(cmd);
        return out;
    }

    const int count = parseCount();
    const float angleStep = 2.39996323f;
    const float radius = 1.6f;
    auto type = typeFromText();
    std::string typeName = typeToString(type);
    auto chosenColor = colorFromText();
    glm::vec3 basePos{0.0f, 1.0f, -3.0f};

    for (int i = 0; i < count; ++i) {
        auto cmd = std::make_shared<LLM::AddEntityCommand>();
        cmd->entityType = type;
        cmd->name = "LLM_" + typeName + "_" + std::to_string(++m_heuristicCounter);
        float angle = (static_cast<float>(i) + 1.0f) * angleStep;
        glm::vec3 offset = glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
        cmd->position = basePos + offset;
        cmd->autoPlace = true;
        if (chosenColor) cmd->color = *chosenColor;
        out.push_back(cmd);
    }
    return out;
}

void Engine::SubmitNaturalLanguageCommand(const std::string& command) {
    if (!m_llmService || !m_llmEnabled) {
        spdlog::warn("LLM service not available");
        return;
    }

    // Submit to The Architect
    std::string sceneSummary;
    bool hasShowcase = false;
    if (m_commandQueue) {
        sceneSummary = m_commandQueue->BuildSceneSummary(m_registry.get());
    }
    if (m_registry) {
        auto view = m_registry->View<Scene::TagComponent>();
        for (auto entity : view) {
            const auto& tag = view.get<Scene::TagComponent>(entity);
            if (tag.tag == "SpinningCube") {
                hasShowcase = true;
                break;
            }
        }
    }

    // Append camera and renderer state for richer context
    std::string extra;
    if (m_registry) {
        auto cameraView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : cameraView) {
            auto& camera = cameraView.get<Scene::CameraComponent>(entity);
            if (!camera.isActive) continue;
            auto& transform = cameraView.get<Scene::TransformComponent>(entity);
            std::ostringstream ss;

            float camSpeed = glm::length(m_cameraVelocity);
            float aspect = (m_window && m_window->GetHeight() > 0)
                ? m_window->GetAspectRatio()
                : 16.0f / 9.0f;
            float fovRad = glm::radians(camera.fov);
            float farPlane = camera.farPlane;
            float midDepth = std::clamp(farPlane * 0.1f, 5.0f, 50.0f);
            float halfHeight = std::tan(fovRad * 0.5f) * midDepth;
            float halfWidth = halfHeight * aspect;

            ss << "\nCamera: pos("
               << std::round(transform.position.x * 10.0f) / 10.0f << ","
               << std::round(transform.position.y * 10.0f) / 10.0f << ","
               << std::round(transform.position.z * 10.0f) / 10.0f << "), "
               << "fov=" << camera.fov
               << ", near=" << camera.nearPlane
               << ", far=" << camera.farPlane
               << ", mode=" << (m_droneFlightEnabled ? "drone" : "orbit")
               << ", velocity=" << std::round(camSpeed * 10.0f) / 10.0f
               << ", view_span_at_" << std::round(midDepth * 10.0f) / 10.0f
               << "m approx ("
               << std::round((halfWidth * 2.0f) * 10.0f) / 10.0f << "x"
               << std::round((halfHeight * 2.0f) * 10.0f) / 10.0f << ")";

            extra += ss.str();
            break;
        }
    }
    if (m_renderer) {
        const auto quality = m_renderer->GetQualityState();
        std::ostringstream ss;
        ss << "\nRenderer: "
           << "exposure=" << quality.exposure
           << ", shadows=" << (quality.shadowsEnabled ? "on" : "off")
           << ", debug_mode=" << quality.debugViewMode
           << ", bias=" << quality.shadowBias
           << ", pcf_radius=" << quality.shadowPCFRadius
           << ", cascade_lambda=" << quality.cascadeSplitLambda;
        extra += ss.str();
    }
    // Include last scene recipe (from the most recent scene_plan) to help the
    // LLM reason about prior layouts and extend patterns.
    if (m_commandQueue) {
        std::string recipe = m_commandQueue->GetLastSceneRecipe();
        if (!recipe.empty()) {
            extra += "\nPrevious scene recipe:\n";
            extra += recipe;
        }
    }

    if (!extra.empty()) {
        sceneSummary += extra;
    }

    m_llmService->SubmitPrompt(command, sceneSummary, hasShowcase, [this, command](const LLM::LLMResponse& response) {
        if (!response.success) {
            spdlog::error("LLM inference failed: {}", response.text);
            return;
        }

        spdlog::info("Architect response received ({:.2f}s)", response.inferenceTime);
        spdlog::debug("Architect raw text: {}", response.text);

        // Parse JSON commands directly; SceneCommands::ParseJSON handles any
        // necessary salvage. We only fall back to heuristics when the LLM
        // output is clearly not structured JSON (i.e., no "commands" key).
        const std::string& jsonText = response.text;
        auto commands = LLM::CommandParser::ParseJSON(jsonText, GetFocusTarget());

        bool sawCommandsKey = jsonText.find("\"commands\"") != std::string::npos;

        // Fallback: naive keyword add only if there was no structured "commands"
        // block at all. If the LLM attempted JSON, we prefer to do nothing over
        // silently injecting heuristic cubes on parse failure.
        if (commands.empty() && !sawCommandsKey) {
            spdlog::warn("No valid commands parsed and no 'commands' key; applying heuristic add");
            auto fallback = BuildHeuristicCommands(command);
            commands.insert(commands.end(), fallback.begin(), fallback.end());
        }

        // Split Architect output into:
        //  - normal scene commands executed via CommandQueue
        //  - Dreamer texture/envmap requests handled directly here.
        std::vector<std::shared_ptr<LLM::SceneCommand>> queueCommands;
        if (m_dreamerService && m_dreamerEnabled) {
            for (const auto& c : commands) {
                if (!c) continue;
                switch (c->type) {
                    case LLM::CommandType::GenerateTexture: {
                        auto* gen = static_cast<LLM::GenerateTextureCommand*>(c.get());
                        AI::Vision::TextureRequest req;
                        req.targetName = !gen->targetName.empty() ? gen->targetName : GetFocusTarget();
                        req.prompt = gen->prompt;
                        req.materialPreset = gen->materialPreset;
                        req.seed = gen->seed;
                        req.width = gen->width;
                        req.height = gen->height;

                        std::string usageLower = gen->usage;
                        std::transform(usageLower.begin(), usageLower.end(), usageLower.begin(),
                                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                        if (usageLower == "normal") {
                            req.usage = AI::Vision::TextureUsage::Normal;
                        } else if (usageLower == "roughness") {
                            req.usage = AI::Vision::TextureUsage::Roughness;
                        } else if (usageLower == "metalness" || usageLower == "metallic") {
                            req.usage = AI::Vision::TextureUsage::Metalness;
                        } else {
                            req.usage = AI::Vision::TextureUsage::Albedo;
                        }

                        // If the Architect requests an albedo map, automatically queue
                        // companion normal/roughness maps for richer materials.
                        m_dreamerService->SubmitRequest(req);
                        if (req.usage == AI::Vision::TextureUsage::Albedo) {
                            AI::Vision::TextureRequest normalReq = req;
                            normalReq.usage = AI::Vision::TextureUsage::Normal;
                            m_dreamerService->SubmitRequest(normalReq);

                            AI::Vision::TextureRequest roughReq = req;
                            roughReq.usage = AI::Vision::TextureUsage::Roughness;
                            m_dreamerService->SubmitRequest(roughReq);
                        }
                        spdlog::info("[Dreamer] Queued LLM texture job for '{}' (usage={}, preset='{}')",
                                     req.targetName, gen->usage, req.materialPreset);
                        break;
                    }
                    case LLM::CommandType::GenerateEnvmap: {
                        auto* gen = static_cast<LLM::GenerateEnvmapCommand*>(c.get());
                        AI::Vision::TextureRequest req;
                        req.targetName = !gen->name.empty() ? gen->name : std::string("Envmap");
                        req.prompt = gen->prompt;
                        req.materialPreset.clear();
                        req.seed = gen->seed;
                        req.width = gen->width ? gen->width : 1024;
                        req.height = gen->height ? gen->height : 512;
                        req.usage = AI::Vision::TextureUsage::Environment;
                        m_dreamerService->SubmitRequest(req);
                        spdlog::info("[Dreamer] Queued LLM environment job '{}'", req.targetName);
                        break;
                    }
                    default:
                        queueCommands.push_back(c);
                        break;
                }
            }
        } else {
            queueCommands = commands;
        }

        // Queue non-Dreamer commands for execution on main thread
        if (m_commandQueue && !queueCommands.empty()) {
            m_commandQueue->PushBatch(queueCommands);
            spdlog::info("Queued {} commands for execution", queueCommands.size());
            for (const auto& c : queueCommands) {
                spdlog::info("  {}", c->ToString());
            }
        }
    });
}

void Engine::EnqueueSceneCommand(std::shared_ptr<LLM::SceneCommand> command) {
    if (!m_commandQueue || !command) {
        return;
    }
    m_commandQueue->Push(std::move(command));
}

} // namespace Cortex
