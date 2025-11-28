#include "Engine.h"
#include "ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
#include "Utils/FileUtils.h"
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
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>

namespace Cortex {

Engine::~Engine() {
    Shutdown();
}

void Engine::SyncDebugMenuFromRenderer() {
    if (!m_renderer) {
        return;
    }

    UI::DebugMenuState dbg{};
    dbg.exposure = m_renderer->GetExposure();
    dbg.shadowBias = m_renderer->GetShadowBias();
    dbg.shadowPCFRadius = m_renderer->GetShadowPCFRadius();
    dbg.cascadeLambda = m_renderer->GetCascadeSplitLambda();
    dbg.cascade0ResolutionScale = m_renderer->GetCascadeResolutionScale(0);
    dbg.bloomIntensity   = m_renderer->GetBloomIntensity();
    dbg.cameraBaseSpeed  = m_cameraBaseSpeed;
    dbg.lightingRig      = 0;

    // Mirror renderer toggles into the debug menu state so the settings panel
    // and keyboard shortcuts stay in sync.
    dbg.shadowsEnabled   = m_renderer->GetShadowsEnabled();
    dbg.pcssEnabled      = m_renderer->IsPCSS();
    dbg.fxaaEnabled      = m_renderer->IsFXAAEnabled();
    dbg.taaEnabled       = m_renderer->IsTAAEnabled();
    dbg.ssaoEnabled      = m_renderer->GetSSAOEnabled();
    dbg.iblEnabled       = m_renderer->GetIBLEnabled();
    dbg.ssrEnabled       = m_renderer->GetSSREnabled();
    dbg.fogEnabled       = m_renderer->IsFogEnabled();
    dbg.rayTracingEnabled = m_renderer->IsRayTracingSupported() && m_renderer->IsRayTracingEnabled();

    UI::DebugMenu::SyncFromState(dbg);
}

namespace {
    using nlohmann::json;

    std::filesystem::path GetDebugMenuStatePath() {
        namespace fs = std::filesystem;
        // Store next to the executable / working directory
        return fs::current_path() / "debug_menu_state.json";
    }

    UI::DebugMenuState LoadDebugMenuStateOrDefault(const UI::DebugMenuState& defaults) {
        UI::DebugMenuState state = defaults;
        const auto path = GetDebugMenuStatePath();
        try {
            if (std::filesystem::exists(path)) {
                std::ifstream in(path);
                if (in) {
                    json j;
                    in >> j;
                    if (j.contains("exposure")) state.exposure = j.value("exposure", state.exposure);
                    if (j.contains("shadowBias")) state.shadowBias = j.value("shadowBias", state.shadowBias);
                    if (j.contains("shadowPCFRadius")) state.shadowPCFRadius = j.value("shadowPCFRadius", state.shadowPCFRadius);
                    if (j.contains("cascadeLambda")) state.cascadeLambda = j.value("cascadeLambda", state.cascadeLambda);
                    if (j.contains("cascade0ResolutionScale")) state.cascade0ResolutionScale = j.value("cascade0ResolutionScale", state.cascade0ResolutionScale);
                    if (j.contains("bloomIntensity")) state.bloomIntensity = j.value("bloomIntensity", state.bloomIntensity);
                    if (j.contains("cameraBaseSpeed")) state.cameraBaseSpeed = j.value("cameraBaseSpeed", state.cameraBaseSpeed);
                    if (j.contains("fractalAmplitude")) state.fractalAmplitude = j.value("fractalAmplitude", state.fractalAmplitude);
                    if (j.contains("fractalFrequency")) state.fractalFrequency = j.value("fractalFrequency", state.fractalFrequency);
                    if (j.contains("fractalOctaves")) state.fractalOctaves = j.value("fractalOctaves", state.fractalOctaves);
                    if (j.contains("fractalCoordMode")) state.fractalCoordMode = j.value("fractalCoordMode", state.fractalCoordMode);
                    if (j.contains("fractalScaleX")) state.fractalScaleX = j.value("fractalScaleX", state.fractalScaleX);
                    if (j.contains("fractalScaleZ")) state.fractalScaleZ = j.value("fractalScaleZ", state.fractalScaleZ);
                    if (j.contains("fractalLacunarity")) state.fractalLacunarity = j.value("fractalLacunarity", state.fractalLacunarity);
                    if (j.contains("fractalGain")) state.fractalGain = j.value("fractalGain", state.fractalGain);
                    if (j.contains("fractalWarpStrength")) state.fractalWarpStrength = j.value("fractalWarpStrength", state.fractalWarpStrength);
                    if (j.contains("fractalNoiseType")) state.fractalNoiseType = j.value("fractalNoiseType", state.fractalNoiseType);
                    if (j.contains("lightingRig")) state.lightingRig = j.value("lightingRig", state.lightingRig);
                    if (j.contains("rayTracingEnabled")) state.rayTracingEnabled = j.value("rayTracingEnabled", state.rayTracingEnabled);
                }
            }
        } catch (...) {
            // On any failure, fall back to defaults
        }
        return state;
    }

    void SaveDebugMenuStateToDisk(const UI::DebugMenuState& state) {
        const auto path = GetDebugMenuStatePath();
        try {
            json j;
            j["exposure"] = state.exposure;
            j["shadowBias"] = state.shadowBias;
            j["shadowPCFRadius"] = state.shadowPCFRadius;
            j["cascadeLambda"] = state.cascadeLambda;
            j["cascade0ResolutionScale"] = state.cascade0ResolutionScale;
            j["bloomIntensity"] = state.bloomIntensity;
            j["cameraBaseSpeed"] = state.cameraBaseSpeed;
            j["fractalAmplitude"] = state.fractalAmplitude;
            j["fractalFrequency"] = state.fractalFrequency;
            j["fractalOctaves"] = state.fractalOctaves;
            j["fractalCoordMode"] = state.fractalCoordMode;
            j["fractalScaleX"] = state.fractalScaleX;
            j["fractalScaleZ"] = state.fractalScaleZ;
            j["fractalLacunarity"] = state.fractalLacunarity;
            j["fractalGain"] = state.fractalGain;
            j["fractalWarpStrength"] = state.fractalWarpStrength;
            j["fractalNoiseType"] = state.fractalNoiseType;
            j["lightingRig"] = state.lightingRig;
            j["rayTracingEnabled"] = state.rayTracingEnabled;

            std::ofstream out(path);
            if (out) {
                out << j.dump(2);
            }
        } catch (...) {
            // Persistence is best-effort; ignore errors
        }
    }

    // Axis-aligned bounding box ray test in local space (unit cube [-0.5,0.5]^3).
    bool RayIntersectsAABB(const glm::vec3& rayOrigin,
                           const glm::vec3& rayDir,
                           const glm::vec3& aabbMin,
                           const glm::vec3& aabbMax,
                           float& outT) {
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; ++i) {
            float origin = rayOrigin[i];
            float dir = rayDir[i];
            if (std::abs(dir) < 1e-6f) {
                if (origin < aabbMin[i] || origin > aabbMax[i]) {
                    return false;
                }
                continue;
            }

            float invD = 1.0f / dir;
            float t0 = (aabbMin[i] - origin) * invD;
            float t1 = (aabbMax[i] - origin) * invD;
            if (t0 > t1) std::swap(t0, t1);

            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin) {
                return false;
            }
        }

        outT = tMin;
        return tMax >= 0.0f;
    }

    // Closest approach between mouse ray and gizmo axis.
    bool RayHitsAxis(const glm::vec3& rayOrigin,
                     const glm::vec3& rayDir,
                     const glm::vec3& axisOrigin,
                     const glm::vec3& axisDir,
                     float axisLength,
                     float threshold,
                     float& outRayT) {
        glm::vec3 d1 = glm::normalize(rayDir);
        glm::vec3 d2 = glm::normalize(axisDir);
        glm::vec3 w0 = rayOrigin - axisOrigin;

        float a = glm::dot(d1, d1);
        float b = glm::dot(d1, d2);
        float c = glm::dot(d2, d2);
        float d = glm::dot(d1, w0);
        float e = glm::dot(d2, w0);
        float denom = a * c - b * b;
        if (std::abs(denom) < 1e-6f) {
            return false;
        }

        float tRay = (b * e - c * d) / denom;
        float tAxis = (a * e - b * d) / denom;
        tAxis = std::clamp(tAxis, 0.0f, axisLength);

        if (tRay < 0.0f) {
            return false;
        }

        glm::vec3 pRay = rayOrigin + d1 * tRay;
        glm::vec3 pAxis = axisOrigin + d2 * tAxis;
        float dist = glm::length(pRay - pAxis);
        if (dist > threshold) {
            return false;
        }

        outRayT = tRay;
        return true;
    }

    bool RayPlaneIntersection(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              const glm::vec3& planePoint,
                              const glm::vec3& planeNormal,
                              glm::vec3& outPoint) {
        float denom = glm::dot(rayDir, planeNormal);
        if (std::abs(denom) < 1e-5f) {
            return false;
        }
        float t = glm::dot(planePoint - rayOrigin, planeNormal) / denom;
        if (t < 0.0f) {
            return false;
        }
        outPoint = rayOrigin + rayDir * t;
        return true;
    }

    // Scale gizmo axis length and hit-test thickness based on distance so the
    // on-screen size remains usable across a wide range of zoom levels.
    inline void ComputeGizmoScale(float distance,
                                  float& outAxisLength,
                                  float& outThreshold) {
        distance = std::max(distance, 0.1f);
        // Choose a base angular size; world size grows with distance.
        float axisLength = distance * 0.15f;
        axisLength = std::clamp(axisLength, 0.5f, 10.0f);
        float threshold = axisLength * 0.15f;

        outAxisLength = axisLength;
        outThreshold = threshold;
    }
}

