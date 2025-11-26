#include "Engine.h"
#include "ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Utils/MeshGenerator.h"
#include "LLM/SceneCommands.h"
#include "LLM/RegressionTests.h"
#include "UI/TextPrompt.h"
#include "UI/DebugMenu.h"
#include <windows.h>
#include "Scene/Components.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

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
    InitializeCameraController();
    ShowCameraHelpOverlay();

    // Initialize debug menu with current renderer/camera parameters
    if (m_renderer) {
        UI::DebugMenuState dbg{};
        dbg.exposure = m_renderer->GetExposure();
        dbg.shadowBias = m_renderer->GetShadowBias();
        dbg.shadowPCFRadius = m_renderer->GetShadowPCFRadius();
        dbg.cascadeLambda = m_renderer->GetCascadeSplitLambda();
        dbg.cascade0ResolutionScale = m_renderer->GetCascadeResolutionScale(0);
        dbg.bloomIntensity = m_renderer->GetBloomIntensity();
        dbg.cameraBaseSpeed = m_cameraBaseSpeed;
        UI::DebugMenu::Initialize(m_window->GetHWND(), dbg);
    }

    // Apply camera config
    m_cameraBaseSpeed = config.cameraBaseSpeed;
    m_cameraSprintMultiplier = config.cameraSprintMultiplier;
    m_mouseSensitivity = config.mouseSensitivity;

    // Phase 2: Initialize The Architect (LLM)
    if (config.enableLLM) {
        m_llmService = std::make_unique<LLM::LLMService>();
        m_commandQueue = std::make_unique<LLM::CommandQueue>();
        m_commandQueue->RefreshLookup(m_registry.get());

        auto llmResult = m_llmService->Initialize(config.llmConfig);
        if (llmResult.IsErr()) {
            spdlog::warn("LLM initialization failed: {}", llmResult.Error());
            spdlog::info("Continuing without LLM support");
        } else {
            m_llmEnabled = true;
            spdlog::info("The Architect is online!");
            spdlog::info("Press T to enter text input mode for natural language commands");

            // Run a small regression suite once at startup (logs only)
            LLM::RunRegressionTests();
        }
    }

    m_running = true;
    m_lastFrameTime = std::chrono::high_resolution_clock::now();

    spdlog::info("Cortex Engine initialized successfully!");
    spdlog::info("Ready to render. Press ESC to exit.");

    return Result<void>::Ok();
}

void Engine::ShowCameraHelpOverlay() {
    if (m_cameraHelpShown || !m_window) {
        return;
    }

    const char* message =
        "Camera controls:\n"
        "\n"
        "  Right mouse button  - Enable mouse look\n"
        "  Move mouse          - Look around\n"
        "  W / A / S / D       - Move forward / left / back / right\n"
        "  Q / E               - Move down / up\n"
        "  Shift (hold)        - Sprint (faster movement)\n"
        "  F1                  - Reset camera to default\n"
        "\n"
        "Lighting & shadows debug:\n"
        "  F3                  - Toggle shadows\n"
        "  F4                  - Cycle debug view (shaded/normal/rough/metal/albedo/cascades)\n"
        "  F5 / F6             - Decrease / increase shadow PCF radius\n"
        "  F7 / F8             - Decrease / increase shadow bias\n"
        "  F9 / F10            - Adjust cascade split lambda\n"
        "  F11 / F12           - Adjust near cascade resolution scale\n"
        "\n"
        "Press OK to continue. Press F2 later to show this help again.";

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Camera & Shadow Controls",
        message,
        m_window->GetSDLWindow());

    m_cameraHelpShown = true;
}

