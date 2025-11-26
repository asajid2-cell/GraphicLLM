#include "RegressionTests.h"
#include "SceneCommands.h"
#include "CommandQueue.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/Renderer.h"
#include <spdlog/spdlog.h>

namespace Cortex::LLM {

void RunRegressionTests() {
    spdlog::info("[LLM Tests] Running command regression tests...");

    Scene::ECS_Registry registry;
    Graphics::Renderer renderer; // Not fully initialized; safe for simple setters
    CommandQueue queue;

    // Helper to run a JSON script and optionally log the resulting scene summary
    auto runScript = [&](const char* label, const std::string& json, bool logSummary) {
        spdlog::info("[LLM Tests] {}", label);
        auto commands = CommandParser::ParseJSON(json);
        if (commands.empty()) {
            spdlog::warn("[LLM Tests]  Parsed 0 commands");
            return;
        }

        for (auto& c : commands) {
            queue.Push(c);
        }
        queue.ExecuteAll(&registry, &renderer);
        auto statuses = queue.ConsumeStatus();
        for (const auto& s : statuses) {
            spdlog::info("  [{}] {}", s.success ? "ok" : "fail", s.message);
        }

        if (logSummary) {
            queue.RefreshLookup(&registry);
            auto summary = queue.BuildSceneSummary(&registry, 800);
            spdlog::info("[LLM Tests] Scene summary after '{}': {}", label, summary);
        }
    };

    // Test 1: add_light + modify_light
    runScript(
        "add_light + modify_light",
        R"({
            "commands":[
                {
                    "type":"add_light",
                    "light_type":"spot",
                    "name":"KeyLight",
                    "position":[0,4,-3],
                    "direction":[0,-1,0.25],
                    "color":[1.0,0.95,0.8,1.0],
                    "intensity":10.0,
                    "range":20.0
                },
                {
                    "type":"modify_light",
                    "target":"KeyLight",
                    "intensity":14.0,
                    "color":[1.0,0.9,0.7,1.0]
                }
            ]
        })",
        true);

    // Test 2: modify_renderer (exposure + shadows)
    runScript(
        "modify_renderer exposure + shadows",
        R"({
            "commands":[
                {
                    "type":"modify_renderer",
                    "exposure":1.8,
                    "shadows":false
                }
            ]
        })",
        false);

    // Test 3: multi-light setup (warm key + cool rim) plus shadow tuning
    runScript(
        "multi-light warm key + cool rim",
        R"({
            "commands":[
                {
                    "type":"add_light",
                    "light_type":"spot",
                    "name":"KeyLight",
                    "position":[0.0,4.0,-3.0],
                    "direction":[0.0,-1.0,0.3],
                    "color":[1.0,0.85,0.6,1.0],
                    "intensity":12.0,
                    "range":25.0
                },
                {
                    "type":"add_light",
                    "light_type":"spot",
                    "name":"RimLight",
                    "position":[-3.0,3.0,2.0],
                    "direction":[0.5,-0.3,-1.0],
                    "color":[0.6,0.8,1.0,1.0],
                    "intensity":8.0,
                    "range":25.0
                },
                {
                    "type":"modify_renderer",
                    "shadow_bias":0.0007,
                    "shadow_pcf_radius":2.0
                }
            ]
        })",
        true);

    // Test 4: modify_camera position/FOV
    // Create a simple camera entity
    {
        auto e = registry.CreateEntity();
        registry.AddComponent<Scene::TransformComponent>(e);
        auto& cam = registry.AddComponent<Scene::CameraComponent>(e);
        cam.isActive = true;
    }
    runScript(
        "modify_camera",
        R"({
            "commands":[
                {
                    "type":"modify_camera",
                    "position":[0.0,2.0,-8.0],
                    "fov":70.0
                }
            ]
        })",
        false);

    spdlog::info("[LLM Tests] Regression tests complete.");
}

} // namespace Cortex::LLM