Result<void> Engine::Initialize(const EngineConfig& config) {
    using clock = std::chrono::high_resolution_clock;
    const auto tStart = clock::now();

    spdlog::info("Initializing Cortex Engine...");
    spdlog::info("Version: 0.1.0 - Phase 1: Iron Foundation");

    // Create device
    m_device = std::make_unique<Graphics::DX12Device>();
    auto deviceResult = m_device->Initialize(config.device);
    if (deviceResult.IsErr()) {
        return Result<void>::Err("Failed to initialize device: " + deviceResult.Error());
    }
    const auto tAfterDevice = clock::now();
    spdlog::info("  DX12 device initialized in {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(tAfterDevice - tStart).count());

    // Create window
    m_window = std::make_unique<Window>();
    auto windowResult = m_window->Initialize(config.window, m_device.get());
    if (windowResult.IsErr()) {
        return Result<void>::Err("Failed to initialize window: " + windowResult.Error());
    }
    const auto tAfterWindow = clock::now();
    spdlog::info("  Window created in {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(tAfterWindow - tAfterDevice).count());

    // Create renderer
    m_renderer = std::make_unique<Graphics::Renderer>();
    auto rendererResult = m_renderer->Initialize(m_device.get(), m_window.get());
    if (rendererResult.IsErr()) {
        return Result<void>::Err("Failed to initialize renderer: " + rendererResult.Error());
    }
    const auto tAfterRenderer = clock::now();
    spdlog::info("  Renderer initialized in {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(tAfterRenderer - tAfterWindow).count());

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
    const auto tAfterScene = clock::now();
    spdlog::info("  Scene and camera initialized in {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(tAfterScene - tAfterRenderer).count());

    // Apply camera config
    m_cameraBaseSpeed = config.cameraBaseSpeed;
    m_cameraSprintMultiplier = config.cameraSprintMultiplier;
    m_mouseSensitivity = config.mouseSensitivity;
    // Tie flight dynamics to the current base speed so traversal scales with scene size.
    m_cameraMaxSpeed = std::max(15.0f, m_cameraBaseSpeed * 8.0f);

    // Optional ray tracing (DXR) toggle - off by default unless enabled and supported.
    if (m_renderer) {
        if (config.enableRayTracing && m_renderer->IsRayTracingSupported()) {
            m_renderer->SetRayTracingEnabled(true);
        } else {
            m_renderer->SetRayTracingEnabled(false);
        }
        spdlog::info("Ray tracing config: requested={}, supported={}, enabled={}",
                     config.enableRayTracing ? "ON" : "OFF",
                     m_renderer->IsRayTracingSupported() ? "YES" : "NO",
                     m_renderer->IsRayTracingEnabled() ? "ON" : "OFF");
    }

    // Phase 2: Initialize The Architect (LLM) asynchronously so the window appears sooner
    if (config.enableLLM) {
        m_llmService = std::make_unique<LLM::LLMService>();
        m_commandQueue = std::make_unique<LLM::CommandQueue>();
        m_commandQueue->RefreshLookup(m_registry.get());
        // Keep the engine's logical focus target in sync with LLM-driven edits.
        m_commandQueue->SetFocusCallback([this](const std::string& name) {
            SetFocusTarget(name);
        });
        // Allow the Architect to drive editor-style selection. The callback
        // returns the resolved scene tag when a match is found so that focus
        // and status messages use a concrete, canonical name.
        m_commandQueue->SetSelectionCallback([this](const std::string& name) -> std::optional<std::string> {
            if (!m_registry) return std::nullopt;
            auto view = m_registry->View<Scene::TagComponent, Scene::TransformComponent>();
            std::string targetLower = name;
            std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            entt::entity best = entt::null;
            std::string resolvedTag;

            for (auto e : view) {
                const auto& tag = view.get<Scene::TagComponent>(e);
                std::string tagLower = tag.tag;
                std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (tagLower == targetLower || tagLower.find(targetLower) != std::string::npos) {
                    best = e;
                    resolvedTag = tag.tag;
                    break;
                }
            }

            if (best != entt::null) {
                m_selectedEntity = best;
                SetFocusTarget(resolvedTag);
                spdlog::info("[Architect] Selected entity '{}' via LLM (query '{}')", resolvedTag, name);
                return resolvedTag;
            }

            return std::nullopt;
        });
        // Allow LLM commands to focus the camera on a named entity.
        m_commandQueue->SetFocusCameraCallback([this](const std::string& name) {
            if (!m_registry) return;
            if (!name.empty()) {
                auto view = m_registry->View<Scene::TagComponent, Scene::TransformComponent>();
                std::string targetLower = name;
                std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                entt::entity best = entt::null;
                for (auto e : view) {
                    const auto& tag = view.get<Scene::TagComponent>(e);
                    std::string tagLower = tag.tag;
                    std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (tagLower == targetLower || tagLower.find(targetLower) != std::string::npos) {
                        best = e;
                        break;
                    }
                }
                if (best != entt::null) {
                    m_selectedEntity = best;
                    FrameSelectedEntity();
                    spdlog::info("[Architect] Framed entity '{}' via LLM", name);
                }
            } else {
                FrameSelectedEntity();
            }
        });

        m_llmInitializing = true;
        auto llmConfig = config.llmConfig; // copy for the background thread
        spdlog::info("  Starting LLM initialization on a background thread...");

        m_llmInitThread = std::thread([this, llmConfig]() {
            using clock_local = std::chrono::high_resolution_clock;
            const auto tLLMStart = clock_local::now();

            auto llmResult = m_llmService->Initialize(llmConfig);
            if (llmResult.IsErr()) {
                spdlog::warn("LLM initialization failed: {}", llmResult.Error());
                spdlog::info("Continuing without LLM support");
            } else {
                const auto tLLMEnd = clock_local::now();
                const auto llmMs = std::chrono::duration_cast<std::chrono::milliseconds>(tLLMEnd - tLLMStart).count();

                m_llmEnabled = true;
                spdlog::info("The Architect is online! (LLM ready in {} ms)", llmMs);
                spdlog::info("Press T to enter text input mode for natural language commands");

                // Run a small regression suite once after LLM is ready (logs only)
                LLM::RunRegressionTests();
            }

            m_llmInitializing = false;
        });
    }

    // Phase 3: Initialize The Dreamer (async texture generator). This is a lightweight
    // CPU-only service that produces RGBA8 pixels; the Engine uploads them via the
    // Renderer on the main thread.
    if (config.enableDreamer) {
        m_dreamerService = std::make_unique<AI::Vision::DreamerService>();
        auto dreamerResult = m_dreamerService->Initialize(config.dreamerConfig);
        if (dreamerResult.IsErr()) {
            spdlog::warn("Dreamer initialization failed: {}", dreamerResult.Error());
        } else {
            m_dreamerEnabled = true;
            spdlog::info("The Dreamer is online! (async texture generation ready)");
        }
    }

    // Initialize debug menu with current / persisted renderer & camera parameters
    if (m_renderer && m_window) {
        UI::DebugMenuState dbg{};
        dbg.exposure = m_renderer->GetExposure();
        dbg.shadowBias = m_renderer->GetShadowBias();
        dbg.shadowPCFRadius = m_renderer->GetShadowPCFRadius();
        dbg.cascadeLambda = m_renderer->GetCascadeSplitLambda();
        dbg.cascade0ResolutionScale = m_renderer->GetCascadeResolutionScale(0);
        dbg.bloomIntensity = m_renderer->GetBloomIntensity();
        dbg.cameraBaseSpeed = m_cameraBaseSpeed;
        // Initialize fractal debug defaults (can be overridden from JSON)
        dbg.fractalAmplitude = 0.0f;
        dbg.fractalFrequency = 0.5f;
        dbg.fractalOctaves = 4.0f;
        dbg.fractalCoordMode = 1.0f;
        dbg.fractalScaleX = 1.0f;
        dbg.fractalScaleZ = 1.0f;
        dbg.fractalLacunarity = 2.0f;
        dbg.fractalGain = 0.5f;
        dbg.fractalWarpStrength = 0.0f;
        dbg.fractalNoiseType = 0.0f;

        dbg = LoadDebugMenuStateOrDefault(dbg);

        // Apply persisted values back into renderer / camera so the scene matches the UI
        m_renderer->SetExposure(dbg.exposure);
        m_renderer->SetShadowBias(dbg.shadowBias);
        m_renderer->SetShadowPCFRadius(dbg.shadowPCFRadius);
        m_renderer->SetCascadeSplitLambda(dbg.cascadeLambda);
        m_renderer->AdjustCascadeResolutionScale(0, dbg.cascade0ResolutionScale - m_renderer->GetCascadeResolutionScale(0));
        m_renderer->SetBloomIntensity(dbg.bloomIntensity);
        m_renderer->SetFractalParams(
            dbg.fractalAmplitude,
            dbg.fractalFrequency,
            dbg.fractalOctaves,
            dbg.fractalCoordMode,
            dbg.fractalScaleX,
            dbg.fractalScaleZ,
            dbg.fractalLacunarity,
            dbg.fractalGain,
            dbg.fractalWarpStrength,
            dbg.fractalNoiseType);
        m_cameraBaseSpeed = dbg.cameraBaseSpeed;
        m_cameraMaxSpeed = std::max(15.0f, m_cameraBaseSpeed * 8.0f);

        UI::DebugMenu::Initialize(m_window->GetHWND(), dbg);
    }

    m_running = true;
    m_lastFrameTime = std::chrono::high_resolution_clock::now();

    const auto tEnd = clock::now();
    spdlog::info("Cortex Engine initialized successfully in {} ms (without LLM load).",
        std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count());
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
        "  Left mouse button   - Select entity under cursor\n"
        "  F                   - Frame selected entity (focus camera)\n"
        "  Right mouse button  - Orbit camera around focus (hold)\n"
        "  Middle mouse button - Pan focus point (hold)\n"
        "  Mouse wheel         - Zoom in/out around focus\n"
        "  G                   - Toggle drone/free-flight camera (auto-forward)\n"
        "  W / A / S / D       - Move forward / left / back / right\n"
        "  Space / Ctrl        - Move up / down (drone mode)\n"
        "  Q / E               - Roll left / right (drone mode)\n"
        "  Shift (hold)        - Sprint (faster movement)\n"
        "  F1                  - Reset camera to default\n"
        "\n"
        "Lighting & debug:\n"
        "  F3                  - Toggle shadows\n"
        "  F4                  - Cycle debug view (shaded/normal/rough/metal/albedo/cascades/IBL/SSAO/SSR/SceneGraph)\n"
        "  Z                   - Toggle temporal AA (TAA) on/off\n"
        "  R                   - Cycle SSR/SSAO (both on -> SSR only -> SSAO only -> both off)\n"
        "  F5                  - Increase shadow PCF radius\n"
        "  F7 / F8             - Decrease / increase shadow bias\n"
        "  F9 / F10            - Adjust cascade split lambda\n"
        "  F11 / F12           - Adjust near cascade resolution scale\n"
        "  F2                  - Reset debug settings and show debug menu\n"
        "  V                   - Toggle ray tracing (if supported)\n"
        "  C                   - Cycle environment preset\n"
        "\n"
        "Press OK to continue.";

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

    // Ensure any asynchronous LLM initialization has completed before tearing down.
    if (m_llmInitThread.joinable()) {
        m_llmInitThread.join();
    }

    // Persist last used debug menu state
    SaveDebugMenuStateToDisk(UI::DebugMenu::GetState());
    UI::DebugMenu::Shutdown();

    // Phase 2: Shutdown LLM
    if (m_llmService) {
        m_llmService->Shutdown();
    }
    m_commandQueue.reset();
    m_llmService.reset();

    // Phase 3: Shutdown Dreamer
    if (m_dreamerService) {
        m_dreamerService->Shutdown();
        m_dreamerService.reset();
    }

    ServiceLocator::SetRegistry(nullptr);
    ServiceLocator::SetRenderer(nullptr);
    ServiceLocator::SetDevice(nullptr);

    m_registry.reset();
    m_renderer.reset();
    m_window.reset();
    m_device.reset();

    spdlog::info("Cortex Engine shut down");
}

void Engine::SetFocusTarget(const std::string& name) {
    m_focusTargetName = name;

    // Keep the LLM command queue's notion of the current focus entity in sync
    // with the editor-style selection. When the Architect issues commands
    // targeting this name, they will preferentially operate on this concrete
    // entity instead of relying solely on name-based lookup.
    if (m_commandQueue) {
        entt::entity focusId = entt::null;
        if (m_registry && m_selectedEntity != entt::null) {
            auto& reg = m_registry->GetRegistry();
            if (reg.valid(m_selectedEntity)) {
                focusId = m_selectedEntity;
            }
        }
        m_commandQueue->SetCurrentFocus(name, focusId);
    }
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

    // Always show top-level FPS/camera
    swprintf_s(buffer, L"FPS: %.1f  Frame: %.2f ms", fps, m_frameTime * 1000.0f);
    drawLine(buffer);

    if (haveCamera) {
        swprintf_s(buffer, L"Camera: (%.2f, %.2f, %.2f) FOV: %.1f",
                   camPos.x, camPos.y, camPos.z, camFov);
        drawLine(buffer);
    } else {
        drawLine(L"Camera: <none>");
    }

    // Only show detailed renderer/light/command information in debug screen mode
    if (debugMode == 6) {
        swprintf_s(buffer, L"Exposure (EV): %.2f  Bloom: %.2f", exposure, bloomIntensity);
        drawLine(buffer);

        swprintf_s(buffer, L"Shadows: %s  DebugView: %d  PCSS: %s  FXAA: %s",
                   shadows ? L"ON" : L"OFF",
                   debugMode,
                   pcss ? L"ON" : L"OFF",
                   fxaa ? L"ON" : L"OFF");
        drawLine(buffer);

        swprintf_s(buffer, L"Shadow Bias: %.6f  PCF Radius: %.2f  Cascade \u03bb: %.2f  NearCascScale: %.2f",
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
    }

    // Selection / camera mode / controls hint (always shown)
    std::wstring selName = L"<none>";
    if (m_selectedEntity != entt::null &&
        m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
        selName.assign(tag.tag.begin(), tag.tag.end());
    }

    swprintf_s(buffer, L"Selected: %s  Focus: %hs  Mode: %hs",
               selName.c_str(),
               m_focusTargetName.empty() ? "<none>" : m_focusTargetName.c_str(),
               m_droneFlightEnabled ? "Drone" : "Orbit");
    drawLine(buffer);

    drawLine(L"LMB: select  F: frame  G: drone  RMB: orbit  MMB: pan");

    // Engine settings overlay (toggled with M). Draw a simple side panel with
    // categories and current values inspired by DCC render settings UIs.
    if (UI::DebugMenu::IsVisible()) {
        UI::DebugMenuState state = UI::DebugMenu::GetState();

        // Panel rectangle on the right side
        int panelX = static_cast<int>(m_window->GetWidth()) - 320;
        int panelY = 40;
        int panelW = 300;
        int panelH = 260;

        HBRUSH brush = CreateSolidBrush(RGB(10, 10, 10));
        RECT panelRect{ panelX, panelY, panelX + panelW, panelY + panelH };
        FillRect(dc, &panelRect, brush);
        DeleteObject(brush);
        FrameRect(dc, &panelRect, (HBRUSH)GetStockObject(WHITE_BRUSH));

        int y = panelY + 8;
        auto drawPanelLine = [&](const wchar_t* text, COLORREF color) {
            SetTextColor(dc, color);
            TextOutW(dc, panelX + 12, y, text, static_cast<int>(wcslen(text)));
            y += 18;
        };

        drawPanelLine(L"Engine Settings (M to close)", RGB(0, 200, 255));
        y += 4;

        struct Row {
            const wchar_t* label;
            float value;
            int sectionIndex;
        };

        Row rows[] = {
            { L"[Render] Exposure (EV)",           state.exposure,                       0 },
            { L"[Render] Bloom Intensity",         state.bloomIntensity,                 1 },
            { L"[Shadows] Shadows Enabled",        state.shadowsEnabled ? 1.0f : 0.0f,   2 },
            { L"[Shadows] PCSS (Soft Shadows)",    state.pcssEnabled ? 1.0f : 0.0f,      3 },
            { L"[Shadows] Bias",                   state.shadowBias,                     4 },
            { L"[Shadows] PCF Radius",             state.shadowPCFRadius,                5 },
            { L"[Shadows] Cascade Lambda",         state.cascadeLambda,                  6 },
            { L"[AA] FXAA",                        state.fxaaEnabled ? 1.0f : 0.0f,      7 },
            { L"[AA] TAA",                         state.taaEnabled ? 1.0f : 0.0f,       8 },
            { L"[Reflections] SSR",                state.ssrEnabled ? 1.0f : 0.0f,       9 },
            { L"[AO] SSAO",                        state.ssaoEnabled ? 1.0f : 0.0f,      10 },
            { L"[Environment] IBL",                state.iblEnabled ? 1.0f : 0.0f,       11 },
            { L"[Environment] Fog",                state.fogEnabled ? 1.0f : 0.0f,       12 },
            { L"[Camera] Base Speed",              state.cameraBaseSpeed,                13 },
            { L"[Advanced] Ray Tracing",           state.rayTracingEnabled ? 1.0f : 0.0f,14 }
        };

        const int rowCount = static_cast<int>(std::size(rows));
        for (int i = 0; i < rowCount; ++i) {
            const Row& r = rows[i];
            wchar_t line[256];
            if (r.sectionIndex == 2 || r.sectionIndex == 3 || r.sectionIndex == 7 ||
                r.sectionIndex == 8 || r.sectionIndex == 9 || r.sectionIndex == 10 ||
                r.sectionIndex == 11 || r.sectionIndex == 12 || r.sectionIndex == 14) {
                swprintf_s(line, L"%s : %s", r.label, (state.rayTracingEnabled ? L"ON" : L"OFF"));
            } else {
                swprintf_s(line, L"%s : %.3f", r.label, r.value);
            }
            COLORREF color = (m_settingsSection == r.sectionIndex)
                ? RGB(255, 255, 0)
                : RGB(200, 200, 200);
            drawPanelLine(line, color);
        }

        y += 4;
        drawPanelLine(L"Use UP/DOWN to select row", RGB(180, 180, 180));
        drawPanelLine(L"LEFT/RIGHT to tweak value", RGB(180, 180, 180));
        drawPanelLine(L"SPACE/ENTER to toggle flags", RGB(180, 180, 180));
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

            case SDL_EVENT_KEY_DOWN: {
                bool settingsVisible = UI::DebugMenu::IsVisible();

                // When the settings menu is visible, repurpose keys for menu
                // navigation and parameter adjustments instead of camera
                // controls. ESC closes the menu rather than quitting.
                if (settingsVisible) {
                    UI::DebugMenuState state = UI::DebugMenu::GetState();
                    const float stepSmall = 0.05f;

                    if (event.key.key == SDLK_ESCAPE) {
                        UI::DebugMenu::SetVisible(false);
                        break;
                    }
                    // Section navigation: up/down move the highlight through
                    // the list of settings sections.
                    if (event.key.key == SDLK_UP) {
                        m_settingsSection = std::max(0, m_settingsSection - 1);
                        break;
                    }
                    if (event.key.key == SDLK_DOWN) {
                        constexpr int kMaxSection = 14;
                        m_settingsSection = std::min(kMaxSection, m_settingsSection + 1);
                        break;
                    }

                    // Adjust the currently highlighted setting with left/right.
                    if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                        const float dir = (event.key.key == SDLK_RIGHT) ? 1.0f : -1.0f;

                        switch (m_settingsSection) {
                            case 0: // Exposure
                                state.exposure = glm::clamp(state.exposure + dir * stepSmall, 0.0f, 10.0f);
                                break;
                            case 1: // Bloom intensity
                                state.bloomIntensity = glm::clamp(state.bloomIntensity + dir * stepSmall, 0.0f, 5.0f);
                                break;
                            case 2: // Shadows enabled
                                state.shadowsEnabled = !state.shadowsEnabled;
                                break;
                            case 3: // PCSS
                                state.pcssEnabled = !state.pcssEnabled;
                                break;
                            case 4: // Shadow bias
                                state.shadowBias = glm::clamp(state.shadowBias + dir * stepSmall * 0.0005f, 0.00005f, 0.01f);
                                break;
                            case 5: // Shadow PCF radius
                                state.shadowPCFRadius = glm::clamp(state.shadowPCFRadius + dir * stepSmall, 0.0f, 5.0f);
                                break;
                            case 6: // Cascade lambda
                                state.cascadeLambda = glm::clamp(state.cascadeLambda + dir * stepSmall, 0.0f, 1.0f);
                                break;
                            case 7: // FXAA
                                state.fxaaEnabled = !state.fxaaEnabled;
                                break;
                            case 8: // TAA
                                state.taaEnabled = !state.taaEnabled;
                                break;
                            case 9: // SSR
                                state.ssrEnabled = !state.ssrEnabled;
                                break;
                            case 10: // SSAO
                                state.ssaoEnabled = !state.ssaoEnabled;
                                break;
                            case 11: // IBL
                                state.iblEnabled = !state.iblEnabled;
                                break;
                            case 12: // Fog
                                state.fogEnabled = !state.fogEnabled;
                                break;
                            case 13: // Camera base speed
                                state.cameraBaseSpeed = glm::clamp(state.cameraBaseSpeed + dir * stepSmall * 2.0f, 0.1f, 100.0f);
                                m_cameraBaseSpeed = state.cameraBaseSpeed;
                                break;
                            case 14: { // Ray tracing toggle (if supported)
                                auto* renderer = m_renderer.get();
                                if (renderer && renderer->IsRayTracingSupported()) {
                                    state.rayTracingEnabled = !state.rayTracingEnabled;
                                }
                                break;
                            }
                            default:
                                break;
                        }
                        UI::DebugMenu::SyncFromState(state);
                        break;
                    }

                    // Space/Enter toggle boolean sections such as ray tracing.
                    if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) {
                        if (m_settingsSection >= 2 && m_settingsSection <= 12) {
                            // Boolean rows: flip the value
                            // Let LEFT/RIGHT handler handle the actual toggle by
                            // simulating a small positive delta.
                            const SDL_Keycode fake = SDLK_RIGHT;
                            (void)fake; // no-op; we already share logic above.
                        } else if (m_settingsSection == 14) {
                            auto* renderer = m_renderer.get();
                            if (renderer && renderer->IsRayTracingSupported()) {
                                state.rayTracingEnabled = !state.rayTracingEnabled;
                                UI::DebugMenu::SyncFromState(state);
                            }
                        }
                        break;
                    }

                    // Unhandled key while menu is visible: do not fall through
                    // to the rest of the engine hotkeys.
                    break;
                }

                if (event.key.key == SDLK_ESCAPE) {
                    m_running = false;
                }
                else if (event.key.key == SDLK_F) {
                    // Frame the currently selected entity (if any) and mark it
                    // as the logical focus target for LLM/Dreamer edits.
                    FrameSelectedEntity();
                }
                else if (event.key.key == SDLK_G) {
                    // Toggle drone/free-flight camera mode. When enabled, the camera
                    // can be steered continuously without holding the right mouse
                    // button and the mouse is locked in relative mode.
                    m_droneFlightEnabled = !m_droneFlightEnabled;
                    if (m_droneFlightEnabled) {
                        m_cameraControlActive = true;
                        m_cameraVelocity = glm::vec3(0.0f);
                        m_cameraRoll = 0.0f;
                        if (m_window) {
                            SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), true);
                        }
                        spdlog::info("Drone flight enabled (G)");
                    } else {
                        m_cameraControlActive = false;
                        m_cameraVelocity = glm::vec3(0.0f);
                        m_cameraRoll = 0.0f;
                        if (m_window) {
                            SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), false);
                        }
                        spdlog::info("Drone flight disabled");
                    }
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
                else if (event.key.key == SDLK_Y) {
                    // Phase 3: Trigger Dreamer texture generation for the current focus target.
                    if (m_dreamerService && m_dreamerEnabled) {
                        std::string prompt = UI::TextPrompt::Show(
                            m_window->GetHWND(),
                            "Dreamer Texture Prompt",
                            "Describe the texture to generate:");
                        if (!prompt.empty()) {
                            std::string target = GetFocusTarget();
                            if (target.empty()) {
                                target = "SpinningCube";
                            }
                            AI::Vision::TextureRequest req;
                            req.targetName = target;
                            req.prompt = prompt;
                            req.usage = AI::Vision::TextureUsage::Albedo;
                            req.width = 512;
                            req.height = 512;
                            m_dreamerService->SubmitRequest(req);
                            spdlog::info("[Dreamer] Queued texture request for '{}' with prompt: \"{}\"",
                                         target, prompt);
                        } else {
                            spdlog::info("[Dreamer] Texture prompt cancelled");
                        }
                    } else {
                        spdlog::info("[Dreamer] Service not enabled; Y key ignored");
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
                else if (event.key.key == SDLK_Z) {
                    if (m_renderer) {
                        m_renderer->ToggleTAA();
                    }
                }
                else if (event.key.key == SDLK_M) {
                    UI::DebugMenu::Toggle();
                    if (UI::DebugMenu::IsVisible()) {
                        m_settingsSection = 0;
                    }
                }
                else if (event.key.key == SDLK_F2) {
                    // Reset all debug settings (sliders + view modes) to defaults, then show the menu
                    UI::DebugMenu::ResetToDefaults();
                    UI::DebugMenu::SetVisible(true);
                }
                else if (event.key.key == SDLK_F5) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowPCFRadius(0.5f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F7) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(-0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F8) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F9) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(-0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F10) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F11) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, -0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_F12) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, 0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (event.key.key == SDLK_V) {
                    if (m_renderer) {
                        if (!m_renderer->IsRayTracingSupported()) {
                            spdlog::info("Ray tracing not supported on this GPU; V toggle ignored");
                        } else {
                            bool enabled = !m_renderer->IsRayTracingEnabled();
                            m_renderer->SetRayTracingEnabled(enabled);
                            // Keep debug menu state in sync with renderer
                            SyncDebugMenuFromRenderer();
                            spdlog::info("Ray tracing {}", enabled ? "ENABLED" : "DISABLED");
                        }
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
                else if (event.key.key == SDLK_R) {
                    if (m_renderer) {
                        m_renderer->CycleScreenSpaceEffectsDebug();
                    }
                }
                else if (event.key.key == SDLK_C) {
                    if (m_renderer) {
                        // Cycle environment preset (studio -> sunset -> night -> ...).
                        m_renderer->CycleEnvironmentPreset();
                    }
                }
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // Track last mouse position for hover logic.
                m_lastMousePos = glm::vec2(static_cast<float>(event.button.x),
                                           static_cast<float>(event.button.y));

                if (event.button.button == SDL_BUTTON_LEFT) {
                    // If a gizmo axis is under the cursor, begin a drag; otherwise pick entity.
                    glm::vec3 rayOrigin, rayDir;
                    if (ComputeCameraRayFromMouse(m_lastMousePos.x, m_lastMousePos.y, rayOrigin, rayDir) &&
                        m_registry && m_selectedEntity != entt::null) {
                        auto& reg = m_registry->GetRegistry();
                        if (reg.valid(m_selectedEntity) &&
                            reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
                            const auto& t = reg.get<Scene::TransformComponent>(m_selectedEntity);
                            glm::vec3 center = glm::vec3(t.worldMatrix[3]);

                            glm::vec3 axisWorld[3] = {
                                glm::vec3(t.worldMatrix * glm::vec4(1,0,0,0)),
                                glm::vec3(t.worldMatrix * glm::vec4(0,1,0,0)),
                                glm::vec3(t.worldMatrix * glm::vec4(0,0,1,0))
                            };

                            glm::vec3 axes[3];
                            for (int i = 0; i < 3; ++i) {
                                float len2 = glm::length2(axisWorld[i]);
                                axes[i] = (len2 > 1e-6f) ? (axisWorld[i] / std::sqrt(len2))
                                                         : glm::vec3(0.0f);
                            }

                            float axisLength = 1.0f;
                            float threshold = 0.15f;
                            float distance = glm::length(center - rayOrigin);
                            ComputeGizmoScale(distance, axisLength, threshold);

                            GizmoAxis hitAxis = GizmoAxis::None;
                            if (HitTestGizmoAxis(rayOrigin, rayDir, center, axes, axisLength, threshold, hitAxis)) {
                                // Begin drag along this axis.
                                m_gizmoActiveAxis = hitAxis;
                                m_gizmoDragging = true;
                                m_gizmoAxisDir = (hitAxis == GizmoAxis::X) ? axes[0] :
                                                 (hitAxis == GizmoAxis::Y) ? axes[1] : axes[2];
                                m_gizmoDragCenter = center;

                                // Build drag plane facing camera but containing the axis.
                                glm::vec3 planeNormal(0.0f, 1.0f, 0.0f);
                                if (m_activeCameraEntity != entt::null &&
                                    m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) &&
                                    m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
                                    auto& camT = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
                                    glm::vec3 viewDir = glm::normalize(camT.rotation * glm::vec3(0,0,1));
                                    glm::vec3 n = glm::cross(m_gizmoAxisDir, glm::cross(viewDir, m_gizmoAxisDir));
                                    if (glm::length2(n) > 1e-4f) {
                                        planeNormal = glm::normalize(n);
                                    } else {
                                        // Fallback: choose a stable plane that still contains the axis.
                                        switch (hitAxis) {
                                            case GizmoAxis::X: {
                                                glm::vec3 ref(0.0f, 1.0f, 0.0f);
                                                glm::vec3 alt(0.0f, 0.0f, 1.0f);
                                                glm::vec3 pn = glm::cross(m_gizmoAxisDir, ref);
                                                if (glm::length2(pn) < 1e-4f) pn = glm::cross(m_gizmoAxisDir, alt);
                                                if (glm::length2(pn) > 1e-6f) planeNormal = glm::normalize(pn);
                                                break;
                                            }
                                            case GizmoAxis::Y: {
                                                glm::vec3 ref(0.0f, 0.0f, 1.0f);
                                                glm::vec3 alt(1.0f, 0.0f, 0.0f);
                                                glm::vec3 pn = glm::cross(m_gizmoAxisDir, ref);
                                                if (glm::length2(pn) < 1e-4f) pn = glm::cross(m_gizmoAxisDir, alt);
                                                if (glm::length2(pn) > 1e-6f) planeNormal = glm::normalize(pn);
                                                break;
                                            }
                                            case GizmoAxis::Z: {
                                                glm::vec3 ref(0.0f, 1.0f, 0.0f);
                                                glm::vec3 alt(1.0f, 0.0f, 0.0f);
                                                glm::vec3 pn = glm::cross(m_gizmoAxisDir, ref);
                                                if (glm::length2(pn) < 1e-4f) pn = glm::cross(m_gizmoAxisDir, alt);
                                                if (glm::length2(pn) > 1e-6f) planeNormal = glm::normalize(pn);
                                                break;
                                            }
                                            default:
                                                break;
                                        }
                                    }
                                }
                                m_gizmoDragPlaneNormal = planeNormal;
                                m_gizmoDragPlanePoint = m_gizmoDragCenter;

                                // Cache initial entity position and axis parameter.
                                auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);
                                m_gizmoDragStartEntityPos = selT.position;
                                glm::vec3 hitPoint;
                                if (RayPlaneIntersection(rayOrigin, rayDir,
                                                         m_gizmoDragPlanePoint,
                                                         m_gizmoDragPlaneNormal,
                                                         hitPoint)) {
                                    glm::vec3 axisN = glm::normalize(m_gizmoAxisDir);
                                    m_gizmoDragStartAxisParam =
                                        glm::dot(hitPoint - m_gizmoDragCenter, axisN);
                                } else {
                                    m_gizmoDragStartAxisParam = 0.0f;
                                }
                                break;
                            }
                        }
                    }

                    // No gizmo hit; perform standard picking.
                    m_selectedEntity = PickEntityAt(m_lastMousePos.x, m_lastMousePos.y);
                    if (m_selectedEntity != entt::null && m_registry &&
                        m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
                        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
                        SetFocusTarget(tag.tag);
                    }
                }
                else if (!m_droneFlightEnabled &&
                         event.button.button == SDL_BUTTON_RIGHT && m_window) {
                    m_cameraControlActive = true;
                    SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), true);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (m_gizmoDragging) {
                        m_gizmoDragging = false;
                        m_gizmoActiveAxis = GizmoAxis::None;
                    }
                }
                if (!m_droneFlightEnabled &&
                    event.button.button == SDL_BUTTON_RIGHT && m_window) {
                    m_cameraControlActive = false;
                    SDL_SetWindowRelativeMouseMode(m_window->GetSDLWindow(), false);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_lastMousePos = glm::vec2(static_cast<float>(event.motion.x),
                                           static_cast<float>(event.motion.y));
                if (m_gizmoDragging && m_registry && m_selectedEntity != entt::null) {
                    auto& reg = m_registry->GetRegistry();
                    if (!reg.valid(m_selectedEntity) ||
                        !reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
                        // Entity was destroyed while dragging; cancel safely.
                        m_gizmoDragging = false;
                        m_gizmoActiveAxis = GizmoAxis::None;
                    } else {
                        glm::vec3 rayOrigin, rayDir;
                        if (ComputeCameraRayFromMouse(m_lastMousePos.x, m_lastMousePos.y, rayOrigin, rayDir)) {
                            glm::vec3 hitPoint;
                            if (RayPlaneIntersection(rayOrigin, rayDir,
                                                     m_gizmoDragPlanePoint,
                                                     m_gizmoDragPlaneNormal,
                                                     hitPoint)) {
                                if (glm::length2(m_gizmoAxisDir) > 1e-6f) {
                                    glm::vec3 axisN = glm::normalize(m_gizmoAxisDir);
                                    float s = glm::dot(hitPoint - m_gizmoDragCenter, axisN);
                                    float delta = s - m_gizmoDragStartAxisParam;
                                    glm::vec3 offset = axisN * delta;

                                    auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);
                                    selT.position = m_gizmoDragStartEntityPos + offset;
                                }
                            }
                        }
                    }
                } else if (m_cameraControlActive) {
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
        // Mirror any renderer changes into the debug menu so sliders/numbers stay in sync
        SyncDebugMenuFromRenderer();
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

    // Phase 3: Apply Dreamer-generated textures to their targets on the main thread.
    if (m_dreamerService && m_renderer && m_registry) {
        auto results = m_dreamerService->ConsumeFinished();
        if (!results.empty()) {
            auto view = m_registry->View<Scene::TagComponent, Scene::RenderableComponent>();

            auto usageToString = [](AI::Vision::TextureUsage u) -> const char* {
                switch (u) {
                    case AI::Vision::TextureUsage::Albedo:     return "albedo";
                    case AI::Vision::TextureUsage::Normal:     return "normal";
                    case AI::Vision::TextureUsage::Roughness:  return "roughness";
                    case AI::Vision::TextureUsage::Metalness:  return "metalness";
                    case AI::Vision::TextureUsage::Environment:return "environment";
                    case AI::Vision::TextureUsage::Skybox:     return "skybox";
                    default:                                   return "unknown";
                }
            };

            for (auto& tex : results) {
                if (!tex.success) {
                    spdlog::warn("[Dreamer] Texture generation failed for '{}': {}",
                                 tex.targetName, tex.message);
                    continue;
                }

                // Environment / skybox jobs do not need an entity; treat them as global.
                if (tex.usage == AI::Vision::TextureUsage::Environment ||
                    tex.usage == AI::Vision::TextureUsage::Skybox) {
                    auto gpuTex = m_renderer->CreateTextureFromRGBA(
                        tex.pixels.data(),
                        tex.width,
                        tex.height,
                        /*useSRGB=*/true,
                        tex.targetName.empty() ? "Dreamer_Env" : "Dreamer_" + tex.targetName);
                    if (gpuTex.IsErr()) {
                        spdlog::error("[Dreamer] Failed to create GPU env texture for '{}': {}",
                                      tex.targetName, gpuTex.Error());
                        continue;
                    }

                    auto envResult = m_renderer->AddEnvironmentFromTexture(
                        gpuTex.Value(),
                        tex.targetName.empty() ? tex.prompt : tex.targetName);
                    if (envResult.IsErr()) {
                        spdlog::error("[Dreamer] Failed to register environment '{}': {}",
                                      tex.targetName, envResult.Error());
                    } else {
                        spdlog::info("[Dreamer] Applied {} texture as environment '{}'", usageToString(tex.usage),
                                     tex.targetName.empty() ? tex.prompt : tex.targetName);
                    }
                    continue;
                }

                // For surface textures, allow both exact tag matches and prefix matches
                // so that requests like "GiantPig" can hit "GiantPig.Body", etc.
                std::string targetLower = tex.targetName;
                std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                std::vector<entt::entity> exactMatches;
                std::vector<entt::entity> prefixMatches;

                for (auto entity : view) {
                    const auto& tag = view.get<Scene::TagComponent>(entity);
                    std::string tagLower = tag.tag;
                    std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                    if (!targetLower.empty() && tagLower == targetLower) {
                        exactMatches.push_back(entity);
                    } else if (!targetLower.empty() &&
                               tagLower.rfind(targetLower, 0) == 0) {
                        // Tag starts with the requested name (e.g., GiantPig.*)
                        prefixMatches.push_back(entity);
                    }
                }

                std::vector<entt::entity>* chosen = nullptr;
                if (!exactMatches.empty()) {
                    chosen = &exactMatches;
                } else if (!prefixMatches.empty()) {
                    chosen = &prefixMatches;
                }

                if (!chosen || chosen->empty()) {
                    spdlog::warn("[Dreamer] No entity found with tag or prefix '{}' for generated texture",
                                 tex.targetName);
                    continue;
                }

                auto gpuTex = m_renderer->CreateTextureFromRGBA(
                    tex.pixels.data(),
                    tex.width,
                    tex.height,
                    /*useSRGB=*/true,
                    "Dreamer_" + tex.targetName);
                if (gpuTex.IsErr()) {
                    spdlog::error("[Dreamer] Failed to create GPU texture for '{}': {}",
                                  tex.targetName, gpuTex.Error());
                    continue;
                }

                for (auto entity : *chosen) {
                    auto& renderable = view.get<Scene::RenderableComponent>(entity);

                    switch (tex.usage) {
                        case AI::Vision::TextureUsage::Albedo:
                            // Override the albedo map and reset supporting maps so the
                            // Dreamer texture is clearly visible in shaded mode. We set
                            // a sentinel albedoPath instead of clearing it so that
                            // EnsureMaterialTextures does not revert back to the
                            // placeholder texture on subsequent frames.
                            renderable.textures.albedo = gpuTex.Value();
                            renderable.textures.albedoPath = "[Dreamer]";
                            renderable.textures.normal.reset();
                            renderable.textures.normalPath.clear();
                            renderable.textures.metallic.reset();
                            renderable.textures.metallicPath.clear();
                            renderable.textures.roughness.reset();
                            renderable.textures.roughnessPath.clear();
                            // Let the Dreamer-driven albedo texture drive final color
                            // directly; keep albedoColor neutral so the texture is
                            // clearly visible.
                            renderable.albedoColor = glm::vec4(1.0f);
                            renderable.metallic = 0.0f;
                            renderable.roughness = 0.7f;
                            break;
                        case AI::Vision::TextureUsage::Normal:
                            renderable.textures.normal = gpuTex.Value();
                            renderable.textures.normalPath.clear();
                            break;
                        case AI::Vision::TextureUsage::Roughness:
                            renderable.textures.roughness = gpuTex.Value();
                            renderable.textures.roughnessPath.clear();
                            break;
                        case AI::Vision::TextureUsage::Metalness:
                            renderable.textures.metallic = gpuTex.Value();
                            renderable.textures.metallicPath.clear();
                            break;
                        default:
                            break;
                    }

                    if (!tex.materialPreset.empty()) {
                        renderable.presetName = tex.materialPreset;
                    }
                }

                spdlog::info("[Dreamer] Applied {} texture to {} entit(ies) for tag '{}'",
                             usageToString(tex.usage),
                             chosen->size(),
                             tex.targetName);
            }
        }
    }

    // Apply debug menu slider values to renderer/camera
    if (m_renderer) {
        UI::DebugMenuState dbg = UI::DebugMenu::GetState();
        m_cameraBaseSpeed = dbg.cameraBaseSpeed;
        m_cameraMaxSpeed = std::max(15.0f, m_cameraBaseSpeed * 8.0f);

        // Apply lighting rig selection to the scene lights.
        auto rig = Graphics::Renderer::LightingRig::Custom;
        switch (dbg.lightingRig) {
        case 1: rig = Graphics::Renderer::LightingRig::StudioThreePoint; break;
        case 2: rig = Graphics::Renderer::LightingRig::TopDownWarehouse; break;
        case 3: rig = Graphics::Renderer::LightingRig::HorrorSideLight; break;
        default: rig = Graphics::Renderer::LightingRig::Custom; break;
        }
        if (rig != Graphics::Renderer::LightingRig::Custom && m_registry) {
            m_renderer->ApplyLightingRig(rig, m_registry.get());
        }
    }

    // Update active camera (fly controls)
    UpdateCameraController(deltaTime);

    // Update all rotation components (spinning cube)
    auto viewRot = m_registry->View<Scene::RotationComponent, Scene::TransformComponent>();
    for (auto entity : viewRot) {
        auto& rotation = viewRot.get<Scene::RotationComponent>(entity);
        auto& transform = viewRot.get<Scene::TransformComponent>(entity);

        float angle = rotation.speed * deltaTime;
        glm::quat rotationDelta = glm::angleAxis(angle, glm::normalize(rotation.axis));
        transform.rotation = rotationDelta * transform.rotation;
    }

    // Update world matrices for all transforms so picking/gizmos and renderer
    // operate on consistent world-space data.
    m_registry->UpdateTransforms();

    // Per-frame gizmo hover detection (editor-style)
    UpdateGizmoHover();
}

