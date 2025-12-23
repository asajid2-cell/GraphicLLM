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
#include "UI/QuickSettingsWindow.h"
#include "UI/QualitySettingsWindow.h"
#include "UI/SceneEditorWindow.h"
#include "UI/PerformanceWindow.h"
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
#include <vector>
#include <deque>
#include <atomic>
#include <thread>
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

    // Shared layout constant for the hero "Dragon Over Water Studio" scene.
    constexpr float kHeroPoolZ = -3.0f;
    // Shared dimensions for the Cornell box scene (centered at origin).
    constexpr float kCornellHalfExtent = 2.0f; // half-size in X/Z
    constexpr float kCornellHeight     = 2.0f; // Y height

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
                    auto getOr = [&](const char* key, auto current) {
                        using T = std::decay_t<decltype(current)>;
                        std::string k(key);
                        if (j.contains(k)) {
                            return j.value(k, current);
                        }
                        return current;
                    };

                    state.exposure              = getOr("exposure",              state.exposure);
                    state.shadowBias            = getOr("shadowBias",           state.shadowBias);
                    state.shadowPCFRadius       = getOr("shadowPCFRadius",      state.shadowPCFRadius);
                    state.cascadeLambda         = getOr("cascadeLambda",        state.cascadeLambda);
                    state.cascade0ResolutionScale = getOr("cascade0ResolutionScale", state.cascade0ResolutionScale);
                    state.bloomIntensity        = getOr("bloomIntensity",       state.bloomIntensity);
                    state.cameraBaseSpeed       = getOr("cameraBaseSpeed",      state.cameraBaseSpeed);
                    state.fractalAmplitude      = getOr("fractalAmplitude",     state.fractalAmplitude);
                    state.fractalFrequency      = getOr("fractalFrequency",     state.fractalFrequency);
                    state.fractalOctaves        = getOr("fractalOctaves",       state.fractalOctaves);
                    state.fractalCoordMode      = getOr("fractalCoordMode",     state.fractalCoordMode);
                    state.fractalScaleX         = getOr("fractalScaleX",        state.fractalScaleX);
                    state.fractalScaleZ         = getOr("fractalScaleZ",        state.fractalScaleZ);
                    state.fractalLacunarity     = getOr("fractalLacunarity",    state.fractalLacunarity);
                    state.fractalGain           = getOr("fractalGain",          state.fractalGain);
                    state.fractalWarpStrength   = getOr("fractalWarpStrength",  state.fractalWarpStrength);
                    state.fractalNoiseType      = getOr("fractalNoiseType",     state.fractalNoiseType);
                    state.lightingRig           = getOr("lightingRig",          state.lightingRig);
                    state.rayTracingEnabled     = getOr("rayTracingEnabled",    state.rayTracingEnabled);
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

    // Enable GPU culling for GPU-driven rendering (Phase 1 feature)
    m_renderer->SetGPUCullingEnabled(true);

    // Create ECS registry
    m_registry = std::make_unique<Scene::ECS_Registry>();

    // Set up service locator
    ServiceLocator::SetDevice(m_device.get());
    ServiceLocator::SetRenderer(m_renderer.get());
    ServiceLocator::SetRegistry(m_registry.get());
    ServiceLocator::SetEngine(this);

    // Initialize scene quality. When requested via CLI/config, start from a
    // conservative preset tuned for heavy/RT scenes on 8 GB GPUs. Otherwise
    // favor a higher-quality baseline suitable for smaller curated scenes.
    if (m_renderer) {
        if (config.qualityMode == EngineConfig::QualityMode::Conservative) {
            m_renderer->ApplySafeQualityPreset();
        } else {
            // Hero/default mode: full internal resolution and higher-quality
            // AA/effects. RT remains controlled by config.enableRayTracing
            // and the runtime Quality window.
            m_renderer->SetRenderScale(1.0f);
            m_renderer->SetTAAEnabled(true);
            m_renderer->SetFXAAEnabled(false);
            m_renderer->SetSSAOEnabled(true);
            m_renderer->SetSSREnabled(true);
            m_renderer->SetFogEnabled(true);
            m_renderer->SetShadowsEnabled(true);
            m_renderer->SetIBLEnabled(true);
            m_renderer->SetBloomIntensity(0.3f);
            m_renderer->SetExposure(1.2f);
            m_renderer->SetParticlesEnabled(true);
            m_renderer->SetRTReflectionsEnabled(true);
            m_renderer->SetRTGIEnabled(true);
        }

        // Select render backend. The experimental voxel renderer bypasses the
        // classic raster + RT path when explicitly requested via EngineConfig.
        bool useVoxel =
            (config.renderBackend == EngineConfig::RenderBackend::VoxelExperimental);
        m_renderer->SetVoxelBackendEnabled(useVoxel);
        spdlog::info("Render backend: {}", useVoxel ? "VoxelExperimental" : "RasterDX12");
    }

    // Choose initial scene preset based on configuration string, if provided.
    if (!config.initialScenePreset.empty()) {
        std::string sceneLower = config.initialScenePreset;
        std::transform(sceneLower.begin(), sceneLower.end(), sceneLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (sceneLower == "dragon" || sceneLower == "dragonoverwater") {
            m_currentScenePreset = ScenePreset::DragonOverWater;
        } else if (sceneLower == "cornell" || sceneLower == "cornellbox") {
            m_currentScenePreset = ScenePreset::CornellBox;
        } else if (sceneLower == "rt" || sceneLower == "rtshowcase" || sceneLower == "rt_showcase") {
            m_currentScenePreset = ScenePreset::RTShowcase;
        }
        // Unknown strings fall through and keep the engine default.
    }

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
    // TEMPORARILY DISABLED TO ISOLATE DEVICE REMOVAL BUG
    /*
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
    */
    spdlog::info("Dreamer initialization SKIPPED (commented out for debugging)");

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
        dbg.shadowsEnabled = m_renderer->GetShadowsEnabled();
        dbg.pcssEnabled = m_renderer->IsPCSS();
        dbg.fxaaEnabled = m_renderer->IsFXAAEnabled();
        dbg.taaEnabled = m_renderer->IsTAAEnabled();
        dbg.ssrEnabled = m_renderer->GetSSREnabled();
        dbg.ssaoEnabled = m_renderer->GetSSAOEnabled();
        dbg.iblEnabled = m_renderer->GetIBLEnabled();
        dbg.fogEnabled = m_renderer->IsFogEnabled();
        dbg.rayTracingEnabled = m_renderer->IsRayTracingSupported() && m_renderer->IsRayTracingEnabled();
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
        UI::QuickSettingsWindow::Initialize(m_window->GetHWND());
        UI::QualitySettingsWindow::Initialize(m_window->GetHWND());
        UI::SceneEditorWindow::Initialize(m_window->GetHWND());
        UI::PerformanceWindow::Initialize(m_window->GetHWND());
    }

    m_running = true;
    {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> secs = now.time_since_epoch();
        m_lastFrameTimeSeconds = secs.count();
    }

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
        "  R                   - Cycle gizmo mode (translate / rotate / resize)\n"
        "  U                   - Open scene editor window\n"
        "  F5                  - Increase shadow PCF radius\n"
        "  F7 / F8             - Decrease / increase shadow bias\n"
        "  F9 / F10            - Adjust cascade split lambda\n"
        "  F11 / F12           - Adjust near cascade resolution scale\n"
        "  F2                  - Reset debug settings and show debug menu\n"
        "  B                   - Apply hero visual baseline (studio lighting, TAA, SSR/SSAO)\n"
        "  V                   - Toggle ray tracing (if supported)\n"
        "  C                   - Cycle environment preset\n"
        "  1 / 2 / 3           - Jump to hero camera bookmarks\n"
        "  F6                  - Toggle auto-demo orbit around hero scene\n"
        "  Print Screen        - Capture a screenshot to BMP\n"
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

    // Ensure the GPU is idle before we start destroying any scene/UI resources
    // that may own D3D12 objects referenced by in-flight command lists.
    if (m_renderer) {
        m_renderer->WaitForGPU();
    }

    // Persist last used debug menu state
    SaveDebugMenuStateToDisk(UI::DebugMenu::GetState());
    UI::DebugMenu::Shutdown();
    UI::QuickSettingsWindow::Shutdown();
    UI::QualitySettingsWindow::Shutdown();
    UI::SceneEditorWindow::Shutdown();

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
    ServiceLocator::SetEngine(nullptr);

    m_registry.reset();
    m_renderer.reset();
    m_window.reset();
    m_device.reset();

    spdlog::info("Cortex Engine shut down");
}