void Engine::Shutdown() {
    // Make shutdown idempotent and safe even if initialization failed early.
    m_running = false;

    UI::DebugMenu::Shutdown();

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

void Engine::RenderHUD() {
    if (!m_window || !m_registry || !m_renderer) {
        return;
    }

    // Gather camera information
    glm::vec3 camPos(0.0f);
    float camFov = 60.0f;
    bool haveCamera = false;

    if (m_activeCameraEntity != entt::null &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        auto& camera = m_registry->GetComponent<Scene::CameraComponent>(m_activeCameraEntity);
        camPos = transform.position;
        camFov = camera.fov;
        haveCamera = true;
    }

    // Renderer state
    auto* renderer = m_renderer.get();
    float exposure = renderer->GetExposure();
    bool shadows = renderer->GetShadowsEnabled();
    int debugMode = renderer->GetDebugViewMode();
    float shadowBias = renderer->GetShadowBias();
    float shadowPCF = renderer->GetShadowPCFRadius();
    float cascadeLambda = renderer->GetCascadeSplitLambda();
    float cascade0Scale = renderer->GetCascadeResolutionScale(0);
    float bloomIntensity = renderer->GetBloomIntensity();
    bool pcss = renderer->IsPCSS();
    bool fxaa = renderer->IsFXAAEnabled();

    // Approximate FPS from last frame time
    float fps = (m_frameTime > 0.0f) ? (1.0f / m_frameTime) : 0.0f;

    HWND hwnd = m_window->GetHWND();
    if (!hwnd) {
        return;
    }

    HDC dc = GetDC(hwnd);
    if (!dc) {
        return;
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 255, 0));

    int lineY = 8;
    auto drawLine = [&](const wchar_t* text) {
        TextOutW(dc, 8, lineY, text, static_cast<int>(wcslen(text)));
        lineY += 16;
    };

    wchar_t buffer[256];

    swprintf_s(buffer, L"FPS: %.1f  Frame: %.2f ms", fps, m_frameTime * 1000.0f);
    drawLine(buffer);

    if (haveCamera) {
        swprintf_s(buffer, L"Camera: (%.2f, %.2f, %.2f) FOV: %.1f",
                   camPos.x, camPos.y, camPos.z, camFov);
        drawLine(buffer);
    } else {
        drawLine(L"Camera: <none>");
    }

    swprintf_s(buffer, L"Exposure: %.2f  Bloom: %.2f", exposure, bloomIntensity);
    drawLine(buffer);

    swprintf_s(buffer, L"Shadows: %s  DebugView: %d  PCSS: %s  FXAA: %s",
               shadows ? L"ON" : L"OFF",
               debugMode,
               pcss ? L"ON" : L"OFF",
               fxaa ? L"ON" : L"OFF");
    drawLine(buffer);

    swprintf_s(buffer, L"Shadow Bias: %.6f  PCF: %.2f  Lambda: %.2f  Casc0Scale: %.2f",
               shadowBias, shadowPCF, cascadeLambda, cascade0Scale);
    drawLine(buffer);

    // Light count (from registry)
    size_t lightCount = 0;
    if (m_registry) {
        auto lightView = m_registry->View<Scene::LightComponent>();
        lightCount = static_cast<size_t>(lightView.size());
    }
    swprintf_s(buffer, L"Lights: %zu", lightCount);
    drawLine(buffer);

    // Per-light summary (up to two lights)
    if (m_registry && lightCount > 0) {
        drawLine(L"Light details:");
        auto view = m_registry->View<Scene::LightComponent>();
        size_t shown = 0;
        for (auto entity : view) {
            const auto& light = view.get<Scene::LightComponent>(entity);

            const wchar_t* typeLabel = L"Point";
            if (light.type == Scene::LightType::Directional) typeLabel = L"Dir";
            else if (light.type == Scene::LightType::Spot)   typeLabel = L"Spot";

            glm::vec3 pos(0.0f);
            if (m_registry->HasComponent<Scene::TransformComponent>(entity)) {
                pos = m_registry->GetComponent<Scene::TransformComponent>(entity).position;
            }

            std::wstring name;
            if (m_registry->HasComponent<Scene::TagComponent>(entity)) {
                const auto& tag = m_registry->GetComponent<Scene::TagComponent>(entity).tag;
                name.assign(tag.begin(), tag.end());
            } else {
                name = L"<unnamed>";
            }

            swprintf_s(buffer, L"  %s (%s) I=%.2f Pos=(%.1f, %.1f, %.1f)",
                       name.c_str(),
                       typeLabel,
                       light.intensity,
                       pos.x, pos.y, pos.z);
            drawLine(buffer);

            if (++shown >= 2) {
                break;
            }
        }
    }

    if (!m_recentCommandMessages.empty()) {
        drawLine(L"Last commands:");
        for (const auto& msg : m_recentCommandMessages) {
            std::wstring wmsg(msg.begin(), msg.end());
            if (wmsg.size() > 80) {
                wmsg.resize(80);
            }
            TextOutW(dc, 16, lineY, wmsg.c_str(), static_cast<int>(wmsg.size()));
            lineY += 16;
        }
    }

    ReleaseDC(hwnd, dc);
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
                else if (event.key.key == SDLK_F1) {
                    // Reset camera to default position/orientation
                    InitializeCameraController();
                    spdlog::info("Camera reset to default");
                }
                else if (event.key.key == SDLK_H) {
                    m_showHUD = !m_showHUD;
                    spdlog::info("HUD {}", m_showHUD ? "ENABLED" : "DISABLED");
                }
                else if (event.key.key == SDLK_P) {
                    if (m_renderer) {
                        bool enabled = !m_renderer->IsPCSS();
                        m_renderer->SetPCSS(enabled);
                        spdlog::info("PCSS contact-hardening {}", enabled ? "ENABLED" : "DISABLED");
                    }
                }
                else if (event.key.key == SDLK_X) {
                    if (m_renderer) {
                        bool enabled = !m_renderer->IsFXAAEnabled();
                        m_renderer->SetFXAAEnabled(enabled);
                        spdlog::info("FXAA {}", enabled ? "ENABLED" : "DISABLED");
                    }
                }
                else if (event.key.key == SDLK_F2) {
                    // Toggle debug slider menu
                    UI::DebugMenu::Toggle();
                }
                else if (event.key.key == SDLK_F5) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowPCFRadius(-0.5f);
                    }
                }
                else if (event.key.key == SDLK_F6) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowPCFRadius(0.5f);
                    }
                }
                else if (event.key.key == SDLK_F7) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(-0.0002f);
                    }
                }
                else if (event.key.key == SDLK_F8) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(0.0002f);
                    }
                }
                else if (event.key.key == SDLK_F9) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(-0.05f);
                    }
                }
                else if (event.key.key == SDLK_F10) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(0.05f);
                    }
                }
                else if (event.key.key == SDLK_F11) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, -0.1f);
                    }
                }
                else if (event.key.key == SDLK_F12) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, 0.1f);
                    }
                }
                else if (event.key.key == SDLK_F3) {
                    if (m_renderer) {
                        m_renderer->ToggleShadows();
                    }
                }
                else if (event.key.key == SDLK_F4) {
                    if (m_renderer) {
                        m_renderer->CycleDebugViewMode();
                    }
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_RIGHT && m_window) {
                    m_cameraControlActive = true;
                    SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), true);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_RIGHT && m_window) {
                    m_cameraControlActive = false;
                    SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), false);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (m_cameraControlActive) {
                    m_pendingMouseDeltaX += static_cast<float>(event.motion.xrel);
                    m_pendingMouseDeltaY += static_cast<float>(event.motion.yrel);
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
    if (m_commandQueue) {
        auto statuses = m_commandQueue->ConsumeStatus();
        for (const auto& s : statuses) {
            if (s.success) {
                spdlog::info("[Architect] {}", s.message);
            } else {
                spdlog::warn("[Architect] {}", s.message);
            }
            // Track recent command results for HUD display
            m_recentCommandMessages.push_back(s.message);
            constexpr size_t kMaxMessages = 5;
            while (m_recentCommandMessages.size() > kMaxMessages) {
                m_recentCommandMessages.pop_front();
            }
        }
    }

    // Apply debug menu slider values to renderer/camera
    if (m_renderer) {
        UI::DebugMenuState dbg = UI::DebugMenu::GetState();
        m_cameraBaseSpeed = dbg.cameraBaseSpeed;
        m_renderer->SetExposure(dbg.exposure);
        m_renderer->SetShadowBias(dbg.shadowBias);
        m_renderer->SetShadowPCFRadius(dbg.shadowPCFRadius);
        m_renderer->SetCascadeSplitLambda(dbg.cascadeLambda);
        m_renderer->AdjustCascadeResolutionScale(0, dbg.cascade0ResolutionScale - m_renderer->GetCascadeResolutionScale(0));
        m_renderer->SetBloomIntensity(dbg.bloomIntensity);
    }

    // Update active camera (fly controls)
    UpdateCameraController(deltaTime);

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

    // Render HUD overlay using GDI on top of the swap chain
    if (m_showHUD) {
        RenderHUD();
    }
}

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

    if (m_renderer && !wantsAdd && (wantsBrighter || wantsDarker || wantsShadowsOff || wantsShadowsOn)) {
        auto cmd = std::make_shared<LLM::ModifyRendererCommand>();
        if (wantsBrighter || wantsDarker) {
            cmd->setExposure = true;
            float current = m_renderer->GetExposure();
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

void Engine::InitializeCameraController() {
    if (!m_registry) {
        return;
    }

    m_activeCameraEntity = entt::null;
    m_cameraControllerInitialized = false;
    m_cameraControlActive = false;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;

    // Find active camera
    auto cameraView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    for (auto entity : cameraView) {
        auto& camera = cameraView.get<Scene::CameraComponent>(entity);
        if (camera.isActive) {
            m_activeCameraEntity = entity;
            break;
        }
    }

    if (m_activeCameraEntity == entt::null) {
        spdlog::warn("InitializeCameraController: no active camera found");
        return;
    }

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Reset to default position/orientation matching InitializeScene
    transform.position = glm::vec3(0.0f, 1.5f, -6.0f);
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(target - transform.position);
    transform.rotation = glm::quatLookAt(forward, up);

    // Derive yaw/pitch from forward vector (LH, +Z forward)
    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    float pitchLimit = glm::radians(89.0f);
    m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);

    m_cameraControllerInitialized = true;
}

void Engine::UpdateCameraController(float deltaTime) {
    if (!m_cameraControllerInitialized || !m_registry) {
        return;
    }

    if (m_activeCameraEntity == entt::null ||
        !m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) ||
        !m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        m_cameraControllerInitialized = false;
        return;
    }

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Apply mouse look deltas
    if (m_cameraControlActive) {
        float dx = m_pendingMouseDeltaX;
        float dy = m_pendingMouseDeltaY;
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;

        m_cameraYaw   += dx * m_mouseSensitivity;
        m_cameraPitch -= dy * m_mouseSensitivity;

        float pitchLimit = glm::radians(89.0f);
        m_cameraPitch = glm::clamp(m_cameraPitch, -pitchLimit, pitchLimit);
    } else {
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;
    }

    // Build camera basis from yaw/pitch
    float cosPitch = std::cos(m_cameraPitch);
    glm::vec3 forward(
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        std::cos(m_cameraYaw) * cosPitch
    );
    forward = glm::normalize(forward);

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    // Keyboard movement (WASD, QE) in camera-local axes
    if (m_cameraControlActive) {
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        auto keyDown = [&](SDL_Scancode scancode) {
            return scancode >= 0 && scancode < numKeys && keys[scancode];
        };

        glm::vec3 move(0.0f);
        if (keyDown(SDL_SCANCODE_W)) move += forward;
        if (keyDown(SDL_SCANCODE_S)) move -= forward;
        if (keyDown(SDL_SCANCODE_D)) move += right;
        if (keyDown(SDL_SCANCODE_A)) move -= right;
        if (keyDown(SDL_SCANCODE_E)) move += up;
        if (keyDown(SDL_SCANCODE_Q)) move -= up;

        if (glm::length(move) > 0.0f) {
            float speed = m_cameraBaseSpeed;
            if (keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT)) {
                speed *= m_cameraSprintMultiplier;
            }
            move = glm::normalize(move) * speed * deltaTime;
            transform.position += move;
        }
    }

    // Update camera rotation from forward/up
    transform.rotation = glm::quatLookAt(glm::normalize(forward), up);
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
    renderable.textures.albedo = m_renderer->GetPlaceholderTexture();
    renderable.textures.normal = m_renderer->GetPlaceholderNormal();
    renderable.textures.metallic = m_renderer->GetPlaceholderMetallic();
    renderable.textures.roughness = m_renderer->GetPlaceholderRoughness();
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
        r.textures.albedo = m_renderer->GetPlaceholderTexture();
        r.textures.normal = m_renderer->GetPlaceholderNormal();
        r.textures.metallic = m_renderer->GetPlaceholderMetallic();
        r.textures.roughness = m_renderer->GetPlaceholderRoughness();
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

    // Add a simple point light above the origin for forward lighting tests
    entt::entity lightEntity = m_registry->CreateEntity();
    auto& lightTransform = m_registry->AddComponent<Scene::TransformComponent>(lightEntity);
    lightTransform.position = glm::vec3(0.0f, 4.0f, -2.0f);
    auto& lightComp = m_registry->AddComponent<Scene::LightComponent>(lightEntity);
    lightComp.type = Scene::LightType::Point;
    lightComp.color = glm::vec3(1.0f, 0.95f, 0.8f);
    lightComp.intensity = 10.0f;
    lightComp.range = 15.0f;
    lightComp.castsShadows = false;

    spdlog::info("Scene initialized:");
    spdlog::info("{}", m_registry->DescribeScene());
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
            ss << "\nCamera: pos("
               << std::round(transform.position.x * 10.0f) / 10.0f << ","
               << std::round(transform.position.y * 10.0f) / 10.0f << ","
               << std::round(transform.position.z * 10.0f) / 10.0f << "), "
               << "fov=" << camera.fov;
            extra += ss.str();
            break;
        }
    }
    if (m_renderer) {
        std::ostringstream ss;
        ss << "\nRenderer: "
           << "exposure=" << m_renderer->GetExposure()
           << ", shadows=" << (m_renderer->GetShadowsEnabled() ? "on" : "off")
           << ", debug_mode=" << m_renderer->GetDebugViewMode()
           << ", bias=" << m_renderer->GetShadowBias()
           << ", pcf_radius=" << m_renderer->GetShadowPCFRadius()
           << ", cascade_lambda=" << m_renderer->GetCascadeSplitLambda();
        extra += ss.str();
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