void Engine::Render(float deltaTime) {
    // Build debug lines (world axes, selection, gizmos) before issuing the
    // main render; the renderer will consume these in its debug overlay pass.
    DebugDrawSceneGraph();

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

bool Engine::ComputeCameraRayFromMouse(float mouseX, float mouseY,
                                       glm::vec3& outOrigin,
                                       glm::vec3& outDirection) {
    if (!m_window || !m_registry) {
        return false;
    }

    float width = static_cast<float>(m_window->GetWidth());
    float height = static_cast<float>(m_window->GetHeight());
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }

    Scene::TransformComponent* camTransform = nullptr;
    Scene::CameraComponent* camComp = nullptr;

    // Prefer the cached active camera entity when valid; fall back to a scan
    // to recover if the active flag has changed.
    if (m_activeCameraEntity != entt::null &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        camTransform = &m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        camComp = &m_registry->GetComponent<Scene::CameraComponent>(m_activeCameraEntity);
    } else {
        auto camView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
        for (auto entity : camView) {
            auto& camera = camView.get<Scene::CameraComponent>(entity);
            auto& transform = camView.get<Scene::TransformComponent>(entity);
            if (camera.isActive) {
                camComp = &camera;
                camTransform = &transform;
                m_activeCameraEntity = entity;
                break;
            }
        }
    }

    if (!camTransform || !camComp) {
        return false;
    }

    glm::mat4 view = camComp->GetViewMatrix(*camTransform);
    glm::mat4 proj = camComp->GetProjectionMatrix(m_window->GetAspectRatio());
    glm::mat4 invViewProj = glm::inverse(proj * view);

    float ndcX = (2.0f * (mouseX / width)) - 1.0f;
    float ndcY = 1.0f - (2.0f * (mouseY / height));

    glm::vec4 nearClip(ndcX, ndcY, 0.0f, 1.0f); // LH_ZO: z=0 near
    glm::vec4 farClip(ndcX, ndcY, 1.0f, 1.0f);  // z=1 far

    glm::vec4 nearWorld = invViewProj * nearClip;
    glm::vec4 farWorld = invViewProj * farClip;
    if (std::abs(nearWorld.w) > 1e-6f) nearWorld /= nearWorld.w;
    if (std::abs(farWorld.w) > 1e-6f) farWorld /= farWorld.w;

    glm::vec3 pNear = glm::vec3(nearWorld);
    glm::vec3 pFar = glm::vec3(farWorld);

    outOrigin = camTransform->position;
    outDirection = glm::normalize(pFar - outOrigin);
    return glm::length2(outDirection) > 0.0f;
}

