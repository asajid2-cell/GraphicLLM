#include "Engine.h"
#include "ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Utils/MeshGenerator.h"
#include "LLM/SceneCommands.h"
#include "UI/TextPrompt.h"
#include "Scene/Components.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

namespace Cortex {

Engine::~Engine() {
    Shutdown();
}

Result<void> Engine::Initialize(const EngineConfig& config) {
    spdlog::info("Initializing Cortex Engine...");
    spdlog::info("Version: 0.1.0 - Phase 1: Iron Foundation");

    // Create device
    m_device = std::make_unique<Graphics::DX12Device>();
    auto deviceResult = m_device->Initialize(config.device);
    if (deviceResult.IsErr()) {
        return Result<void>::Err("Failed to initialize device: " + deviceResult.Error());
    }

    // Create window
    m_window = std::make_unique<Window>();
    auto windowResult = m_window->Initialize(config.window, m_device.get());
    if (windowResult.IsErr()) {
        return Result<void>::Err("Failed to initialize window: " + windowResult.Error());
    }

    // Create renderer
    m_renderer = std::make_unique<Graphics::Renderer>();
    auto rendererResult = m_renderer->Initialize(m_device.get(), m_window.get());
    if (rendererResult.IsErr()) {
        return Result<void>::Err("Failed to initialize renderer: " + rendererResult.Error());
    }

    // Create ECS registry
    m_registry = std::make_unique<Scene::ECS_Registry>();

    // Set up service locator
    ServiceLocator::SetDevice(m_device.get());
    ServiceLocator::SetRenderer(m_renderer.get());
    ServiceLocator::SetRegistry(m_registry.get());

    // Initialize scene
    InitializeScene();

    // Phase 2: Initialize The Architect (LLM)
    if (config.enableLLM) {
        m_llmService = std::make_unique<LLM::LLMService>();
        m_commandQueue = std::make_unique<LLM::CommandQueue>();

        auto llmResult = m_llmService->Initialize(config.llmConfig);
        if (llmResult.IsErr()) {
            spdlog::warn("LLM initialization failed: {}", llmResult.Error());
            spdlog::info("Continuing without LLM support");
        } else {
            m_llmEnabled = true;
            spdlog::info("The Architect is online!");
            spdlog::info("Press T to enter text input mode for natural language commands");
        }
    }

    m_running = true;
    m_lastFrameTime = std::chrono::high_resolution_clock::now();

    spdlog::info("Cortex Engine initialized successfully!");
    spdlog::info("Ready to render. Press ESC to exit.");

    return Result<void>::Ok();
}

void Engine::Shutdown() {
    if (!m_running) {
        return;
    }

    m_running = false;

    // Phase 2: Shutdown LLM
    if (m_llmService) {
        m_llmService->Shutdown();
    }
    m_commandQueue.reset();
    m_llmService.reset();

    ServiceLocator::SetRegistry(nullptr);
    ServiceLocator::SetRenderer(nullptr);
    ServiceLocator::SetDevice(nullptr);

    m_registry.reset();
    m_renderer.reset();
    m_window.reset();
    m_device.reset();

    spdlog::info("Cortex Engine shut down");
}

void Engine::Run() {
    spdlog::info("Entering main loop...");

    while (m_running) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> deltaTime = currentTime - m_lastFrameTime;
        m_lastFrameTime = currentTime;

        float dt = deltaTime.count();
        m_frameTime = dt;

        // FPS counter
        m_frameCount++;
        m_fpsTimer += dt;
        if (m_fpsTimer >= 1.0f) {
            spdlog::debug("FPS: {} | Frame time: {:.2f}ms", m_frameCount, (m_frameTime * 1000.0f));
            m_frameCount = 0;
            m_fpsTimer = 0.0f;
        }

        // Game loop
        ProcessInput();
        Update(dt);
        Render(dt);
    }

    spdlog::info("Exiting main loop");
}