void Engine::SetSelectedEntity(entt::entity entity) {
    m_selectedEntity = entity;

    if (!m_registry || entity == entt::null) {
        return;
    }

    auto& reg = m_registry->GetRegistry();
    if (!reg.valid(entity)) {
        return;
    }

    if (reg.all_of<Scene::TagComponent>(entity)) {
        const auto& tag = reg.get<Scene::TagComponent>(entity);
        SetFocusTarget(tag.tag);
    }
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

void Engine::ToggleScenePreset() {
    ScenePreset next;
    switch (m_currentScenePreset) {
    case ScenePreset::RTShowcase:
        next = ScenePreset::CornellBox;
        break;
    case ScenePreset::CornellBox:
        next = ScenePreset::DragonOverWater;
        break;
    case ScenePreset::DragonOverWater:
    default:
        next = ScenePreset::RTShowcase;
        break;
    }
    RebuildScene(next);
}

void Engine::ApplyHeroVisualBaseline() {
    if (!m_renderer) {
        return;
    }

    // Image-based lighting and environment tuned for the hero studio scene.
    m_renderer->SetEnvironmentPreset("studio");
    m_renderer->SetIBLEnabled(true);
    // Slightly reduce diffuse IBL so direct lighting and reflections carry
    // more of the contrast, while keeping specular IBL strong for metals.
    m_renderer->SetIBLIntensity(0.85f, 1.25f);

    // Camera-friendly exposure / bloom for HDR studio environments.
    m_renderer->SetExposure(1.2f);
    m_renderer->SetBloomIntensity(0.3f);

    // Shadow and AA defaults that balance quality and stability. Enable both
    // TAA and FXAA for maximum quality.
    m_renderer->SetShadowsEnabled(true);
    m_renderer->SetShadowBias(0.0005f);
    m_renderer->SetShadowPCFRadius(1.5f);
    m_renderer->SetCascadeSplitLambda(0.5f);

    m_renderer->SetTAAEnabled(true);
    m_renderer->SetFXAAEnabled(true);

    // Screen-space ambient occlusion and reflections enabled as baseline.
    m_renderer->SetSSAOEnabled(true);
    m_renderer->SetSSREnabled(true);

    // Water tuning for the hero pool: gentle waves with a clear, reflective surface.
    // levelY matches the water surface entity's Y, amplitude and secondary amplitude keep
    // the motion visible without breaking reflections on the dragon and sphere.
    m_renderer->SetWaterParams(
        -0.02f,   // levelY
        0.03f,    // amplitude
        6.0f,     // wavelength
        0.6f,     // speed
        1.0f, 0.2f,
        0.015f);  // secondaryAmplitude

    // Enable fog for atmospheric effects.
    m_renderer->SetFogEnabled(true);

    // Reflect the new renderer state into the debug menu so sliders stay in sync.
    SyncDebugMenuFromRenderer();

    spdlog::info("Hero visual baseline applied (studio environment, TAA, SSR+SSAO)");
}

void Engine::ApplyVRAMQualityGovernor() {
    if (!m_renderer) {
        return;
    }

    // Reset flag; it will be raised again if any step takes effect.
    m_qualityAutoReduced = false;

    const float estimatedMB = m_renderer->GetEstimatedVRAMMB();
    // Soft limit tuned for 8 GB adapters. Now that we've fixed the upload buffer
    // use-after-free bugs and added texture caching, we can safely use a much
    // higher threshold. The duplicate texture loading was causing massive VRAM waste.
    constexpr float kSoftLimitMB = 7500.0f; // Raised from 6500 MB
    if (estimatedMB <= kSoftLimitMB) {
        return;
    }

    bool changed = false;

    // Peel off expensive features one by one so we keep as much visual
    // fidelity as possible while backing away from the limit.
    if (m_renderer->GetSSREnabled()) {
        m_renderer->SetSSREnabled(false);
        m_perfSSROff = true;
        changed = true;
        spdlog::warn("VRAM governor: disabling SSR (est VRAM {:.0f} MB > {:.0f} MB)", estimatedMB, kSoftLimitMB);
    } else if (m_renderer->GetSSAOEnabled()) {
        m_renderer->SetSSAOEnabled(false);
        changed = true;
        spdlog::warn("VRAM governor: disabling SSAO (est VRAM {:.0f} MB > {:.0f} MB)", estimatedMB, kSoftLimitMB);
    } else if (m_renderer->IsFogEnabled()) {
        m_renderer->SetFogEnabled(false);
        changed = true;
        spdlog::warn("VRAM governor: disabling fog (est VRAM {:.0f} MB > {:.0f} MB)", estimatedMB, kSoftLimitMB);
    } else {
        // Fall back to the aggressive safe preset which clamps shadow-map
        // size, render scale, and heavy RT/SSR/SSAO features.
        m_renderer->ApplySafeQualityPreset();
        changed = true;
        spdlog::warn("VRAM governor: applied safe low-quality preset (est VRAM {:.0f} MB > {:.0f} MB)",
                     estimatedMB, kSoftLimitMB);
    }

    if (changed) {
        m_qualityAutoReduced = true;
        // Keep debug UI in sync with any toggles we just changed.
        SyncDebugMenuFromRenderer();
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
    bool taa = renderer->IsTAAEnabled();
    bool ssr = renderer->GetSSREnabled();
    bool ssao = renderer->GetSSAOEnabled();
    bool ibl = renderer->GetIBLEnabled();
    bool fog = renderer->IsFogEnabled();
    bool rtSupported = renderer->IsRayTracingSupported();
    bool rtEnabled = renderer->IsRayTracingEnabled();
    std::string envNameUtf8 = renderer->GetCurrentEnvironmentName();

    // Approximate FPS from last frame time
    float fps = (m_frameTime > 0.0f) ? (1.0f / m_frameTime) : 0.0f;
    // Estimated VRAM usage for the current renderer configuration. This is a
    // coarse upper bound based on active render targets and a small allowance
    // for meshes/textures, suitable for on-screen diagnostics.
    float vramMB = renderer->GetEstimatedVRAMMB();

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

    // Always show top-level FPS / frame time and an approximate VRAM estimate
    swprintf_s(buffer, L"FPS: %.1f  Frame: %.2f ms", fps, m_frameTime * 1000.0f);
    drawLine(buffer);

    swprintf_s(buffer, L"VRAM (est): %.0f MB", vramMB);
    drawLine(buffer);

    if (haveCamera) {
        swprintf_s(buffer, L"Camera: (%.2f, %.2f, %.2f) FOV: %.1f",
                   camPos.x, camPos.y, camPos.z, camFov);
        drawLine(buffer);
    } else {
        drawLine(L"Camera: <none>");
    }

    // High-level render mode and quality summary.
    auto debugViewLabel = [](int mode) -> const wchar_t* {
        switch (mode) {
            case 0:  return L"Shaded";
            case 1:  return L"Normals";
            case 2:  return L"Roughness";
            case 3:  return L"Metallic";
            case 4:  return L"Albedo";
            case 5:  return L"Cascades";
            case 6:  return L"DebugScreen";
            case 13: return L"SSAO_Only";
            case 14: return L"SSAO_Overlay";
            case 15: return L"SSR_Only";
            case 16: return L"SSR_Overlay";
            case 18: return L"RT_ShadowMask";
            case 19: return L"RT_ShadowHistory";
            case 20: return L"RT_Reflections";
            case 21: return L"RT_GI";
            case 22: return L"Shaded_NoRTGI";
            case 23: return L"Shaded_NoRTRefl";
            case 24: return L"RT_ReflectionRays";
            default: return L"Other";
        }
    };

    std::wstring envName;
    if (!envNameUtf8.empty()) {
        envName.assign(envNameUtf8.begin(), envNameUtf8.end());
    } else {
        envName = L"<none>";
    }

    swprintf_s(buffer, L"View: %s (%d)  RTX: %s%s",
               debugViewLabel(debugMode),
               debugMode,
               rtEnabled ? L"ON" : L"OFF",
               !rtSupported ? L" [Not Supported]" : L"");
    drawLine(buffer);

    swprintf_s(buffer, L"Env: %s  IBL: %s  Fog: %s",
               envName.c_str(),
               ibl ? L"ON" : L"OFF",
               fog ? L"ON" : L"OFF");
    drawLine(buffer);

    const wchar_t* aaLabel = taa ? L"TAA" : (fxaa ? L"FXAA" : L"None");
    swprintf_s(buffer, L"AA: %s  SSR: %s  SSAO: %s",
               aaLabel,
               ssr ? L"ON" : L"OFF",
               ssao ? L"ON" : L"OFF");
    drawLine(buffer);

    // Scene preset summary and quick hint for switching.
    const wchar_t* sceneLabel =
        (m_currentScenePreset == ScenePreset::CornellBox)
            ? L"Cornell Box"
            : L"Dragon Over Water Studio";
    swprintf_s(buffer, L"Scene: %s  (press N to switch)", sceneLabel);
    drawLine(buffer);

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
                else if (light.type == Scene::LightType::AreaRect) typeLabel = L"Area";

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

    // When an object is selected, expose its material numerically.
    if (m_selectedEntity != entt::null &&
        m_registry->HasComponent<Scene::RenderableComponent>(m_selectedEntity)) {
        const auto& renderable = m_registry->GetComponent<Scene::RenderableComponent>(m_selectedEntity);
        std::wstring preset;
        if (!renderable.presetName.empty()) {
            preset.assign(renderable.presetName.begin(), renderable.presetName.end());
        } else {
            preset = L"<none>";
        }
        swprintf_s(buffer, L"Material: preset=%s  base=(%.2f, %.2f, %.2f)  metal=%.2f  rough=%.2f  ao=%.2f",
                   preset.c_str(),
                   renderable.albedoColor.r,
                   renderable.albedoColor.g,
                   renderable.albedoColor.b,
                   renderable.metallic,
                   renderable.roughness,
                   renderable.ao);
        drawLine(buffer);
    }

    drawLine(L"LMB: select  F: frame  G: drone  RMB: orbit  MMB: pan");

    // When the GPU settings overlay is visible (M / F2), render a textual
    // legend so it is obvious what each row controls and what the current
    // values are. The colored bars themselves are drawn in the post-process
    // shader; this HUD pass just annotates them.
    if (UI::DebugMenu::IsVisible()) {
        UI::DebugMenuState state = UI::DebugMenu::GetState();

        drawLine(L"[Settings overlay active GÇô M / F2]");
        drawLine(L"Use UP/DOWN to select row, LEFT/RIGHT to tweak, SPACE/ENTER to toggle.");

        int panelX = static_cast<int>(m_window->GetWidth()) - 320;
        int y = 48;

        auto drawPanelLine = [&](const wchar_t* text, COLORREF color) {
            SetTextColor(dc, color);
            TextOutW(dc, panelX + 12, y, text, static_cast<int>(wcslen(text)));
            y += 18;
        };

        struct Row {
            const wchar_t* label;
            float          value;
            bool           isBool;
            int            sectionIndex;
        };

        Row rows[] = {
            { L"[Render] Exposure (EV)",           state.exposure,                       false, 0 },
            { L"[Render] Bloom Intensity",         state.bloomIntensity,                 false, 1 },
            { L"[Shadows] Shadows Enabled",        state.shadowsEnabled ? 1.0f : 0.0f,   true,  2 },
            { L"[Shadows] PCSS (Soft Shadows)",    state.pcssEnabled ? 1.0f : 0.0f,      true,  3 },
            { L"[Shadows] Bias",                   state.shadowBias,                     false, 4 },
            { L"[Shadows] PCF Radius",             state.shadowPCFRadius,                false, 5 },
            { L"[Shadows] Cascade Lambda",         state.cascadeLambda,                  false, 6 },
            { L"[AA] FXAA",                        state.fxaaEnabled ? 1.0f : 0.0f,      true,  7 },
            { L"[AA] TAA",                         state.taaEnabled ? 1.0f : 0.0f,       true,  8 },
            { L"[Reflections] SSR",                state.ssrEnabled ? 1.0f : 0.0f,       true,  9 },
            { L"[AO] SSAO",                        state.ssaoEnabled ? 1.0f : 0.0f,      true,  10 },
            { L"[Environment] IBL",                state.iblEnabled ? 1.0f : 0.0f,       true,  11 },
            { L"[Environment] Fog",                state.fogEnabled ? 1.0f : 0.0f,       true,  12 },
            { L"[Camera] Base Speed",              state.cameraBaseSpeed,                false, 13 },
            { L"[Advanced] Ray Tracing",           state.rayTracingEnabled ? 1.0f : 0.0f,true,  14 }
        };

        const int rowCount = static_cast<int>(std::size(rows));
        for (int i = 0; i < rowCount; ++i) {
            const Row& r = rows[i];
            wchar_t lineText[256];

            if (r.isBool) {
                const bool on = (r.value > 0.5f);
                swprintf_s(lineText, L"%2d) %s : %s", r.sectionIndex, r.label, on ? L"ON" : L"OFF");
            } else {
                swprintf_s(lineText, L"%2d) %s : %.3f", r.sectionIndex, r.label, r.value);
            }

            COLORREF color = (m_settingsSection == r.sectionIndex)
                ? RGB(255, 255, 0)
                : RGB(200, 200, 200);

            drawPanelLine(lineText, color);
        }
    }

    ReleaseDC(hwnd, dc);
}

void Engine::CaptureScreenshot() {
    if (!m_window) {
        spdlog::warn("CaptureScreenshot: window not available");
        return;
    }

    HWND hwnd = m_window->GetHWND();
    if (!hwnd) {
        spdlog::warn("CaptureScreenshot: HWND is null");
        return;
    }

    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) {
        spdlog::warn("CaptureScreenshot: GetClientRect failed");
        return;
    }

    int width  = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        spdlog::warn("CaptureScreenshot: invalid client size");
        return;
    }

    HDC hdcWindow = GetDC(hwnd);
    if (!hdcWindow) {
        spdlog::warn("CaptureScreenshot: GetDC failed");
        return;
    }

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    if (!hdcMem) {
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: CreateCompatibleDC failed");
        return;
    }

    HBITMAP hbm = CreateCompatibleBitmap(hdcWindow, width, height);
    if (!hbm) {
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: CreateCompatibleBitmap failed");
        return;
    }

    HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    BITMAP bmp{};
    GetObject(hbm, sizeof(BITMAP), &bmp);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = -bmp.bmHeight; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(bmp.bmWidth) * static_cast<size_t>(bmp.bmHeight) * 4u);
    if (!GetDIBits(hdcWindow, hbm, 0, static_cast<UINT>(bmp.bmHeight), pixels.data(),
                   reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS)) {
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: GetDIBits failed");
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t filenameW[MAX_PATH];
    swprintf_s(filenameW, L"screenshot_%04d%02d%02d_%02d%02d%02d_%03d.bmp",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    HANDLE hFile = CreateFileW(filenameW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: failed to create output file");
        return;
    }

    BITMAPFILEHEADER bmf{};
    bmf.bfType = 0x4D42; // 'BM'
    DWORD dibSize = static_cast<DWORD>(pixels.size());
    bmf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmf.bfSize = bmf.bfOffBits + dibSize;

    DWORD written = 0;
    WriteFile(hFile, &bmf, sizeof(bmf), &written, nullptr);
    WriteFile(hFile, &bi, sizeof(bi), &written, nullptr);
    WriteFile(hFile, pixels.data(), dibSize, &written, nullptr);

    CloseHandle(hFile);

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);

    // Convert filename to UTF-8 for logging.
    int len = WideCharToMultiByte(CP_UTF8, 0, filenameW, -1, nullptr, 0, nullptr, nullptr);
    std::string filenameUtf8;
    if (len > 0) {
        filenameUtf8.resize(static_cast<size_t>(len - 1));
        WideCharToMultiByte(CP_UTF8, 0, filenameW, -1, filenameUtf8.data(), len - 1, nullptr, nullptr);
    } else {
        filenameUtf8 = "screenshot.bmp";
    }

    spdlog::info("Screenshot captured to {}", filenameUtf8);
}