entt::entity Engine::PickEntityAt(float mouseX, float mouseY) {
    if (!m_registry) {
        return entt::null;
    }

    glm::vec3 rayOrigin, rayDir;
    if (!ComputeCameraRayFromMouse(mouseX, mouseY, rayOrigin, rayDir)) {
        return entt::null;
    }

    auto view = m_registry->View<Scene::TransformComponent, Scene::RenderableComponent>();
    const glm::vec3 aabbMin(-0.5f);
    const glm::vec3 aabbMax(0.5f);

    float bestDist = std::numeric_limits<float>::max();
    entt::entity best = entt::null;

    for (auto entity : view) {
        auto& transform = view.get<Scene::TransformComponent>(entity);
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        if (!renderable.visible) continue;

        glm::mat4 world = transform.worldMatrix;
        glm::mat4 invWorld = transform.inverseWorldMatrix;
        glm::vec3 localOrigin = glm::vec3(invWorld * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 localDir = glm::vec3(invWorld * glm::vec4(rayDir, 0.0f));
        if (glm::length2(localDir) < 1e-6f) continue;
        localDir = glm::normalize(localDir);

        float tLocal = 0.0f;
        if (!RayIntersectsAABB(localOrigin, localDir, aabbMin, aabbMax, tLocal)) continue;
        if (tLocal < 0.0f) continue;

        glm::vec3 hitLocal = localOrigin + localDir * tLocal;
        glm::vec3 hitWorld = glm::vec3(world * glm::vec4(hitLocal, 1.0f));
        float dist = glm::length(hitWorld - rayOrigin);
        if (dist < bestDist) {
            bestDist = dist;
            best = entity;
        }
    }

    if (best != entt::null && m_registry->HasComponent<Scene::TagComponent>(best)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(best);
        spdlog::info("Picked entity '{}' (id={})", tag.tag,
                     static_cast<uint32_t>(entt::to_integral(best)));
    } else if (best == entt::null) {
        spdlog::info("Pick miss (no entity under cursor)");
    }

    return best;
}

void Engine::FrameSelectedEntity() {
    if (!m_registry) return;
    if (m_selectedEntity == entt::null) return;

    if (!m_registry->HasComponent<Scene::TransformComponent>(m_selectedEntity)) {
        return;
    }

    // Find active camera
    Scene::TransformComponent* camTransform = nullptr;
    Scene::CameraComponent* camComp = nullptr;
    auto camView = m_registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    for (auto entity : camView) {
        auto& camera = camView.get<Scene::CameraComponent>(entity);
        auto& transform = camView.get<Scene::TransformComponent>(entity);
        if (camera.isActive) {
            camComp = &camera;
            camTransform = &transform;
            m_activeCameraEntity = entity;
            break;
        }
    }
    if (!camTransform || !camComp) return;

    const auto& selTransform = m_registry->GetComponent<Scene::TransformComponent>(m_selectedEntity);

    // Build a world-space bounding sphere from the mesh if available; fall back
    // to a simple scale-based heuristic otherwise.
    glm::vec3 focus = glm::vec3(selTransform.worldMatrix[3]);
    float radius = 0.5f;

    if (m_registry->HasComponent<Scene::RenderableComponent>(m_selectedEntity)) {
        const auto& renderable = m_registry->GetComponent<Scene::RenderableComponent>(m_selectedEntity);
        if (renderable.mesh && !renderable.mesh->positions.empty()) {
            glm::vec3 localMin(std::numeric_limits<float>::max());
            glm::vec3 localMax(-std::numeric_limits<float>::max());
            for (const auto& p : renderable.mesh->positions) {
                localMin = glm::min(localMin, p);
                localMax = glm::max(localMax, p);
            }

            glm::vec3 localCorners[8] = {
                glm::vec3(localMin.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMin.y, localMin.z),
                glm::vec3(localMax.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMax.y, localMin.z),
                glm::vec3(localMin.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMin.y, localMax.z),
                glm::vec3(localMax.x, localMax.y, localMax.z),
                glm::vec3(localMin.x, localMax.y, localMax.z)
            };

            glm::vec3 worldMin(std::numeric_limits<float>::max());
            glm::vec3 worldMax(-std::numeric_limits<float>::max());
            for (const auto& c : localCorners) {
                glm::vec3 wc = glm::vec3(selTransform.worldMatrix * glm::vec4(c, 1.0f));
                worldMin = glm::min(worldMin, wc);
                worldMax = glm::max(worldMax, wc);
            }

            focus = (worldMin + worldMax) * 0.5f;
            glm::vec3 extents = (worldMax - worldMin) * 0.5f;
            radius = glm::length(extents);
        }
    }

    if (radius < 0.5f) {
        glm::vec3 absScale = glm::abs(selTransform.scale);
        radius = std::max({absScale.x, absScale.y, absScale.z}) * 0.5f;
        if (radius < 0.5f) radius = 0.5f;
    }

    float fovRad = glm::radians(camComp->fov);
    float distance = radius / std::max(std::sin(fovRad * 0.5f), 0.1f);
    distance = std::clamp(distance, camComp->nearPlane + radius, camComp->farPlane * 0.5f);

    // Position camera behind current view direction looking at focus
    glm::vec3 forward = glm::normalize(focus - camTransform->position);
    if (glm::length2(forward) < 1e-6f) {
        forward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    // If forward is nearly parallel to worldUp, choose an alternate up vector
    // to avoid degeneracy in the cross product.
    if (std::abs(glm::dot(forward, worldUp)) > 0.98f) {
        worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    glm::vec3 right = glm::cross(forward, worldUp);
    if (glm::length2(right) < 1e-6f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = glm::normalize(right);
    }
    glm::vec3 up = glm::normalize(glm::cross(right, forward));

    camTransform->position = focus - forward * distance;
    camTransform->rotation = glm::quatLookAt(forward, up);

    // Update yaw/pitch to keep flycam in sync.
    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

    // Update logical focus target so LLM/Dreamer edits apply to this object.
    if (m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
        SetFocusTarget(tag.tag);
    }

    spdlog::info("Framed entity (distance ~{}, fov={} deg)",
                 distance, camComp->fov);
}

bool Engine::HitTestGizmoAxis(const glm::vec3& rayOrigin,
                              const glm::vec3& rayDir,
                              const glm::vec3& center,
                              const glm::vec3 axes[3],
                              float axisLength,
                              float threshold,
                              GizmoAxis& outAxis) {
    float bestT = std::numeric_limits<float>::max();
    GizmoAxis best = GizmoAxis::None;

    for (int i = 0; i < 3; ++i) {
        if (glm::length2(axes[i]) < 1e-6f) {
            continue;
        }
        float tRay = 0.0f;
        if (RayHitsAxis(rayOrigin, rayDir, center, axes[i], axisLength, threshold, tRay)) {
            if (tRay < bestT) {
                bestT = tRay;
                best = (i == 0 ? GizmoAxis::X : (i == 1 ? GizmoAxis::Y : GizmoAxis::Z));
            }
        }
    }

    outAxis = best;
    return best != GizmoAxis::None;
}

void Engine::UpdateGizmoHover() {
    m_gizmoHoveredAxis = GizmoAxis::None;

    // While dragging we keep the active axis locked and skip hover tests.
    if (m_gizmoDragging) {
        return;
    }

    if (!m_window || !m_registry || m_selectedEntity == entt::null) {
        return;
    }

    glm::vec3 rayOrigin, rayDir;
    if (!ComputeCameraRayFromMouse(m_lastMousePos.x, m_lastMousePos.y, rayOrigin, rayDir)) {
        return;
    }

    auto& reg = m_registry->GetRegistry();
    if (!reg.valid(m_selectedEntity) ||
        !reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
        return;
    }

    const auto& t = reg.get<Scene::TransformComponent>(m_selectedEntity);
    glm::vec3 center = glm::vec3(t.worldMatrix[3]);

    glm::vec3 axisWorld[3] = {
        glm::vec3(t.worldMatrix * glm::vec4(1,0,0,0)),
        glm::vec3(t.worldMatrix * glm::vec4(0,1,0,0)),
        glm::vec3(t.worldMatrix * glm::vec4(0,0,1,0))
    };

    glm::vec3 axes[3];
    for (int i = 0; i < 3; ++i) {
        float len2 = glm::length2(axisWorld[i]);
        axes[i] = (len2 > 1e-6f) ? (axisWorld[i] / std::sqrt(len2))
                                 : glm::vec3(0.0f);
    }

    float axisLength = 1.0f;
    float threshold = 0.15f;
    float distance = glm::length(center - rayOrigin);
    ComputeGizmoScale(distance, axisLength, threshold);

    GizmoAxis axis = GizmoAxis::None;
    HitTestGizmoAxis(rayOrigin, rayDir, center, axes, axisLength, threshold, axis);
    m_gizmoHoveredAxis = axis;
}

void Engine::DebugDrawSceneGraph() {
    if (!m_renderer || !m_registry) {
        return;
    }

    // Clear any lines generated in previous frame.
    m_renderer->ClearDebugLines();

    // World origin axes
    const glm::vec3 origin(0.0f);
    m_renderer->AddDebugLine(origin, origin + glm::vec3(1,0,0), glm::vec4(1,0,0,1));
    m_renderer->AddDebugLine(origin, origin + glm::vec3(0,1,0), glm::vec4(0,1,0,1));
    m_renderer->AddDebugLine(origin, origin + glm::vec3(0,0,1), glm::vec4(0,0,1,1));

    auto& reg = m_registry->GetRegistry();
    auto view = m_registry->View<Scene::TransformComponent>();

    // Selection highlight (simple wireframe box in world space).
    if (m_selectedEntity != entt::null &&
        reg.valid(m_selectedEntity) &&
        reg.all_of<Scene::TransformComponent>(m_selectedEntity)) {
        const auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);
        glm::vec3 c = glm::vec3(selT.worldMatrix[3]);

        // Use a unit cube in local space and transform it into world space so
        // the box respects hierarchy and rotation.
        glm::vec3 localCorners[8] = {
            glm::vec3(-0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f, -0.5f, -0.5f),
            glm::vec3( 0.5f,  0.5f, -0.5f),
            glm::vec3(-0.5f,  0.5f, -0.5f),
            glm::vec3(-0.5f, -0.5f,  0.5f),
            glm::vec3( 0.5f, -0.5f,  0.5f),
            glm::vec3( 0.5f,  0.5f,  0.5f),
            glm::vec3(-0.5f,  0.5f,  0.5f),
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            corners[i] = glm::vec3(selT.worldMatrix * glm::vec4(localCorners[i], 1.0f));
        }

        auto edge = [&](int a, int b) {
            m_renderer->AddDebugLine(corners[a], corners[b],
                                     glm::vec4(1.0f, 1.0f, 0.0f, 0.9f));
        };

        // Bottom
        edge(0,1); edge(1,2); edge(2,3); edge(3,0);
        // Top
        edge(4,5); edge(5,6); edge(6,7); edge(7,4);
        // Vertical
        edge(0,4); edge(1,5); edge(2,6); edge(3,7);

        // Translation gizmo centered at c, using object-space axes in world space.
        glm::vec3 axisX = glm::vec3(selT.worldMatrix * glm::vec4(1,0,0,0));
        glm::vec3 axisY = glm::vec3(selT.worldMatrix * glm::vec4(0,1,0,0));
        glm::vec3 axisZ = glm::vec3(selT.worldMatrix * glm::vec4(0,0,1,0));

        float len = 1.0f;
        float dummyThreshold = 0.0f;
        float camDistance = len;
        if (m_activeCameraEntity != entt::null &&
            reg.valid(m_activeCameraEntity) &&
            reg.all_of<Scene::TransformComponent, Scene::CameraComponent>(m_activeCameraEntity)) {
            const auto& camT = reg.get<Scene::TransformComponent>(m_activeCameraEntity);
            camDistance = glm::length(c - camT.position);
        }
        ComputeGizmoScale(camDistance, len, dummyThreshold);

        auto safeNormalize = [](const glm::vec3& v) {
            float l2 = glm::length2(v);
            return (l2 > 1e-6f) ? (v / std::sqrt(l2)) : glm::vec3(0.0f);
        };

        axisX = safeNormalize(axisX);
        axisY = safeNormalize(axisY);
        axisZ = safeNormalize(axisZ);

        auto colorForAxis = [&](GizmoAxis axis, const glm::vec3& base) {
            if (m_gizmoActiveAxis == axis || m_gizmoHoveredAxis == axis) {
                return glm::vec4(1.0f);
            }
            return glm::vec4(base, 1.0f);
        };

        glm::vec3 xTip = c + axisX * len;
        m_renderer->AddDebugLine(c, xTip, colorForAxis(GizmoAxis::X, {1,0,0}));
        m_renderer->AddDebugLine(xTip - axisY*0.05f, xTip + axisY*0.05f,
                                 colorForAxis(GizmoAxis::X, {1,0,0}));

        glm::vec3 yTip = c + axisY * len;
        m_renderer->AddDebugLine(c, yTip, colorForAxis(GizmoAxis::Y, {0,1,0}));
        m_renderer->AddDebugLine(yTip - axisZ*0.05f, yTip + axisZ*0.05f,
                                 colorForAxis(GizmoAxis::Y, {0,1,0}));

        glm::vec3 zTip = c + axisZ * len;
        m_renderer->AddDebugLine(c, zTip, colorForAxis(GizmoAxis::Z, {0,0,1}));
        m_renderer->AddDebugLine(zTip - axisX*0.05f, zTip + axisX*0.05f,
                                 colorForAxis(GizmoAxis::Z, {0,0,1}));
    }
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
    m_cameraVelocity = glm::vec3(0.0f);
    m_cameraRoll = 0.0f;

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
    transform.position = glm::vec3(0.0f, 3.0f, -8.0f);
    glm::vec3 target(0.0f, 1.0f, 0.0f);
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

    // Apply mouse look deltas (yaw/pitch) from accumulated motion.
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

    // Optional roll for drone-style banking, only in drone mode.
    if (m_droneFlightEnabled) {
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        auto keyDown = [&](SDL_Scancode scancode) {
            return scancode >= 0 && scancode < numKeys && keys[scancode];
        };

        float rollInput = 0.0f;
        // Q/E control roll in drone mode; vertical thrust is Space/Ctrl.
        if (keyDown(SDL_SCANCODE_Q)) rollInput -= 1.0f;
        if (keyDown(SDL_SCANCODE_E)) rollInput += 1.0f;

        if (std::abs(rollInput) > 0.0f) {
            m_cameraRoll += rollInput * m_cameraRollSpeed * deltaTime;
        } else {
            // Exponential decay back toward level horizon when no roll input.
            float decay = std::exp(-m_cameraRollDamping * deltaTime);
            m_cameraRoll *= decay;
        }

        // Clamp roll to a reasonable banking range.
        float maxRoll = glm::radians(75.0f);
        m_cameraRoll = glm::clamp(m_cameraRoll, -maxRoll, maxRoll);

        if (std::abs(m_cameraRoll) > 1e-4f) {
            glm::quat rollQuat = glm::angleAxis(m_cameraRoll, forward);
            right = rollQuat * right;
            up = rollQuat * up;
        }
    }

    // Keyboard movement (WASD, vertical) in camera-local axes
    if (m_cameraControlActive) {
        int numKeys = 0;
        const bool* keys = SDL_GetKeyboardState(&numKeys);
        auto keyDown = [&](SDL_Scancode scancode) {
            return scancode >= 0 && scancode < numKeys && keys[scancode];
        };

        glm::vec3 moveDir(0.0f);
        if (keyDown(SDL_SCANCODE_W)) moveDir += forward;
        if (keyDown(SDL_SCANCODE_S)) moveDir -= forward;
        if (keyDown(SDL_SCANCODE_D)) moveDir += right;
        if (keyDown(SDL_SCANCODE_A)) moveDir -= right;

        if (m_droneFlightEnabled) {
            // In drone mode, vertical thrust is space/ctrl; Q/E are roll.
            if (keyDown(SDL_SCANCODE_SPACE)) moveDir += up;
            if (keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL)) moveDir -= up;

            // Auto-forward cruise: when no explicit movement keys are pressed,
            // keep the camera gliding forward for fast, fluid traversal.
            const bool hasDirectionalInput =
                keyDown(SDL_SCANCODE_W) || keyDown(SDL_SCANCODE_S) ||
                keyDown(SDL_SCANCODE_A) || keyDown(SDL_SCANCODE_D) ||
                keyDown(SDL_SCANCODE_SPACE) ||
                keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL);
            if (!hasDirectionalInput) {
                moveDir += forward;
            }
        } else {
            // Legacy non-drone mode keeps Q/E as vertical movement.
            if (keyDown(SDL_SCANCODE_E) || keyDown(SDL_SCANCODE_SPACE)) moveDir += up;
            if (keyDown(SDL_SCANCODE_Q) ||
                keyDown(SDL_SCANCODE_LCTRL) ||
                keyDown(SDL_SCANCODE_RCTRL)) moveDir -= up;
        }

        if (!m_droneFlightEnabled) {
            // Classic immediate flycam for non-drone mode.
            if (glm::length(moveDir) > 0.0f) {
                float speed = m_cameraBaseSpeed;
                if (keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT)) {
                    speed *= m_cameraSprintMultiplier;
                }
                moveDir = glm::normalize(moveDir) * speed * deltaTime;
                transform.position += moveDir;
            }
        } else {
            // Drone/free-flight mode: velocity-based movement with acceleration and damping.
            // Apply exponential damping so the camera coasts and then gently comes to rest.
            float damping = std::max(0.0f, m_cameraDamping);
            if (damping > 0.0f) {
                float decay = std::exp(-damping * deltaTime);
                m_cameraVelocity *= decay;
            }

            glm::vec3 accel(0.0f);
            if (glm::length(moveDir) > 0.0f) {
                glm::vec3 dir = glm::normalize(moveDir);
                float thrust = m_cameraAcceleration * m_cameraBaseSpeed;
                bool sprint = keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT);
                if (sprint) {
                    thrust *= m_cameraSprintMultiplier;
                }
                accel = dir * thrust;
            }

            m_cameraVelocity += accel * deltaTime;

            // Clamp velocity magnitude to a maximum cruise speed derived from base speed.
            float maxSpeed = m_cameraMaxSpeed;
            bool sprinting = keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT);
            if (sprinting) {
                maxSpeed *= m_cameraSprintMultiplier;
            }
            float vLen = glm::length(m_cameraVelocity);
            if (vLen > maxSpeed && vLen > 1e-4f) {
                m_cameraVelocity = (m_cameraVelocity / vLen) * maxSpeed;
            }

            transform.position += m_cameraVelocity * deltaTime;
        }
    } else {
        // When camera control is inactive, keep motion state reset.
        m_cameraVelocity = glm::vec3(0.0f);
        m_cameraRoll = 0.0f;
    }

    // Update camera rotation from forward/up (including any roll).
    transform.rotation = glm::quatLookAt(glm::normalize(forward), up);
}