void Engine::ProcessInput() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Phase 2: Handle text input mode
        if (m_textInputMode) {
            switch (event.type) {
                case SDL_EVENT_TEXT_INPUT:
                    m_textInputBuffer += event.text.text;
                    spdlog::info("Input: {}", m_textInputBuffer);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                        // Submit command to The Architect
                        if (!m_textInputBuffer.empty() && m_llmEnabled) {
                            spdlog::info("Submitting to Architect: \"{}\"", m_textInputBuffer);
                            SubmitNaturalLanguageCommand(m_textInputBuffer);
                            m_textInputBuffer.clear();
                        }
                        m_textInputMode = false;
                        SDL_StopTextInput(m_window->GetSDLWindow());
                        spdlog::info("Text input mode: OFF");
                    }
                    else if (event.key.key == SDLK_ESCAPE) {
                        // Cancel text input
                        m_textInputBuffer.clear();
                        m_textInputMode = false;
                        SDL_StopTextInput(m_window->GetSDLWindow());
                        spdlog::info("Text input cancelled");
                    }
                    else if (event.key.key == SDLK_BACKSPACE && !m_textInputBuffer.empty()) {
                        m_textInputBuffer.pop_back();
                        spdlog::info("Input: {}", m_textInputBuffer);
                    }
                    break;
            }
            continue;  // Don't process other events in text input mode
        }

        // Normal event handling
        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    m_running = false;
                }
                else if (event.key.key == SDLK_T && m_llmEnabled) {
                    // Block to show native prompt; returns empty on cancel
                    auto text = UI::TextPrompt::Show(m_window->GetHWND());
                    if (!text.empty()) {
                        spdlog::info("Submitting to Architect: \"{}\"", text);
                        SubmitNaturalLanguageCommand(text);
                    } else {
                        spdlog::info("Text input cancelled");
                    }
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                m_window->OnResize(
                    static_cast<uint32_t>(event.window.data1),
                    static_cast<uint32_t>(event.window.data2)
                );
                break;
        }
    }
}

void Engine::Update(float deltaTime) {
    // Pump LLM callbacks on the main thread to avoid cross-thread scene mutations
    if (m_llmService) {
        m_llmService->PumpCallbacks();
    }

    // Phase 2: Execute pending LLM commands
    if (m_commandQueue && m_commandQueue->HasPending()) {
        m_commandQueue->ExecuteAll(m_registry.get(), m_renderer.get());
    }

    // Update all rotation components (spinning cube)
    auto view = m_registry->View<Scene::RotationComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& rotation = view.get<Scene::RotationComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        // Rotate around the specified axis
        float angle = rotation.speed * deltaTime;
        glm::quat rotationDelta = glm::angleAxis(angle, glm::normalize(rotation.axis));
        transform.rotation = rotationDelta * transform.rotation;
    }
}

void Engine::Render(float deltaTime) {
    m_renderer->Render(m_registry.get(), deltaTime);
}

std::vector<std::shared_ptr<LLM::SceneCommand>> Engine::BuildHeuristicCommands(const std::string& text) {
    std::vector<std::shared_ptr<LLM::SceneCommand>> out;

    // Lowercase copy for keyword checks
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    const bool wantsAdd = lower.find("add") != std::string::npos ||
                          lower.find("spawn") != std::string::npos ||
                          lower.find("create") != std::string::npos ||
                          lower.find("place") != std::string::npos ||
                          lower.find("drop") != std::string::npos;
    const bool wantsColorChange = lower.find("color") != std::string::npos ||
                                  lower.find("make it") != std::string::npos ||
                                  lower.find("turn it") != std::string::npos ||
                                  lower.find("turn") != std::string::npos;

    // If the user is not clearly asking to add, prefer to modify the existing showcase cube
    if (!wantsAdd && wantsColorChange) {
        auto cmd = std::make_shared<LLM::ModifyMaterialCommand>();
        cmd->targetName = "SpinningCube";
        cmd->setColor = true;
        if (lower.find("red") != std::string::npos) cmd->color = {1,0,0,1};
        else if (lower.find("green") != std::string::npos) cmd->color = {0,1,0,1};
        else if (lower.find("blue") != std::string::npos) cmd->color = {0,0,1,1};
        else if (lower.find("orange") != std::string::npos) cmd->color = {1.0f, 0.5f, 0.1f, 1};
        else if (lower.find("purple") != std::string::npos) cmd->color = {0.5f, 0.2f, 0.8f, 1};
        else if (lower.find("yellow") != std::string::npos) cmd->color = {1.0f, 0.9f, 0.2f, 1};
        else cmd->color = {0.8f, 0.8f, 0.8f, 1};
        out.push_back(cmd);
        return out;
    }

    // Default path: add new entity if user hinted at creation
    if (!wantsAdd) {
        return out;
    }

    auto cmd = std::make_shared<LLM::AddEntityCommand>();
    cmd->name = "LLM_Object_" + std::to_string(++m_heuristicCounter);

    if (lower.find("sphere") != std::string::npos) cmd->entityType = LLM::AddEntityCommand::EntityType::Sphere;
    else if (lower.find("plane") != std::string::npos) cmd->entityType = LLM::AddEntityCommand::EntityType::Plane;
    else cmd->entityType = LLM::AddEntityCommand::EntityType::Cube;

    if (lower.find("red") != std::string::npos) cmd->color = {1,0,0,1};
    else if (lower.find("green") != std::string::npos) cmd->color = {0,1,0,1};
    else if (lower.find("blue") != std::string::npos) cmd->color = {0,0,1,1};

    cmd->autoPlace = true;
    out.push_back(cmd);
    return out;
}

