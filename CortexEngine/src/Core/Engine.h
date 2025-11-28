#pragma once

#include <memory>
#include <chrono>
#include <string>
#include <deque>
#include <atomic>
#include <thread>
#include <entt/entt.hpp>
#include "Window.h"
#include "Graphics/RHI/DX12Device.h"
#include "Graphics/Renderer.h"
#include "Scene/ECS_Registry.h"
#include "LLM/LLMService.h"
#include "LLM/CommandQueue.h"
#include "AI/Vision/DreamerService.h"
#include "Utils/Result.h"
#include <glm/glm.hpp>

namespace Cortex {

struct EngineConfig {
    WindowConfig window;
    Graphics::DeviceConfig device;
    bool enableVSync = true;
    uint32_t targetFPS = 60;

    // Phase 2: LLM config
    bool enableLLM = true;
    LLM::LLMConfig llmConfig;

    // Phase 3: Dreamer config
    bool enableDreamer = true;
    AI::Vision::DreamerConfig dreamerConfig;

    // Camera control config
    float cameraBaseSpeed = 5.0f;
    float cameraSprintMultiplier = 3.0f;
    float mouseSensitivity = 0.003f;
};

// Main engine class - orchestrates the game loop
class Engine {
public:
    Engine() = default;
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Initialize engine
    Result<void> Initialize(const EngineConfig& config = {});

    // Main game loop (blocking)
    void Run();

    // Shutdown
    void Shutdown();

    // Phase 2: Submit natural language command to The Architect
    void SubmitNaturalLanguageCommand(const std::string& command);

    // Accessors
    [[nodiscard]] bool IsRunning() const { return m_running; }
    [[nodiscard]] Window* GetWindow() { return m_window.get(); }
    [[nodiscard]] Graphics::Renderer* GetRenderer() { return m_renderer.get(); }
    [[nodiscard]] Scene::ECS_Registry* GetRegistry() { return m_registry.get(); }

    // Logical focus target (most recently spawned or modified group/entity).
    void SetFocusTarget(const std::string& name) { m_focusTargetName = name; }
    [[nodiscard]] std::string GetFocusTarget() const { return m_focusTargetName; }

private:
    void ProcessInput();
    void Update(float deltaTime);
    void Render(float deltaTime);

    void InitializeScene();  // Create the spinning cube
    std::vector<std::shared_ptr<LLM::SceneCommand>> BuildHeuristicCommands(const std::string& text);

    void InitializeCameraController();
    void UpdateCameraController(float deltaTime);
    void SyncDebugMenuFromRenderer();

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Graphics::DX12Device> m_device;
    std::unique_ptr<Graphics::Renderer> m_renderer;
    std::unique_ptr<Scene::ECS_Registry> m_registry;

    // Phase 2: The Architect
    std::unique_ptr<LLM::LLMService> m_llmService;
    std::unique_ptr<LLM::CommandQueue> m_commandQueue;
    bool m_llmEnabled = false;
    std::atomic<bool> m_llmInitializing{false};
    std::thread m_llmInitThread;

    // Phase 3: The Dreamer (async texture generator)
    std::unique_ptr<AI::Vision::DreamerService> m_dreamerService;
    bool m_dreamerEnabled = false;

    // Text input state
    bool m_textInputMode = false;
    std::string m_textInputBuffer;

    bool m_running = false;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    uint32_t m_heuristicCounter = 0;

    float m_frameTime = 0.0f;
    uint32_t m_frameCount = 0;
    float m_fpsTimer = 0.0f;

    // HUD / debug overlay
    void RenderHUD();
    bool m_showHUD = true;
    std::deque<std::string> m_recentCommandMessages;

    // Camera control state
    void ShowCameraHelpOverlay();

    bool m_cameraControlActive = false;
    bool m_cameraControllerInitialized = false;
    bool m_droneFlightEnabled = false;
    bool m_cameraOrbitMode = false;
    bool m_cameraHelpShown = false;
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.0f;
    float m_cameraBaseSpeed = 5.0f;
    float m_cameraSprintMultiplier = 3.0f;
    float m_mouseSensitivity = 0.003f;
    float m_pendingMouseDeltaX = 0.0f;
    float m_pendingMouseDeltaY = 0.0f;
    glm::vec3 m_cameraVelocity{0.0f};
    float m_cameraAcceleration = 20.0f;   // units/s^2 thrust in drone mode
    float m_cameraDamping = 2.0f;         // exponential damping factor
    float m_cameraMaxSpeed = 40.0f;       // base max speed (scaled by sprint)
    float m_cameraRoll = 0.0f;            // roll angle in radians (drone mode)
    float m_cameraRollSpeed = 1.5f;       // radians/s roll rate
    float m_cameraRollDamping = 3.0f;     // how quickly roll recenters when no input
    entt::entity m_activeCameraEntity = entt::null;

    // Name of the current logical focus object/group (e.g., Pig_1 or SpinningCube).
    std::string m_focusTargetName;
};

} // namespace Cortex