void Engine::InitializeScene() {
    spdlog::info("Initializing scene...");

    // --- Metallic sphere (procedural) ---
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.4f, 32);
    auto sphereUpload = m_renderer->UploadMesh(sphereMesh);
    if (sphereUpload.IsErr()) {
        spdlog::error("Failed to upload sphere mesh: {}", sphereUpload.Error());
        return;
    }

    entt::entity sphereEntity = m_registry->CreateEntity();
    auto& sphereTransform = m_registry->AddComponent<Scene::TransformComponent>(sphereEntity);
    sphereTransform.position = glm::vec3(-1.5f, 1.0f, -3.0f);

    m_registry->AddComponent<Scene::TagComponent>(sphereEntity, "MetalSphere");

    auto& sphereRenderable = m_registry->AddComponent<Scene::RenderableComponent>(sphereEntity);
    sphereRenderable.mesh = sphereMesh;
    sphereRenderable.textures.albedo    = m_renderer->GetPlaceholderTexture();
    sphereRenderable.textures.normal    = m_renderer->GetPlaceholderNormal();
    sphereRenderable.textures.metallic  = m_renderer->GetPlaceholderMetallic();
    sphereRenderable.textures.roughness = m_renderer->GetPlaceholderRoughness();
    sphereRenderable.albedoColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
    sphereRenderable.metallic    = 1.0f;
    sphereRenderable.roughness   = 0.18f;
    sphereRenderable.ao          = 1.0f;
    sphereRenderable.presetName  = "chrome";

    auto& sphereRotation = m_registry->AddComponent<Scene::RotationComponent>(sphereEntity);
    sphereRotation.axis  = glm::vec3(0.0f, 1.0f, 0.0f);
    sphereRotation.speed = 0.5f;

    // --- Metallic dragon (glTF from glTF-Sample-Models) ---
    std::shared_ptr<Scene::MeshData> dragonMesh;
    {
        auto dragonMeshResult = Utils::LoadSampleModelMesh("DragonAttenuation");
        if (dragonMeshResult.IsErr()) {
            spdlog::warn("Failed to load DragonAttenuation sample model: {}", dragonMeshResult.Error());
        } else {
            dragonMesh = dragonMeshResult.Value();
            auto dragonUpload = m_renderer->UploadMesh(dragonMesh);
            if (dragonUpload.IsErr()) {
                spdlog::warn("Failed to upload dragon mesh: {}", dragonUpload.Error());
                dragonMesh.reset();
            }
        }
    }

    if (dragonMesh) {
        entt::entity dragonEntity = m_registry->CreateEntity();
        auto& dragonTransform = m_registry->AddComponent<Scene::TransformComponent>(dragonEntity);
        dragonTransform.position = glm::vec3(1.5f, 1.0f, -3.0f);
        dragonTransform.scale    = glm::vec3(0.6f); // small dragon

        m_registry->AddComponent<Scene::TagComponent>(dragonEntity, "MetalDragon");

        auto& dragonRenderable = m_registry->AddComponent<Scene::RenderableComponent>(dragonEntity);
        dragonRenderable.mesh = dragonMesh;
        dragonRenderable.textures.albedo    = m_renderer->GetPlaceholderTexture();
        dragonRenderable.textures.normal    = m_renderer->GetPlaceholderNormal();
        dragonRenderable.textures.metallic  = m_renderer->GetPlaceholderMetallic();
        dragonRenderable.textures.roughness = m_renderer->GetPlaceholderRoughness();
        dragonRenderable.albedoColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        dragonRenderable.metallic    = 1.0f;
        dragonRenderable.roughness   = 0.22f;
        dragonRenderable.ao          = 1.0f;
        dragonRenderable.presetName  = "chrome";

        auto& dragonRotation = m_registry->AddComponent<Scene::RotationComponent>(dragonEntity);
        dragonRotation.axis  = glm::vec3(0.0f, 1.0f, 0.0f);
        dragonRotation.speed = 0.3f;
    }

    // Default focus is the metallic sphere so commands like "make it gold"
    // have a clear target.
    SetFocusTarget("MetalSphere");

    // Create a camera
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<Scene::TransformComponent>(cameraEntity);
    // Place camera higher and slightly farther back on -Z to look forward
    // (+Z is forward in our LH system), so the scene starts above the ground
    // plane instead of hugging the "singularity" at the bottom.
    cameraTransform.position = glm::vec3(0.0f, 3.0f, -8.0f);
    cameraTransform.rotation = glm::quatLookAt(
        glm::normalize(glm::vec3(0.0f) - cameraTransform.position),  // Look at origin
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 55.0f;  // Slightly wider FOV for full scene framing
    camera.isActive = true;

    // Add a simple point light above the origin for forward lighting tests
    entt::entity lightEntity = m_registry->CreateEntity();
    auto& lightTransform = m_registry->AddComponent<Scene::TransformComponent>(lightEntity);
    lightTransform.position = glm::vec3(0.0f, 6.0f, -4.0f);
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

} // namespace Cortex