void Engine::Run() {
    spdlog::info("Entering main loop...");

    while (m_running) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> secs = currentTime.time_since_epoch();
        double nowSeconds = secs.count();
        float dt = static_cast<float>(nowSeconds - m_lastFrameTimeSeconds);
        m_lastFrameTimeSeconds = nowSeconds;
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
                const SDL_Keycode key = event.key.key;
                bool overlayVisible = m_settingsOverlayVisible;
                bool settingsWindowVisible = UI::DebugMenu::IsVisible();

                // -----------------------------------------------------------------
                // Global keys that should always work, regardless of settings state
                // -----------------------------------------------------------------
                if (key == SDLK_ESCAPE) {
                    // Close overlay first, then the settings window, then the
                    // quick settings window; only exit the app if no UI is open.
                    if (overlayVisible) {
                        m_settingsOverlayVisible = false;
                        spdlog::info("Settings overlay DISABLED (ESC)");
                    } else if (settingsWindowVisible) {
                        UI::DebugMenu::SetVisible(false);
                        spdlog::info("Settings window HIDDEN (ESC)");
                    } else if (UI::QuickSettingsWindow::IsVisible()) {
                        UI::QuickSettingsWindow::SetVisible(false);
                        spdlog::info("Quick settings window HIDDEN (ESC)");
                    } else {
                        m_running = false;
                    }
                    break;
                }
                  if (key == SDLK_H) {
                      m_showGizmos = !m_showGizmos;
                      m_showOriginAxes = m_showGizmos;  // Toggle origin axes together with gizmos
                      spdlog::info("Gizmos/Axes {}", m_showGizmos ? "ENABLED" : "DISABLED");
                      break;
                  }
                  if (key == SDLK_F8) {
                      // Native quality/performance tuning window with
                      // render-scale and RTX feature controls.
                      UI::QualitySettingsWindow::Toggle();
                      spdlog::info("Quality settings window toggled (F8)");
                      break;
                  }
                  if (key == SDLK_B) {
                      ApplyHeroVisualBaseline();
                      break;
                  }
                  if (key == SDLK_K) {
                      if (m_renderer) {
                          m_renderer->ToggleGPUCullingFreeze();
                          spdlog::info("GPU culling freeze {} (K)",
                                       m_renderer->IsGPUCullingFreezeEnabled() ? "ENABLED" : "DISABLED");
                      }
                      break;
                  }
                  if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
                      if (m_renderer && m_renderer->GetDebugViewMode() == 32) {
                          const int delta = (key == SDLK_LEFTBRACKET) ? -1 : 1;
                          m_renderer->AdjustHZBDebugMip(delta);
                      }
                      break;
                  }
                if (key == SDLK_PRINTSCREEN) {
                    CaptureScreenshot();
                    break;
                }
                if (key == SDLK_F6) {
                    // Toggle scripted auto-demo camera flythrough around the hero scene.
                    m_autoDemoEnabled = !m_autoDemoEnabled;
                    m_autoDemoTime = 0.0f;
                    if (m_autoDemoEnabled) {
                        spdlog::info("Auto-demo ENABLED (F6) - camera will orbit the hero scene");
                    } else {
                        spdlog::info("Auto-demo DISABLED (F6)");
                    }
                    break;
                }
                if (key == SDLK_N) {
                    // Scene preset toggle: Cornell box <-> Dragon studio.
                    ToggleScenePreset();
                    break;
                }
                if (key == SDLK_1 || key == SDLK_2 || key == SDLK_3) {
                    // Camera bookmarks for the current scene preset.
                    if (m_registry && m_activeCameraEntity != entt::null &&
                        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
                        m_autoDemoEnabled = false;
                        auto& t = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
                        glm::vec3 center;
                        if (m_currentScenePreset == ScenePreset::CornellBox) {
                            center = glm::vec3(0.0f, 1.0f, 0.0f);
                            if (key == SDLK_1) {
                                // Default front view.
                                t.position = glm::vec3(0.0f, 1.2f, -4.0f);
                            } else if (key == SDLK_2) {
                                // High overhead shot.
                                t.position = glm::vec3(0.0f, 3.0f, -2.0f);
                            } else { // SDLK_3
                                // Angled view from the right.
                                t.position = glm::vec3(3.0f, 1.5f, -3.0f);
                            }
                        } else {
                            center = glm::vec3(0.0f, 1.0f, kHeroPoolZ);
                            if (key == SDLK_1) {
                                // Default hero shot.
                                t.position = glm::vec3(0.0f, 3.0f, -8.0f);
                            } else if (key == SDLK_2) {
                                // High overhead shot looking down at the pool.
                                t.position = glm::vec3(0.0f, 8.0f, kHeroPoolZ - 1.0f);
                            } else { // SDLK_3
                                // Angled view from the dragon side.
                                t.position = glm::vec3(6.0f, 4.0f, kHeroPoolZ + 4.0f);
                            }
                        }

                        glm::vec3 forward = glm::normalize(center - t.position);
                        glm::vec3 up(0.0f, 1.0f, 0.0f);
                        if (std::abs(glm::dot(forward, up)) > 0.99f) {
                            up = glm::vec3(0.0f, 0.0f, 1.0f);
                        }
                        t.rotation = glm::quatLookAt(forward, up);

                        forward = glm::normalize(forward);
                        m_cameraYaw = std::atan2(forward.x, forward.z);
                        m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

                        int index = (key == SDLK_1) ? 1 : (key == SDLK_2 ? 2 : 3);
                        spdlog::info("Camera bookmark {} applied", index);
                    }
                    break;
                }
                if (key == SDLK_O) {
                    // Toggle dedicated quick settings window (separate from
                    // the GPU overlay / native debug window).
                    UI::QuickSettingsWindow::Toggle();
                    spdlog::info("Quick settings window toggled (O)");
                    break;
                }
                if (key == SDLK_U) {
                    // Separate scene editor window for spawning primitives and models.
                    UI::SceneEditorWindow::Toggle();
                    spdlog::info("Scene editor window toggled (U)");
                    break;
                }
                if (key == SDLK_M) {
                    // GPU overlay (in-shader menu) toggle GÇô does not affect
                    // the native F2 settings window.
                    m_settingsOverlayVisible = !m_settingsOverlayVisible;
                    if (m_settingsOverlayVisible) {
                        m_settingsSection = 0;
                    }
                    spdlog::info("Settings overlay {}", m_settingsOverlayVisible ? "ENABLED" : "DISABLED");
                    break;
                }
                if (key == SDLK_F2) {
                    // Reset all debug settings (renderer + state) to defaults,
                    // then show the native slider/checkbox settings window.
                    UI::DebugMenu::ResetToDefaults();
                    UI::DebugMenu::SetVisible(true);
                    spdlog::info("Settings window RESET and ENABLED (F2)");
                    break;
                }
                if (key == SDLK_T && m_llmEnabled) {
                    // Architect text prompt (native dialog)
                    auto text = UI::TextPrompt::Show(m_window ? m_window->GetHWND() : nullptr);
                    if (!text.empty()) {
                        spdlog::info("Submitting to Architect: \"{}\"", text);
                        SubmitNaturalLanguageCommand(text);
                    } else {
                        spdlog::info("Text input cancelled");
                    }
                    break;
                }
                if (key == SDLK_Y) {
                    // Phase 3: Trigger Dreamer texture generation for the current focus target.
                    if (m_dreamerService && m_dreamerEnabled) {
                        std::string prompt = UI::TextPrompt::Show(
                            m_window ? m_window->GetHWND() : nullptr,
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
                    break;
                }

                // -----------------------------------------------------------------
                // When the GPU settings overlay is visible, use keys for menu navigation
                // and value adjustments. Other keys still work thanks to the
                // global handlers above.
                // -----------------------------------------------------------------
                if (overlayVisible) {
                    UI::DebugMenuState state = UI::DebugMenu::GetState();
                    const float stepSmall = 0.05f;

                    // Section navigation
                    if (key == SDLK_UP) {
                        m_settingsSection = std::max(0, m_settingsSection - 1);
                        break;
                    }
                    if (key == SDLK_DOWN) {
                        constexpr int kMaxSection = 14;
                        m_settingsSection = std::min(kMaxSection, m_settingsSection + 1);
                        break;
                    }

                    // Adjust numeric / toggle rows with LEFT/RIGHT
                    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
                        const float dir = (key == SDLK_RIGHT) ? 1.0f : -1.0f;

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

                    // Space/Enter toggle boolean rows
                    if (key == SDLK_SPACE || key == SDLK_RETURN) {
                        if (m_settingsSection >= 2 && m_settingsSection <= 12) {
                            switch (m_settingsSection) {
                                case 2:  state.shadowsEnabled = !state.shadowsEnabled; break;
                                case 3:  state.pcssEnabled = !state.pcssEnabled; break;
                                case 7:  state.fxaaEnabled = !state.fxaaEnabled; break;
                                case 8:  state.taaEnabled = !state.taaEnabled; break;
                                case 9:  state.ssrEnabled = !state.ssrEnabled; break;
                                case 10: state.ssaoEnabled = !state.ssaoEnabled; break;
                                case 11: state.iblEnabled = !state.iblEnabled; break;
                                case 12: state.fogEnabled = !state.fogEnabled; break;
                                default: break;
                            }
                            UI::DebugMenu::SyncFromState(state);
                        } else if (m_settingsSection == 14) {
                            auto* renderer = m_renderer.get();
                            if (renderer && renderer->IsRayTracingSupported()) {
                                state.rayTracingEnabled = !state.rayTracingEnabled;
                                UI::DebugMenu::SyncFromState(state);
                            }
                        }
                        break;
                    }
                    // For other keys while menu is visible, fall through to the
                    // normal hotkeys so F4, camera controls, etc. still work.
                }

                if (key == SDLK_F) {
                    // Frame the currently selected entity (if any) and mark it
                    // as the logical focus target for LLM/Dreamer edits.
                    FrameSelectedEntity();
                }
                else if (key == SDLK_G) {
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
                else if (key == SDLK_F1) {
                    // Reset camera to default position/orientation
                    InitializeCameraController();
                    spdlog::info("Camera reset to default");
                }
                else if (key == SDLK_P) {
                    // Toggle performance diagnostics window
                    UI::PerformanceWindow::Toggle();
                }
                else if (key == SDLK_X) {
                    if (m_renderer) {
                        bool enabled = !m_renderer->IsFXAAEnabled();
                        m_renderer->SetFXAAEnabled(enabled);
                        spdlog::info("FXAA {}", enabled ? "ENABLED" : "DISABLED");
                    }
                }
                else if (key == SDLK_Z) {
                    if (m_renderer) {
                        m_renderer->ToggleTAA();
                    }
                }
                else if (key == SDLK_F5) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowPCFRadius(0.5f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F7) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(-0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F8) {
                    if (m_renderer) {
                        m_renderer->AdjustShadowBias(0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F9) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(-0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F10) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeSplitLambda(0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F11) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, -0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F12) {
                    if (m_renderer) {
                        m_renderer->AdjustCascadeResolutionScale(0, 0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_V) {
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
                    // Cycle gizmo mode between translate, rotate, and resize
                    // so the same axis handles can be used for all three.
                    if (m_gizmoMode == GizmoMode::Translate) {
                        m_gizmoMode = GizmoMode::Rotate;
                    } else if (m_gizmoMode == GizmoMode::Rotate) {
                        m_gizmoMode = GizmoMode::Scale;
                    } else {
                        m_gizmoMode = GizmoMode::Translate;
                    }

                    const char* label = (m_gizmoMode == GizmoMode::Translate)
                        ? "TRANSLATE"
                        : (m_gizmoMode == GizmoMode::Rotate ? "ROTATE" : "RESIZE");
                    spdlog::info("Gizmo mode: {}", label);
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
                    bool gizmoWasHit = false;

                    // Only test gizmo interaction if gizmos are visible
                    glm::vec3 rayOrigin, rayDir;
                    if (m_showGizmos &&
                        ComputeCameraRayFromMouse(m_lastMousePos.x, m_lastMousePos.y, rayOrigin, rayDir) &&
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
                                gizmoWasHit = true;
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

                                // Cache initial entity transform and axis parameter.
                                auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);
                                m_gizmoDragStartEntityPos   = selT.position;
                                m_gizmoDragStartEntityRot   = selT.rotation;
                                m_gizmoDragStartEntityScale = selT.scale;
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
                            }
                        }
                    }

                    // No gizmo hit; perform standard picking.
                    if (!gizmoWasHit) {
                        m_selectedEntity = PickEntityAt(m_lastMousePos.x, m_lastMousePos.y);
                        if (m_selectedEntity != entt::null && m_registry &&
                            m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
                            const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
                            SetFocusTarget(tag.tag);
                        }
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

                                    auto& selT = reg.get<Scene::TransformComponent>(m_selectedEntity);

                                    if (m_gizmoMode == GizmoMode::Translate) {
                                        glm::vec3 offset = axisN * delta;
                                        selT.position = m_gizmoDragStartEntityPos + offset;
                                    } else if (m_gizmoMode == GizmoMode::Rotate) {
                                        // Rotate around the gizmo axis passing through the
                                        // object's center. Map drag distance along the axis
                                        // to an angle in radians.
                                        float angle = delta;
                                        float maxAngle = glm::radians(720.0f);
                                        angle = glm::clamp(angle, -maxAngle, maxAngle);
                                        glm::quat deltaRot = glm::angleAxis(angle, axisN);
                                        selT.rotation = glm::normalize(deltaRot * m_gizmoDragStartEntityRot);
                                    } else if (m_gizmoMode == GizmoMode::Scale) {
                                        // Resize the object by scaling uniformly based on
                                        // drag distance along the selected axis. Mapping
                                        // delta into a modest scale factor keeps interaction
                                        // predictable and prevents negative scales.
                                        float scaleDelta  = delta * 0.5f;
                                        float scaleFactor = 1.0f + scaleDelta;
                                        scaleFactor = std::clamp(scaleFactor, 0.1f, 10.0f);
                                        selT.scale = m_gizmoDragStartEntityScale * scaleFactor;
                                    }
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
                // Ensure all GPU work (including uploads) completes before resizing
                // swap chain buffers to avoid race conditions with in-flight frames.
                if (m_renderer) {
                    m_renderer->WaitForGPU();
                }
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

    // Update active camera (fly controls) and optional auto-demo orbit
    UpdateCameraController(deltaTime);
    UpdateAutoDemo(deltaTime);

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

    // Simple buoyancy integration for entities tagged with BuoyancyComponent.
    if (m_renderer && m_registry) {
        auto buoyView = m_registry->View<Scene::BuoyancyComponent, Scene::TransformComponent>();
        for (auto entity : buoyView) {
            auto& buoyancy  = buoyView.get<Scene::BuoyancyComponent>(entity);
            auto& transform = buoyView.get<Scene::TransformComponent>(entity);

            glm::vec2 xz(transform.position.x, transform.position.z);
            float waterHeight = m_renderer->SampleWaterHeightAt(xz);

            // Positive displacement means the water surface is above the object.
            float displacement = waterHeight - transform.position.y;

            // Spring-damper vertical motion: force = k * displacement - c * velocity.
            float k = 1.5f / glm::max(0.1f, buoyancy.radius);
            float c = buoyancy.damping;

            float accel = k * displacement - c * buoyancy.verticalVelocity;
            buoyancy.verticalVelocity += accel * deltaTime;

            // Integrate vertical position.
            transform.position.y += buoyancy.verticalVelocity * deltaTime;
        }
    }

    // CPU particle system integration: emit and update particles for simple
    // smoke / fire effects. Simulation runs in lockstep with the main update
    // so that render and physics stay in sync without introducing additional
    // threading complexity.
    if (m_registry) {
        auto view = m_registry->View<Scene::ParticleEmitterComponent, Scene::TransformComponent>();
        for (auto entity : view) {
            auto& emitter   = view.get<Scene::ParticleEmitterComponent>(entity);
            auto& transform = view.get<Scene::TransformComponent>(entity);

            // Emit new particles according to the configured rate.
            emitter.emissionAccumulator += deltaTime * glm::max(emitter.rate, 0.0f);
            const int maxToEmit = static_cast<int>(emitter.emissionAccumulator);
            emitter.emissionAccumulator -= static_cast<float>(maxToEmit);

            const std::size_t maxParticles = 2048;
            for (int i = 0; i < maxToEmit; ++i) {
                if (emitter.particles.size() >= maxParticles) {
                    break;
                }

                Scene::Particle p;
                p.age = 0.0f;
                p.lifetime = glm::max(emitter.lifetime, 0.1f);

                // Simple deterministic jitter based on current particle count
                // so behaviour stays stable across runs without a RNG.
                const float seed = static_cast<float>(emitter.particles.size() + 1);
                auto rand01 = [seed](float k) {
                    float v = std::sin(seed * (12.9898f + k) + 78.233f) * 43758.5453f;
                    return v - std::floor(v);
                };

                glm::vec3 velJitter(
                    (rand01(1.0f) * 2.0f - 1.0f) * emitter.velocityRandom.x,
                    (rand01(2.0f) * 2.0f - 1.0f) * emitter.velocityRandom.y,
                    (rand01(3.0f) * 2.0f - 1.0f) * emitter.velocityRandom.z);

                p.velocity = emitter.initialVelocity + velJitter;
                p.size = emitter.sizeStart;
                p.color = emitter.colorStart;

                if (emitter.localSpace) {
                    p.position = glm::vec3(0.0f);
                } else {
                    p.position = glm::vec3(transform.worldMatrix[3]);
                }

                emitter.particles.push_back(p);
            }

            // Integrate existing particles.
            const float gravity = emitter.gravity;
            for (auto& p : emitter.particles) {
                p.age += deltaTime;
                if (p.age > p.lifetime) {
                    continue;
                }
                p.velocity.y += gravity * deltaTime;
                p.position += p.velocity * deltaTime;

                float t = glm::clamp(p.age / p.lifetime, 0.0f, 1.0f);
                p.size = glm::mix(emitter.sizeStart, emitter.sizeEnd, t);
                p.color = glm::mix(emitter.colorStart, emitter.colorEnd, t);
            }

            // Remove dead particles in-place.
            emitter.particles.erase(
                std::remove_if(
                    emitter.particles.begin(),
                    emitter.particles.end(),
                    [](const Scene::Particle& p) { return p.age >= p.lifetime; }),
                emitter.particles.end());
        }
    }

    // Per-frame gizmo hover detection (editor-style)
    UpdateGizmoHover();
}

void Engine::Render(float deltaTime) {
    // Build debug lines (world axes, selection, gizmos) before issuing the
    // main render; the renderer will consume these in its debug overlay pass.
    DebugDrawSceneGraph();

    // Let the renderer know whether the GPU settings overlay should be
    // visible, along with the currently highlighted row index. This drives
    // the in-shader panel in the post-process path (M key).
    if (m_renderer) {
        m_renderer->SetDebugOverlayState(m_settingsOverlayVisible, m_settingsSection);
    }

    m_renderer->Render(m_registry.get(), deltaTime);

    // Render HUD overlay using GDI on top of the swap chain (for FPS/camera
    // text). Even when the user has hidden the normal HUD, keep RenderHUD()
    // active while either the settings overlay or the native settings window
    // is visible so the menu legend and row labels remain accessible.
    if (m_showHUD || m_settingsOverlayVisible || UI::DebugMenu::IsVisible()) {
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
    const bool mentionsWater   = contains("water");

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

    // Simple water controls: raise/lower level or make waves calmer/rougher.
    if (m_renderer && !wantsAdd && mentionsWater) {
        auto cmd = std::make_shared<LLM::ModifyRendererCommand>();
        float level = m_renderer->GetWaterLevel();
        float amp   = m_renderer->GetWaterWaveAmplitude();
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

    // Only update hover if gizmos are visible
    if (!m_showGizmos) {
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

    // World origin axes (toggled with H key along with gizmos)
    if (m_showOriginAxes) {
        const glm::vec3 origin(0.0f);
        m_renderer->AddDebugLine(origin, origin + glm::vec3(1,0,0), glm::vec4(1,0,0,1));
        m_renderer->AddDebugLine(origin, origin + glm::vec3(0,1,0), glm::vec4(0,1,0,1));
        m_renderer->AddDebugLine(origin, origin + glm::vec3(0,0,1), glm::vec4(0,0,1,1));
    }

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
        // Only draw if gizmos are enabled (toggle with H key)
        if (m_showGizmos) {
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

        float axisThickness = len * 0.02f;

        glm::vec4 xColor = colorForAxis(GizmoAxis::X, {1,0,0});
        glm::vec4 yColor = colorForAxis(GizmoAxis::Y, {0,1,0});
        glm::vec4 zColor = colorForAxis(GizmoAxis::Z, {0,0,1});

        glm::vec3 xTip = c + axisX * len;
        glm::vec3 xOffset = (axisY + axisZ) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, xTip, xColor);
        m_renderer->AddDebugLine(c + xOffset, xTip + xOffset, xColor);
        m_renderer->AddDebugLine(c - xOffset, xTip - xOffset, xColor);
        m_renderer->AddDebugLine(xTip - axisY*0.05f, xTip + axisY*0.05f, xColor);

        glm::vec3 yTip = c + axisY * len;
        glm::vec3 yOffset = (axisZ + axisX) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, yTip, yColor);
        m_renderer->AddDebugLine(c + yOffset, yTip + yOffset, yColor);
        m_renderer->AddDebugLine(c - yOffset, yTip - yOffset, yColor);
        m_renderer->AddDebugLine(yTip - axisZ*0.05f, yTip + axisZ*0.05f, yColor);

        glm::vec3 zTip = c + axisZ * len;
        glm::vec3 zOffset = (axisX + axisY) * (0.5f * axisThickness);
        m_renderer->AddDebugLine(c, zTip, zColor);
        m_renderer->AddDebugLine(c + zOffset, zTip + zOffset, zColor);
        m_renderer->AddDebugLine(c - zOffset, zTip - zOffset, zColor);
        m_renderer->AddDebugLine(zTip - axisX*0.05f, zTip + axisX*0.05f, zColor);
        }  // if (m_showGizmos)
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

    // Reset to the default position/orientation for the current scene preset
    // and derive yaw/pitch from the resulting forward vector.
    SetCameraToSceneDefault(transform);

    m_cameraControllerInitialized = true;
}

void Engine::UpdateCameraController(float deltaTime) {
    if (!m_cameraControllerInitialized || !m_registry) {
        return;
    }

    // When the auto-demo is active, camera motion is driven by UpdateAutoDemo
    // so manual input is ignored for the duration of the scripted flythrough.
    if (m_autoDemoEnabled) {
        m_pendingMouseDeltaX = 0.0f;
        m_pendingMouseDeltaY = 0.0f;
        m_cameraVelocity = glm::vec3(0.0f);
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

        m_cameraYaw += dx * m_mouseSensitivity;
        // Invert Y so that moving the mouse up pitches the camera down and
        // moving it down pitches up, matching the requested flight-style
        // controls.
        m_cameraPitch += dy * m_mouseSensitivity;

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
        // Standard FPS controls: WASD for horizontal/forward movement
        if (keyDown(SDL_SCANCODE_S)) moveDir += forward;  // W = forward
        if (keyDown(SDL_SCANCODE_W)) moveDir -= forward;  // S = backward
        if (keyDown(SDL_SCANCODE_D)) moveDir += right;    // D = right
        if (keyDown(SDL_SCANCODE_A)) moveDir -= right;    // A = left

        // Space for up, Ctrl for down (vertical movement)
        if (keyDown(SDL_SCANCODE_SPACE)) moveDir += up;   // Space = up
        if (keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL)) {
            moveDir -= up;  // Ctrl = down
        }

        if (m_droneFlightEnabled) {

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

void Engine::UpdateAutoDemo(float deltaTime) {
    if (!m_autoDemoEnabled || !m_registry) {
        return;
    }

    if (m_activeCameraEntity == entt::null ||
        !m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) ||
        !m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        return;
    }

    m_autoDemoTime += deltaTime;

    auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);

    // Simple orbital camera path around the hero pool.
    const float orbitRadius = 8.0f;
    const float orbitHeight = 3.0f;
    const glm::vec3 center(0.0f, 1.0f, kHeroPoolZ);

    float angle = m_autoDemoTime * 0.35f; // radians per second
    float yOffset = 0.5f * std::sin(m_autoDemoTime * 0.5f);

    transform.position = glm::vec3(
        orbitRadius * std::sin(angle),
        orbitHeight + yOffset,
        center.z - orbitRadius * std::cos(angle));

    glm::vec3 forward = glm::normalize(center - transform.position);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    transform.rotation = glm::quatLookAt(forward, up);

    // Keep yaw/pitch consistent with the scripted forward vector so that when
    // auto-demo is disabled, manual controls resume from a sensible state.
    forward = glm::normalize(forward);
    m_cameraYaw = std::atan2(forward.x, forward.z);
    m_cameraPitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));

    // Keep all RTX and screen-space features enabled at all times.
    if (m_renderer) {
        // Always enable ray tracing if supported
        if (m_renderer->IsRayTracingSupported()) {
            m_renderer->SetRayTracingEnabled(true);
        }
        // Always keep all screen-space effects enabled
        m_renderer->SetSSREnabled(true);
        m_renderer->SetSSAOEnabled(true);

        // Ensure a pleasant studio-like environment while the demo runs.
        m_renderer->SetEnvironmentPreset("studio");
        SyncDebugMenuFromRenderer();
    }
}

void Engine::BuildDragonStudioScene() {
    spdlog::info("Building hero scene: Dragon Over Water Studio");

    // Hero staging scene: "Dragon Over Water Studio"
    //
    // This scene is designed to exercise:
    //  - Planar water rendering (waves, reflections)
    //  - Direct lighting + cascaded sun shadows
    //  - Hybrid SSR / RT reflections and RT GI
    //  - LLM-driven edits on top of a curated layout.
    //
    // Layout (left-handed, +Z forward):
    //  - Large studio floor centered at z = -3
    //  - Square pool and water surface inset into the floor
    //  - Metal dragon hovering above the water
    //  - Chrome sphere opposite the dragon
    //  - Colored cube on the near rim
    //  - Backdrop wall behind the pool
    //  - Three-point studio lighting rig (key / fill / rim).

    const float poolZ = -3.0f;

    // Create a camera
    entt::entity cameraEntity = m_registry->CreateEntity();
    m_registry->AddComponent<Scene::TagComponent>(cameraEntity, "MainCamera");

    auto& cameraTransform = m_registry->AddComponent<Scene::TransformComponent>(cameraEntity);
    // Place camera above and behind the pool, looking toward its center.
    cameraTransform.position = glm::vec3(0.0f, 3.0f, -8.0f);
    glm::vec3 focus(0.0f, 1.0f, poolZ);
    cameraTransform.rotation = glm::quatLookAt(
        glm::normalize(focus - cameraTransform.position),
        glm::vec3(0.0f, 1.0f, 0.0f));

    auto& camera = m_registry->AddComponent<Scene::CameraComponent>(cameraEntity);
    camera.fov = 55.0f;  // Slightly wider FOV for full scene framing
    camera.isActive = true;

    // Configure sun / ambient for a clean studio look.
    if (m_renderer) {
        m_renderer->SetSunDirection(glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f)));
        m_renderer->SetSunColor(glm::vec3(1.0f));
        m_renderer->SetSunIntensity(5.0f);
    }

    // Initialize the Khronos sample model library so we can spawn the hero
    // dragon mesh by logical name ("DragonAttenuation"). Failures here should
    // not abort scene creation; we fall back to primitives if needed.
    auto sampleLibResult = Utils::InitializeSampleModelLibrary();
    if (sampleLibResult.IsErr()) {
        spdlog::warn("SampleModelLibrary initialization failed: {}", sampleLibResult.Error());
    }

    // Convenience alias for the renderer pointer.
    Graphics::Renderer* renderer = m_renderer.get();

    // Studio floor: large plane under the pool.
    auto floorMesh = Utils::MeshGenerator::CreatePlane(20.0f, 20.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(floorMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload floor mesh: {}", uploadResult.Error());
            floorMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading floor mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (floorMesh && floorMesh->gpuBuffers) {
        entt::entity floorEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(floorEntity, "StudioFloor");
        auto& floorXform = m_registry->AddComponent<Scene::TransformComponent>(floorEntity);
        floorXform.position = glm::vec3(0.0f, 0.0f, poolZ);
        floorXform.scale = glm::vec3(1.0f);

        auto& floorRenderable = m_registry->AddComponent<Scene::RenderableComponent>(floorEntity);
        floorRenderable.mesh = floorMesh;
        floorRenderable.albedoColor = glm::vec4(0.35f, 0.25f, 0.18f, 1.0f);
        floorRenderable.metallic = 0.0f;
        floorRenderable.roughness = 0.6f;
        floorRenderable.ao = 1.0f;
        floorRenderable.presetName = "wood_floor";
    } else {
        spdlog::warn("Studio floor mesh is unavailable; 'StudioFloor' entity will be skipped.");
    }

    // Pool rim + water share the same underlying plane geometry.
    auto poolMesh = Utils::MeshGenerator::CreatePlane(10.0f, 10.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(poolMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload pool mesh: {}", uploadResult.Error());
            poolMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading pool mesh; aborting Dragon studio geometry build for this run.");
            return;
        }
    }

    if (poolMesh && poolMesh->gpuBuffers) {
        // Pool rim: bright concrete ring around the water.
        entt::entity rimEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(rimEntity, "PoolRim");
        auto& rimXform = m_registry->AddComponent<Scene::TransformComponent>(rimEntity);
        // Avoid coplanar z-fighting with the studio floor plane.
        rimXform.position = glm::vec3(0.0f, 0.002f, poolZ);
        rimXform.scale = glm::vec3(1.0f);

        auto& rimRenderable = m_registry->AddComponent<Scene::RenderableComponent>(rimEntity);
        rimRenderable.mesh = poolMesh;
        rimRenderable.albedoColor = glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        rimRenderable.metallic = 0.0f;
        rimRenderable.roughness = 0.8f;
        rimRenderable.ao = 1.0f;
        rimRenderable.presetName = "concrete";

        // Water surface slightly below the rim so the edge reads clearly.
        entt::entity waterEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(waterEntity, "WaterSurface");
        auto& waterXform = m_registry->AddComponent<Scene::TransformComponent>(waterEntity);
        waterXform.position = glm::vec3(0.0f, -0.02f, poolZ);
        waterXform.scale = glm::vec3(1.0f);

        auto& waterRenderable = m_registry->AddComponent<Scene::RenderableComponent>(waterEntity);
        waterRenderable.mesh = poolMesh;
        waterRenderable.albedoColor = glm::vec4(0.02f, 0.08f, 0.12f, 0.7f);
        waterRenderable.metallic = 0.0f;
        waterRenderable.roughness = 0.08f;
        waterRenderable.ao = 1.0f;
        waterRenderable.presetName = "water";
        m_registry->AddComponent<Scene::WaterSurfaceComponent>(waterEntity, Scene::WaterSurfaceComponent{0.0f});
    } else {
        spdlog::warn("Pool mesh is unavailable; 'PoolRim' and 'WaterSurface' entities will be skipped.");
    }

    // Backdrop wall behind the pool to catch shadows and reflections.
    auto wallMesh = Utils::MeshGenerator::CreatePlane(20.0f, 10.0f);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(wallMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload wall mesh: {}", uploadResult.Error());
            wallMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading backdrop wall mesh; aborting remaining Dragon studio geometry.");
            return;
        }
    }

    if (wallMesh && wallMesh->gpuBuffers) {
        entt::entity wallEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(wallEntity, "BackdropWall");
        auto& wallXform = m_registry->AddComponent<Scene::TransformComponent>(wallEntity);
        wallXform.position = glm::vec3(0.0f, 5.0f, poolZ + 8.0f);
        // Rotate plane upright so its normal points roughly toward the camera.
        wallXform.rotation = glm::quat(glm::vec3(-glm::half_pi<float>(), 0.0f, 0.0f));
        wallXform.scale = glm::vec3(1.0f);

        auto& wallRenderable = m_registry->AddComponent<Scene::RenderableComponent>(wallEntity);
        wallRenderable.mesh = wallMesh;
        wallRenderable.albedoColor = glm::vec4(0.15f, 0.15f, 0.18f, 1.0f);
        wallRenderable.metallic = 0.0f;
        wallRenderable.roughness = 0.85f;
        wallRenderable.ao = 1.0f;
        wallRenderable.presetName = "backdrop";
    } else {
        spdlog::warn("Backdrop wall mesh is unavailable; 'BackdropWall' entity will be skipped.");
    }

    // Hero dragon mesh over the water.
    std::shared_ptr<Scene::MeshData> dragonMesh;
    auto dragonResult = Utils::LoadSampleModelMesh("DragonAttenuation");
    if (dragonResult.IsOk()) {
        dragonMesh = dragonResult.Value();
        if (renderer) {
            auto uploadResult = renderer->UploadMesh(dragonMesh);
            if (uploadResult.IsErr()) {
                spdlog::warn("Failed to upload dragon mesh: {}", uploadResult.Error());
                dragonMesh.reset();
            }
        }
    } else {
        spdlog::warn("Failed to load DragonAttenuation sample mesh: {}", dragonResult.Error());
    }

    if (dragonMesh && dragonMesh->gpuBuffers) {
        entt::entity dragonEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(dragonEntity, "MetalDragon");
        auto& dragonXform = m_registry->AddComponent<Scene::TransformComponent>(dragonEntity);
        dragonXform.position = glm::vec3(1.5f, 1.0f, poolZ);
        dragonXform.scale = glm::vec3(1.0f);

        auto& dragonRenderable = m_registry->AddComponent<Scene::RenderableComponent>(dragonEntity);
        dragonRenderable.mesh = dragonMesh;
        dragonRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        dragonRenderable.metallic = 1.0f;
        dragonRenderable.roughness = 0.22f;
        dragonRenderable.ao = 1.0f;
        dragonRenderable.presetName = "polished_metal";
    }

    // Chrome test sphere opposite the dragon.
    auto sphereMesh = Utils::MeshGenerator::CreateSphere(0.75f, 32);
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(sphereMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload sphere mesh: {}", uploadResult.Error());
            sphereMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading sphere mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    if (sphereMesh && sphereMesh->gpuBuffers) {
        entt::entity sphereEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(sphereEntity, "MetalSphere");
        auto& sphereXform = m_registry->AddComponent<Scene::TransformComponent>(sphereEntity);
        sphereXform.position = glm::vec3(-1.5f, 1.0f, poolZ);
        sphereXform.scale = glm::vec3(1.0f);

        auto& sphereRenderable = m_registry->AddComponent<Scene::RenderableComponent>(sphereEntity);
        sphereRenderable.mesh = sphereMesh;
        sphereRenderable.albedoColor = glm::vec4(0.75f, 0.75f, 0.8f, 1.0f);
        sphereRenderable.metallic = 1.0f;
        sphereRenderable.roughness = 0.05f;
        sphereRenderable.ao = 1.0f;
        sphereRenderable.presetName = "chrome";
    } else {
        spdlog::warn("Sphere mesh is unavailable; 'MetalSphere' entity will be skipped.");
    }

    // Colored cube on the near rim for GI/reflection contrast.
    auto cubeMesh = Utils::MeshGenerator::CreateCube();
    if (renderer) {
        auto uploadResult = renderer->UploadMesh(cubeMesh);
        if (uploadResult.IsErr()) {
            spdlog::warn("Failed to upload cube mesh: {}", uploadResult.Error());
            cubeMesh.reset();
        }
        if (renderer->IsDeviceRemoved()) {
            spdlog::error("DX12 device was removed while uploading cube mesh; remaining Dragon studio geometry will be skipped.");
            return;
        }
    }

    if (cubeMesh && cubeMesh->gpuBuffers) {
        entt::entity cubeEntity = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(cubeEntity, "ColorCube");
        auto& cubeXform = m_registry->AddComponent<Scene::TransformComponent>(cubeEntity);
        cubeXform.position = glm::vec3(0.0f, 0.5f, poolZ - 1.5f);
        cubeXform.scale = glm::vec3(1.5f, 1.0f, 1.5f);

        auto& cubeRenderable = m_registry->AddComponent<Scene::RenderableComponent>(cubeEntity);
        cubeRenderable.mesh = cubeMesh;
        cubeRenderable.albedoColor = glm::vec4(0.5f, 0.1f, 0.8f, 1.0f);
        cubeRenderable.metallic = 0.0f;
        cubeRenderable.roughness = 0.4f;
        cubeRenderable.ao = 1.0f;
        cubeRenderable.presetName = "painted_plastic";
    } else {
        spdlog::warn("Cube mesh is unavailable; 'ColorCube' entity will be skipped.");
    }

    // Studio lighting rig: warm key, cool rim, and soft fill.
    auto makeSpotRotation = [](const glm::vec3& dir) {
        glm::vec3 fwd = glm::normalize(dir);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(fwd, up)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::quatLookAt(fwd, up);
    };

    // Key light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "KeyLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(3.0f, 4.0f, poolZ - 1.0f);
        glm::vec3 dir(-0.6f, -0.8f, 0.7f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(1.0f, 0.95f, 0.85f);
        // Slightly reduced intensity and a softer outer cone keep the floor
        // hotspot under the dragon bright but less extreme. We rely on the
        // sun/cascaded shadows for structure and disable key-light shadows
        // entirely so small PCF/PCSS variations do not cause flicker in the
        // patch under the dragon.
        l.intensity = 10.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 22.0f;
        l.outerConeDegrees = 40.0f;
        l.castsShadows = false;
    }

    // Fill light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "FillLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(-3.0f, 2.0f, poolZ - 0.0f);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Point;
        l.color = glm::vec3(0.8f, 0.85f, 1.0f);
        l.intensity = 4.0f;
        l.range = 20.0f;
        l.castsShadows = false;
    }

    // Rim light
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "RimLight");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 3.0f, poolZ + 7.0f);
        glm::vec3 dir(0.0f, -0.5f, -1.0f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::Spot;
        l.color = glm::vec3(0.9f, 0.9f, 1.0f);
        l.intensity = 6.0f;
        l.range = 25.0f;
        l.innerConeDegrees = 25.0f;
        l.outerConeDegrees = 42.0f;
        l.castsShadows = false;
    }

    // Large softbox-style area light above the pool to produce broad,
    // studio-like highlights on metals and water. This is implemented as a
    // rectangular area light with no dedicated shadow map; it relies on the
    // existing sun shadows and volumetric fog for structure.
    {
        entt::entity e = m_registry->CreateEntity();
        m_registry->AddComponent<Scene::TagComponent>(e, "SoftboxArea");
        auto& t = m_registry->AddComponent<Scene::TransformComponent>(e);
        t.position = glm::vec3(0.0f, 6.0f, poolZ - 1.0f);
        glm::vec3 dir(0.0f, -1.0f, 0.1f);
        t.rotation = makeSpotRotation(dir);

        auto& l = m_registry->AddComponent<Scene::LightComponent>(e);
        l.type = Scene::LightType::AreaRect;
        l.color = glm::vec3(1.0f, 0.98f, 0.94f);
        l.intensity = 3.0f;
        l.range = 30.0f;
        l.areaSize = glm::vec2(6.0f, 4.0f);
        l.twoSided = false;
        l.castsShadows = false;
    }

}

void Engine::InitializeScene() {
    // If no scene has been selected yet (for example, from the config or
    // command line), default to the RT showcase gallery so the engine boots
    // directly into the most feature-rich scene. Other scenes remain
    // available via the scene toggle or LLM commands.
    switch (m_currentScenePreset) {
    case ScenePreset::CornellBox:
    case ScenePreset::DragonOverWater:
    case ScenePreset::RTShowcase:
        break;
    default:
        m_currentScenePreset = ScenePreset::RTShowcase;
        break;
    }

    RebuildScene(m_currentScenePreset);
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

void Engine::EnqueueSceneCommand(std::shared_ptr<LLM::SceneCommand> command) {
    if (!m_commandQueue || !command) {
        return;
    }
    m_commandQueue->Push(std::move(command));
}

} // namespace Cortex