void Engine::InitializeScene() {
    spdlog::info("Initializing scene...");

    // Create a spinning cube
    entt::entity cubeEntity = m_registry->CreateCube(glm::vec3(0.0f, 0.0f, 0.0f), "SpinningCube");

    // Generate cube mesh
    auto cubeMesh = Utils::MeshGenerator::CreateCube();

    // Upload mesh to GPU
    auto uploadResult = m_renderer->UploadMesh(cubeMesh);
    if (uploadResult.IsErr()) {
        spdlog::error("Failed to upload cube mesh: {}", uploadResult.Error());
        return;
    }

    // Set up renderable component
    auto& renderable = m_registry->GetComponent<Scene::RenderableComponent>(cubeEntity);
    renderable.mesh = cubeMesh;
    renderable.texture = m_renderer->GetPlaceholderTexture();
    renderable.albedoColor = glm::vec4(0.8f, 0.3f, 0.2f, 1.0f);  // Orange-red color
    renderable.roughness = 0.6f;
    renderable.metallic = 0.1f;

    // Set up rotation
    auto& rotation = m_registry->GetComponent<Scene::RotationComponent>(cubeEntity);
    rotation.axis = glm::vec3(0.3f, 1.0f, 0.2f);  // Rotate on a diagonal axis
    rotation.speed = 1.5f;  // Radians per second

    // Additional cubes to exercise the renderer and HyperGeometry
    std::vector<glm::vec3> extraPositions = {
        { 2.0f, 0.0f, 0.0f },
        {-2.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 2.0f }
    };
    std::vector<glm::vec4> extraColors = {
        {0.2f, 0.8f, 1.0f, 1.0f},
        {0.6f, 0.2f, 0.9f, 1.0f},
        {0.2f, 0.9f, 0.3f, 1.0f}
    };
    for (size_t i = 0; i < extraPositions.size(); ++i) {
        entt::entity e = m_registry->CreateCube(extraPositions[i], "InstancedCube" + std::to_string(i));
        auto& r = m_registry->GetComponent<Scene::RenderableComponent>(e);
        r.mesh = cubeMesh;
        r.texture = m_renderer->GetPlaceholderTexture();
        r.albedoColor = extraColors[i % extraColors.size()];
        r.roughness = 0.5f;
        r.metallic = 0.2f;

        auto& rot = m_registry->GetComponent<Scene::RotationComponent>(e);
        rot.axis = glm::vec3(0.0f, 1.0f, 0.0f);
        rot.speed = 0.6f + static_cast<float>(i) * 0.3f;
    }

    // Create a camera
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<Scene::TransformComponent>(cameraEntity);
    // Place camera behind the origin on -Z to look forward (+Z is forward in our LH system)
    cameraTransform.position = glm::vec3(0.0f, 1.5f, -6.0f);
    cameraTransform.rotation = glm::quatLookAt(
        glm::normalize(glm::vec3(0.0f) - cameraTransform.position),  // Look at origin
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 55.0f;  // Slightly wider FOV for full scene framing
    camera.isActive = true;

    spdlog::info("Scene initialized:");
    spdlog::info("{}", m_registry->DescribeScene());
}

void Engine::SubmitNaturalLanguageCommand(const std::string& command) {
    if (!m_llmService || !m_llmEnabled) {
        spdlog::warn("LLM service not available");
        return;
    }

    // Submit to The Architect
    m_llmService->SubmitPrompt(command, [this, command](const LLM::LLMResponse& response) {
        if (!response.success) {
            spdlog::error("LLM inference failed: {}", response.text);
            return;
        }

        spdlog::info("Architect response received ({:.2f}s)", response.inferenceTime);
        spdlog::debug("Architect raw text: {}", response.text);

        // Parse JSON commands
        auto commands = LLM::CommandParser::ParseJSON(response.text);

        // Fallback: naive keyword add if no commands parsed
        if (commands.empty()) {
            spdlog::warn("No valid commands parsed from LLM response, applying heuristic add");
            auto fallback = BuildHeuristicCommands(command);
            commands.insert(commands.end(), fallback.begin(), fallback.end());
        }

        // Queue commands for execution on main thread
        m_commandQueue->PushBatch(commands);

        spdlog::info("Queued {} commands for execution", commands.size());
        for (const auto& c : commands) {
            spdlog::info("  {}", c->ToString());
        }
    });
}

} // namespace Cortex
