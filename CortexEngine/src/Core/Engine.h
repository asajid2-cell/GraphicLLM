#pragma once

#include <memory>
#include <chrono>
#include <string>
#include "Window.h"
#include "Graphics/RHI/DX12Device.h"
#include "Graphics/Renderer.h"
#include "Scene/ECS_Registry.h"
#include "LLM/LLMService.h"
#include "LLM/CommandQueue.h"
#include "Utils/Result.h"

namespace Cortex {

struct EngineConfig {
    WindowConfig window;
    Graphics::DeviceConfig device;
    bool enableVSync = true;
    uint32_t targetFPS = 60;

    // Phase 2: LLM config
    bool enableLLM = true;
    LLM::LLMConfig llmConfig;
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

private:
    void ProcessInput();
    void Update(float deltaTime);
    void Render(float deltaTime);

    void InitializeScene();  // Create the spinning cube
    std::vector<std::shared_ptr<LLM::SceneCommand>> BuildHeuristicCommands(const std::string& text);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Graphics::DX12Device> m_device;
    std::unique_ptr<Graphics::Renderer> m_renderer;
    std::unique_ptr<Scene::ECS_Registry> m_registry;

    // Phase 2: The Architect
    std::unique_ptr<LLM::LLMService> m_llmService;
    std::unique_ptr<LLM::CommandQueue> m_commandQueue;
    bool m_llmEnabled = false;

    // Text input state
    bool m_textInputMode = false;
    std::string m_textInputBuffer;

    bool m_running = false;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    uint32_t m_heuristicCounter = 0;

    float m_frameTime = 0.0f;
    uint32_t m_frameCount = 0;
    float m_fpsTimer = 0.0f;
};

} // namespace Cortex
