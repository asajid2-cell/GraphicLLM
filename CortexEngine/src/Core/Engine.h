#pragma once

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

    // Ray tracing config (DXR)
    bool enableRayTracing = false;
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
    void SetFocusTarget(const std::string& name);
    [[nodiscard]] std::string GetFocusTarget() const { return m_focusTargetName; }

    // Scene preset controls (used by debug UI / hotkeys)
    void ToggleScenePreset();

private:
    // High-level scene presets for easy switching between curated layouts.
    enum class ScenePreset {
        CornellBox = 0,
        DragonOverWater = 1,
    };

    void ProcessInput();
    void Update(float deltaTime);
    void Render(float deltaTime);

    void InitializeScene();
    std::vector<std::shared_ptr<LLM::SceneCommand>> BuildHeuristicCommands(const std::string& text);

    void RebuildScene(ScenePreset preset);
    void BuildCornellScene();
    void BuildDragonStudioScene();

    void InitializeCameraController();
    void UpdateCameraController(float deltaTime);
    void ApplyHeroVisualBaseline();
    void UpdateAutoDemo(float deltaTime);
    void SyncDebugMenuFromRenderer();
    void DebugDrawSceneGraph();
    void SetCameraToSceneDefault(Scene::TransformComponent& transform);

    void CaptureScreenshot();

    // Picking / camera helpers
    bool ComputeCameraRayFromMouse(float mouseX, float mouseY,
                                   glm::vec3& outOrigin,
                                   glm::vec3& outDirection);
    entt::entity PickEntityAt(float mouseX, float mouseY);
    void FrameSelectedEntity();

    // Translation / rotation gizmo helpers
    enum class GizmoAxis { None, X, Y, Z };
    enum class GizmoMode { Translate, Rotate };
    void UpdateGizmoHover();
    bool HitTestGizmoAxis(const glm::vec3& rayOrigin,
                          const glm::vec3& rayDir,
                          const glm::vec3& center,
                          const glm::vec3 axes[3],
                          float axisLength,
                          float threshold,
                          GizmoAxis& outAxis);

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
    double m_lastFrameTimeSeconds = 0.0;

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

    // Simple auto-demo orbit around the hero scene so the engine can present
    // itself without manual camera input.
    bool  m_autoDemoEnabled = false;
    float m_autoDemoTime = 0.0f;

    // Current selection for editor-style interactions (picking & framing).
    entt::entity m_selectedEntity = entt::null;

    // Translation gizmo interaction state
    GizmoAxis m_gizmoHoveredAxis = GizmoAxis::None;
    GizmoAxis m_gizmoActiveAxis  = GizmoAxis::None;
    GizmoMode m_gizmoMode        = GizmoMode::Translate;
    bool m_gizmoDragging = false;
    glm::vec2 m_lastMousePos{0.0f, 0.0f};
    glm::vec3 m_gizmoAxisDir{0.0f};
    glm::vec3 m_gizmoDragCenter{0.0f};
    glm::vec3 m_gizmoDragPlaneNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 m_gizmoDragPlanePoint{0.0f};
    glm::vec3 m_gizmoDragStartEntityPos{0.0f};
    float m_gizmoDragStartAxisParam = 0.0f;
    glm::quat m_gizmoDragStartEntityRot{1.0f, 0.0f, 0.0f, 0.0f};

    // Engine settings (debug menu) navigation state
    int  m_settingsSection = 0;
    int  m_settingsItem = 0;
    bool m_settingsOverlayVisible = false; // GPU overlay (M key)

    // Name of the current logical focus object/group (e.g., Pig_1 or SpinningCube).
    std::string m_focusTargetName;

    // Current scene preset used when (re)building the ECS layout.
    ScenePreset m_currentScenePreset = ScenePreset::DragonOverWater;
};

} // namespace Cortex
