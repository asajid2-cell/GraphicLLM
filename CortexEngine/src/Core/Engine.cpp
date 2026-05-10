#include "Engine.h"
#include "EngineEditorMode.h"
#include "Core/VisualValidation.h"
#include "Editor/EditorWorld.h"
#include "ServiceLocator.h"
#include "Debug/GPUProfiler.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/RendererLightingRigControl.h"
#include "Graphics/RendererTuningState.h"
#include "Graphics/Renderer.h"
#include "Graphics/FrameContractJson.h"
#include "Utils/MeshGenerator.h"
#include "Utils/GLTFLoader.h"
#include "Utils/FileUtils.h"
#include "LLM/SceneCommands.h"
#include "LLM/RegressionTests.h"
#include "UI/TextPrompt.h"
#include "UI/DebugMenu.h"
#include "UI/QuickSettingsWindow.h"
#include "UI/QualitySettingsWindow.h"
#include "UI/GraphicsSettingsWindow.h"
#include "UI/SceneEditorWindow.h"
#include "UI/PerformanceWindow.h"
#include <windows.h>
#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include "Game/InteractionSystem.h"
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
#include <cstdint>
#include <limits>
#include <vector>
#include <deque>
#include <atomic>
#include <thread>
#include <cstdlib>
#include <cctype>
#include <nlohmann/json.hpp>

namespace Cortex {

Engine::Engine() = default;

Engine::~Engine() {
    Shutdown();
}

void Engine::SyncDebugMenuFromRenderer() {
    if (!m_renderer) {
        return;
    }

    const auto quality = m_renderer->GetQualityState();
    const auto features = m_renderer->GetFeatureState();
    const auto rt = m_renderer->GetRayTracingState();

    UI::DebugMenuState dbg{};
    dbg.exposure = quality.exposure;
    dbg.shadowBias = quality.shadowBias;
    dbg.shadowPCFRadius = quality.shadowPCFRadius;
    dbg.cascadeLambda = quality.cascadeSplitLambda;
    dbg.cascade0ResolutionScale = quality.cascade0ResolutionScale;
    dbg.bloomIntensity   = quality.bloomIntensity;
    dbg.cameraBaseSpeed  = m_cameraBaseSpeed;
    dbg.lightingRig      = 0;

    // Mirror renderer toggles into the debug menu state so the settings panel
    // and keyboard shortcuts stay in sync.
    dbg.shadowsEnabled   = quality.shadowsEnabled;
    dbg.pcssEnabled      = features.pcssEnabled;
    dbg.fxaaEnabled      = features.fxaaEnabled;
    dbg.taaEnabled       = features.taaEnabled;
    dbg.ssaoEnabled      = features.ssaoEnabled;
    dbg.iblEnabled       = features.iblEnabled;
    dbg.ssrEnabled       = features.ssrEnabled;
    dbg.fogEnabled       = features.fogEnabled;
    dbg.rayTracingEnabled = rt.supported && rt.enabled;

    UI::DebugMenu::SyncFromState(dbg);
}

namespace {
    using nlohmann::json;

    // Shared layout constant for the hero "Dragon Over Water Studio" scene.
    constexpr float kHeroPoolZ = -3.0f;
    // Shared dimensions for the Cornell box scene (centered at origin).
    constexpr float kCornellHalfExtent = 2.0f; // half-size in X/Z
    constexpr float kCornellHeight     = 2.0f; // Y height

    const char* HudModeName(EngineHudMode mode) {
        switch (mode) {
        case EngineHudMode::Off: return "off";
        case EngineHudMode::Minimal: return "minimal";
        case EngineHudMode::Performance: return "performance";
        case EngineHudMode::RendererHealth: return "renderer_health";
        case EngineHudMode::FullDebug: return "full_debug";
        }
        return "renderer_health";
    }

    bool IsExperimentalTerrainEnabled() {
        const char* value = std::getenv("CORTEX_ENABLE_EXPERIMENTAL_TERRAIN");
        if (!value) {
            return false;
        }

        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return !normalized.empty() &&
               normalized != "0" &&
               normalized != "false" &&
               normalized != "off" &&
               normalized != "no";
    }

    bool InitialPresetUsesRTShowcase(const std::string& preset) {
        if (preset.empty()) {
            return true;
        }

        std::string normalized = preset;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized == "rt" || normalized == "rtshowcase" ||
               normalized == "rt_showcase" || normalized == "material_lab" ||
               normalized == "materiallab" || normalized == "effects_showcase" ||
               normalized == "effectsshowcase" || normalized == "effects" ||
               normalized == "glass_water_courtyard" ||
               normalized == "glasswatercourtyard" || normalized == "courtyard" ||
               normalized == "god_rays" ||
               normalized == "godrays" || normalized == "temporal" ||
               normalized == "temporalvalidation" ||
               normalized == "temporal_validation";
    }

    Graphics::RendererDebugControlState ToRendererDebugControlState(const UI::DebugMenuState& state) {
        Graphics::RendererDebugControlState controls{};
        controls.exposure = state.exposure;
        controls.shadowBias = state.shadowBias;
        controls.shadowPCFRadius = state.shadowPCFRadius;
        controls.cascadeLambda = state.cascadeLambda;
        controls.cascade0ResolutionScale = state.cascade0ResolutionScale;
        controls.bloomIntensity = state.bloomIntensity;
        controls.fractalAmplitude = state.fractalAmplitude;
        controls.fractalFrequency = state.fractalFrequency;
        controls.fractalOctaves = state.fractalOctaves;
        controls.fractalCoordMode = state.fractalCoordMode;
        controls.fractalScaleX = state.fractalScaleX;
        controls.fractalScaleZ = state.fractalScaleZ;
        controls.fractalLacunarity = state.fractalLacunarity;
        controls.fractalGain = state.fractalGain;
        controls.fractalWarpStrength = state.fractalWarpStrength;
        controls.fractalNoiseType = state.fractalNoiseType;
        controls.shadowsEnabled = state.shadowsEnabled;
        controls.pcssEnabled = state.pcssEnabled;
        controls.fxaaEnabled = state.fxaaEnabled;
        controls.taaEnabled = state.taaEnabled;
        controls.ssrEnabled = state.ssrEnabled;
        controls.ssaoEnabled = state.ssaoEnabled;
        controls.iblEnabled = state.iblEnabled;
        controls.fogEnabled = state.fogEnabled;
        controls.rayTracingEnabled = state.rayTracingEnabled;
        return controls;
    }

    std::filesystem::path GetDebugMenuStatePath() {
        namespace fs = std::filesystem;
        // Store next to the executable / working directory
        return fs::current_path() / "debug_menu_state.json";
    }

    std::filesystem::path GetExecutableLogDirectory() {
        namespace fs = std::filesystem;
        if (const char* overrideDir = std::getenv("CORTEX_LOG_DIR")) {
            if (*overrideDir) {
                fs::path logDir = fs::path(overrideDir);
                std::error_code ec;
                fs::create_directories(logDir, ec);
                if (!ec) {
                    return logDir;
                }
            }
        }

        wchar_t exePathW[MAX_PATH]{};
        const DWORD exeLen = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);

        fs::path logDir;
        if (exeLen > 0 && exeLen < MAX_PATH) {
            logDir = fs::path(exePathW).parent_path() / "logs";
        } else {
            logDir = fs::current_path() / "logs";
        }

        std::error_code ec;
        fs::create_directories(logDir, ec);
        if (ec) {
            logDir = fs::current_path() / "logs";
            ec.clear();
            fs::create_directories(logDir, ec);
        }
        return logDir;
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
    m_maxFrames = config.maxFrames;
    m_exitAfterVisualValidationCapture = config.exitAfterVisualValidationCapture;
    m_hudMode = config.initialHudMode;
    if (m_maxFrames > 0 || m_exitAfterVisualValidationCapture) {
        spdlog::info("Smoke automation: maxFrames={} exitAfterVisualValidationCapture={}",
                     static_cast<unsigned long long>(m_maxFrames),
                     m_exitAfterVisualValidationCapture);
    }

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
    const bool startupRTShowcase = InitialPresetUsesRTShowcase(config.initialScenePreset);
    if (startupRTShowcase) {
        const float startupScale =
            (config.qualityMode == EngineConfig::QualityMode::Conservative) ? 0.67f : 0.85f;
        Graphics::ApplyRenderScaleControl(*m_renderer, startupScale);
        spdlog::info("Renderer startup render scale preset for RT showcase: {:.2f}", startupScale);
    }
    auto rendererResult = m_renderer->Initialize(m_device.get(), m_window.get());
    if (rendererResult.IsErr()) {
        return Result<void>::Err("Failed to initialize renderer: " + rendererResult.Error());
    }
    const auto tAfterRenderer = clock::now();
    spdlog::info("  Renderer initialized in {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(tAfterRenderer - tAfterWindow).count());

    // Enable GPU culling for GPU-driven rendering (Phase 1 feature)
    Graphics::ApplyGPUCullingEnabledControl(*m_renderer, true);

    // Create ECS registry
    m_registry = std::make_unique<Scene::ECS_Registry>();

    // Set up service locator
    ServiceLocator::SetDevice(m_device.get());
    ServiceLocator::SetRenderer(m_renderer.get());
    ServiceLocator::SetRegistry(m_registry.get());
    ServiceLocator::SetEngine(this);

    // Initialize scene quality. Conservative mode now reduces resolution and
    // residency pressure without removing renderer features from the frame.
    if (m_renderer) {
        if (config.qualityMode == EngineConfig::QualityMode::Conservative) {
            Graphics::ApplySafeQualityPresetControl(*m_renderer);
        } else {
            // Hero/default mode: full internal resolution and higher-quality
            // AA/effects. RT remains controlled by config.enableRayTracing
            // and the runtime Quality window.
            Graphics::ApplyHighQualityStartupControls(*m_renderer, startupRTShowcase);
        }

        // Select render backend. The experimental voxel renderer bypasses the
        // classic raster + RT path when explicitly requested via EngineConfig.
        bool useVoxel =
            (config.renderBackend == EngineConfig::RenderBackend::VoxelExperimental);
        Graphics::ApplyVoxelBackendControl(*m_renderer, useVoxel);
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
        } else if (sceneLower == "material_lab" || sceneLower == "materiallab" || sceneLower == "materials") {
            m_currentScenePreset = ScenePreset::MaterialLab;
        } else if (sceneLower == "glass_water_courtyard" ||
                   sceneLower == "glasswatercourtyard" ||
                   sceneLower == "courtyard") {
            m_currentScenePreset = ScenePreset::GlassWaterCourtyard;
        } else if (sceneLower == "effects_showcase" || sceneLower == "effectsshowcase" || sceneLower == "effects") {
            m_currentScenePreset = ScenePreset::EffectsShowcase;
        } else if (sceneLower == "temporal" ||
                   sceneLower == "temporalvalidation" ||
                   sceneLower == "temporal_validation") {
            m_currentScenePreset = ScenePreset::TemporalValidation;
        } else if (sceneLower == "god_rays" || sceneLower == "godrays") {
            m_currentScenePreset = ScenePreset::GodRays;
        } else if (sceneLower == "engine_editor" || sceneLower == "engineeditor") {
            // Experimental terrain/editor world is quarantined while the core
            // renderer architecture is cleaned up. Keep the code available for
            // explicit investigation, but remove it from normal launch flow.
            if (IsExperimentalTerrainEnabled()) {
                m_engineEditorMode = true;
                m_currentScenePreset = ScenePreset::ProceduralTerrain;
                spdlog::info("Experimental Engine Editor terrain enabled via CORTEX_ENABLE_EXPERIMENTAL_TERRAIN");
            } else {
                m_currentScenePreset = ScenePreset::RTShowcase;
                spdlog::warn("Ignoring experimental terrain scene '{}'. Set CORTEX_ENABLE_EXPERIMENTAL_TERRAIN=1 to opt in.",
                             config.initialScenePreset);
            }
        }
        // Unknown strings fall through and keep the engine default.
    }

    // Engine Editor mode owns terrain streaming through EditorWorld. Initialize
    // it before scene construction so the minimal terrain scene can use the
    // same terrain parameters for physics and camera placement.
    if (m_engineEditorMode) {
        m_editorModeController = std::make_unique<EngineEditorMode>();
        auto initResult = m_editorModeController->Initialize(this, m_renderer.get(), m_registry.get());
        if (initResult.IsErr()) {
            spdlog::error("Failed to initialize EngineEditorMode: {}", initResult.Error());
            m_editorModeController.reset();
        }
    }

    InitializeScene();
    InitializeCameraController();
    if (!config.initialCameraBookmark.empty() &&
        !ApplyShowcaseCameraBookmark(config.initialCameraBookmark)) {
        spdlog::warn("Startup camera bookmark '{}' was not found for the active scene",
                     config.initialCameraBookmark);
    }

    // If Engine Editor mode, enter Play mode for terrain navigation.
    if (m_engineEditorMode) {
        EnterPlayMode();
        spdlog::info("Entered Play Mode for Engine Editor - WASD to move, Space to jump");
    }

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
        auto rt = m_renderer->GetRayTracingState();
        if (config.enableRayTracing && rt.supported) {
            Graphics::ApplyFeatureToggleControl(*m_renderer, Graphics::RendererFeatureToggle::RayTracing, true);
        } else {
            Graphics::ApplyFeatureToggleControl(*m_renderer, Graphics::RendererFeatureToggle::RayTracing, false);
        }
        rt = m_renderer->GetRayTracingState();
        spdlog::info("Ray tracing config: requested={}, supported={}, enabled={}",
                     config.enableRayTracing ? "ON" : "OFF",
                     rt.supported ? "YES" : "NO",
                     rt.enabled ? "ON" : "OFF");
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

    // Phase 3: Initialize The Dreamer (async texture generator). This is an
    // optional service; when no TensorRT engines are configured it falls back
    // to the CPU procedural generator.
    if (config.enableDreamer) {
        m_dreamerService = std::make_unique<AI::Vision::DreamerService>();
        auto dreamerResult = m_dreamerService->Initialize(config.dreamerConfig);
        if (dreamerResult.IsErr()) {
            spdlog::warn("Dreamer initialization failed: {}", dreamerResult.Error());
        } else {
            m_dreamerEnabled = true;
            spdlog::info("The Dreamer is online! (async texture generation ready)");
        }
    } else {
        spdlog::info("Dreamer disabled by configuration");
    }

    // Initialize debug menu with current / persisted renderer & camera parameters
    if (m_renderer && m_window) {
        const auto quality = m_renderer->GetQualityState();
        const auto features = m_renderer->GetFeatureState();
        const auto rt = m_renderer->GetRayTracingState();

        UI::DebugMenuState dbg{};
        dbg.exposure = quality.exposure;
        dbg.shadowBias = quality.shadowBias;
        dbg.shadowPCFRadius = quality.shadowPCFRadius;
        dbg.cascadeLambda = quality.cascadeSplitLambda;
        dbg.cascade0ResolutionScale = quality.cascade0ResolutionScale;
        dbg.bloomIntensity = quality.bloomIntensity;
        dbg.cameraBaseSpeed = m_cameraBaseSpeed;
        dbg.shadowsEnabled = quality.shadowsEnabled;
        dbg.pcssEnabled = features.pcssEnabled;
        dbg.fxaaEnabled = features.fxaaEnabled;
        dbg.taaEnabled = features.taaEnabled;
        dbg.ssrEnabled = features.ssrEnabled;
        dbg.ssaoEnabled = features.ssaoEnabled;
        dbg.iblEnabled = features.iblEnabled;
        dbg.fogEnabled = features.fogEnabled;
        dbg.rayTracingEnabled = rt.supported && rt.enabled;
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
        if ((m_currentScenePreset == ScenePreset::RTShowcase ||
             m_currentScenePreset == ScenePreset::TemporalValidation) &&
            rt.supported) {
            // RTShowcase is the canonical feature-measurement scene. Do not let
            // stale persisted UI values silently remove or retune the RT workload.
            dbg.rayTracingEnabled = true;
            dbg.exposure = quality.exposure;
            dbg.bloomIntensity = quality.bloomIntensity;
        }

        // Apply persisted values back into renderer / camera so the scene matches the UI.
        Graphics::ApplyDebugControlState(*m_renderer, ToRendererDebugControlState(dbg));
        m_cameraBaseSpeed = dbg.cameraBaseSpeed;
        m_cameraMaxSpeed = std::max(15.0f, m_cameraBaseSpeed * 8.0f);

        if (const char* debugViewEnv = std::getenv("CORTEX_DEBUG_VIEW")) {
            char* end = nullptr;
            const long requested = std::strtol(debugViewEnv, &end, 10);
            if (end != debugViewEnv) {
                Graphics::ApplyDebugViewModeControl(*m_renderer, static_cast<int>(requested));
            }
        }

        UI::DebugMenu::Initialize(m_window->GetHWND(), dbg);
        UI::QuickSettingsWindow::Initialize(m_window->GetHWND());
        UI::QualitySettingsWindow::Initialize(m_window->GetHWND());
        UI::GraphicsSettingsWindow::Initialize(m_window->GetHWND());
        UI::SceneEditorWindow::Initialize(m_window->GetHWND());
        UI::PerformanceWindow::Initialize(m_window->GetHWND());
    }

    if (m_renderer) {
        const bool forceUserGraphicsSettings = [] {
            const char* value = std::getenv("CORTEX_LOAD_USER_GRAPHICS_SETTINGS");
            return value && value[0] != '\0' && value[0] != '0';
        }();
        const bool disableUserGraphicsSettings = [] {
            const char* value = std::getenv("CORTEX_DISABLE_USER_GRAPHICS_SETTINGS");
            return value && value[0] != '\0' && value[0] != '0';
        }();
        const bool interactiveLaunch = m_maxFrames == 0 && !m_exitAfterVisualValidationCapture;
        if (!disableUserGraphicsSettings && (interactiveLaunch || forceUserGraphicsSettings)) {
            std::filesystem::path settingsPath = Graphics::GetDefaultRendererTuningStatePath();
            if (const char* overridePath = std::getenv("CORTEX_GRAPHICS_SETTINGS_PATH")) {
                if (overridePath[0] != '\0') {
                    settingsPath = overridePath;
                }
            }

            std::string loadError;
            auto loaded = Graphics::LoadRendererTuningStateFile(settingsPath, &loadError);
            if (loaded) {
                Graphics::ApplyRendererTuningState(*m_renderer, *loaded);
                spdlog::info("Loaded user graphics settings from '{}'", settingsPath.string());
            } else if (!loadError.empty()) {
                spdlog::warn("Ignoring user graphics settings '{}': {}",
                             settingsPath.string(),
                             loadError);
            }
        }
    }

    if (!config.initialGraphicsPreset.empty() && m_renderer) {
        std::string resolvedPreset;
        std::string loadError;
        const auto presetPath = Graphics::GetDefaultRendererGraphicsPresetCollectionPath();
        auto preset = Graphics::LoadRendererGraphicsPresetFile(
            presetPath,
            config.initialGraphicsPreset,
            &resolvedPreset,
            &loadError);
        if (preset) {
            Graphics::ApplyRendererTuningState(*m_renderer, *preset);
            spdlog::info("Startup graphics preset '{}' applied from '{}'",
                         resolvedPreset.empty() ? config.initialGraphicsPreset : resolvedPreset,
                         presetPath.string());
        } else {
            spdlog::warn("Ignoring startup graphics preset '{}': {} ({})",
                         config.initialGraphicsPreset,
                         loadError.empty() ? "unknown error" : loadError,
                         presetPath.string());
        }
    }

    if (!config.initialEnvironmentPreset.empty() && m_renderer) {
        Graphics::ApplyEnvironmentPresetControl(*m_renderer, config.initialEnvironmentPreset);
        Graphics::ApplyFeatureToggleControl(*m_renderer, Graphics::RendererFeatureToggle::IBL, true);
        spdlog::info("Startup environment preset applied: '{}'", config.initialEnvironmentPreset);
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
        WriteFrameDiagnosticsReport(true);
    }

    // Persist last used debug menu state
    SaveDebugMenuStateToDisk(UI::DebugMenu::GetState());
    UI::DebugMenu::Shutdown();
    UI::QuickSettingsWindow::Shutdown();
    UI::QualitySettingsWindow::Shutdown();
    UI::GraphicsSettingsWindow::Shutdown();
    UI::SceneEditorWindow::Shutdown();
    UI::PerformanceWindow::Shutdown();

    // Shutdown Engine Editor Mode controller
    if (m_editorModeController) {
        m_editorModeController->Shutdown();
        m_editorModeController.reset();
    }

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
    // N key cycles through curated IBL scenes only
    // ProceduralTerrain is accessed via J key as a separate world mode
    ScenePreset next;
    switch (m_currentScenePreset) {
    case ScenePreset::RTShowcase:
        next = ScenePreset::CornellBox;
        break;
    case ScenePreset::CornellBox:
        next = ScenePreset::DragonOverWater;
        break;
    case ScenePreset::DragonOverWater:
        next = ScenePreset::MaterialLab;
        break;
    case ScenePreset::MaterialLab:
        next = ScenePreset::GlassWaterCourtyard;
        break;
    case ScenePreset::GlassWaterCourtyard:
        next = ScenePreset::EffectsShowcase;
        break;
    case ScenePreset::EffectsShowcase:
        next = ScenePreset::TemporalValidation;
        break;
    case ScenePreset::TemporalValidation:
    default:
        next = ScenePreset::RTShowcase;
        break;
    }
    RebuildScene(next);
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
        m_totalFrameCount++;
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

        if (m_maxFrames > 0 && m_totalFrameCount >= m_maxFrames) {
            spdlog::info("Smoke automation reached max frame count {}; exiting main loop",
                         static_cast<unsigned long long>(m_maxFrames));
            m_running = false;
        }
    }

    spdlog::info("Exiting main loop");
}

void Engine::Update(float deltaTime) {
    // Update Engine Editor Mode controller if active (handles its own sun state)
    if (m_editorModeController && m_editorModeController->IsInitialized()) {
        m_editorModeController->Update(deltaTime);
        // Editor controller manages sun state, skip world state update
    } else {
        // Update world simulation (time-of-day, sun position) for non-editor mode
        m_worldState.Update(deltaTime);

        // Push sun state to renderer when in terrain mode (procedural sky)
        if (m_renderer && m_currentScenePreset == ScenePreset::ProceduralTerrain) {
            Graphics::ApplyEditorTimeOfDayControls(*m_renderer,
                                                   m_worldState.sunDirection,
                                                   m_worldState.sunColor,
                                                   m_worldState.sunIntensity);
        }
    }

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

                bool useSRGB = true;
                switch (tex.usage) {
                    case AI::Vision::TextureUsage::Albedo:
                        useSRGB = true;
                        break;
                    case AI::Vision::TextureUsage::Normal:
                    case AI::Vision::TextureUsage::Roughness:
                    case AI::Vision::TextureUsage::Metalness:
                        useSRGB = false;
                        break;
                    default:
                        useSRGB = true;
                        break;
                }

                auto gpuTex = m_renderer->CreateTextureFromRGBA(
                    tex.pixels.data(),
                    tex.width,
                    tex.height,
                    useSRGB,
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
            Graphics::ApplyLightingRigControl(*m_renderer, rig, m_registry.get());
        }
    }

    // Update active camera (fly controls) and optional auto-demo orbit
    UpdateCameraController(deltaTime);
    UpdateAutoDemo(deltaTime);

    // Update play mode (terrain collision, interaction) if active
    UpdatePlayMode(deltaTime);

    // Dynamic chunk loading for the legacy terrain scene. EngineEditorMode uses
    // EditorWorld for async terrain streaming; running both loaders against the
    // same ECS creates duplicate TerrainChunkComponent entities and corrupts
    // each streamer's bookkeeping.
    const bool editorWorldOwnsTerrain =
        m_editorModeController &&
        m_editorModeController->IsInitialized() &&
        m_editorModeController->GetWorld() != nullptr;
    if (!editorWorldOwnsTerrain &&
        m_terrainEnabled && m_activeCameraEntity != entt::null &&
        m_registry->GetRegistry().valid(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        auto& camTransform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        UpdateDynamicChunkLoading(camTransform.position);
    }

    // Update all rotation components (spinning cube)
    auto viewRot = m_registry->View<Scene::RotationComponent, Scene::TransformComponent>();
    for (auto entity : viewRot) {
        auto& rotation = viewRot.get<Scene::RotationComponent>(entity);
        auto& transform = viewRot.get<Scene::TransformComponent>(entity);

        float angle = rotation.speed * deltaTime;
        glm::quat rotationDelta = glm::angleAxis(angle, glm::normalize(rotation.axis));
        transform.rotation = rotationDelta * transform.rotation;
    }

    if (m_currentScenePreset == ScenePreset::TemporalValidation && m_registry) {
        const float phase = static_cast<float>(m_totalFrameCount) * (1.0f / 60.0f);
        auto view = m_registry->View<Scene::TagComponent, Scene::TransformComponent>();
        for (auto entity : view) {
            const auto& tag = view.get<Scene::TagComponent>(entity).tag;
            auto& transform = view.get<Scene::TransformComponent>(entity);
            if (tag == "TemporalLab_RotatingChromeBlock") {
                transform.position.x = -1.35f + std::sin(phase * 7.5f) * 0.75f;
                transform.position.z = 0.15f + std::cos(phase * 5.0f) * 0.35f;
            } else if (tag == "TemporalLab_RotatingRedSphere") {
                transform.position.x = std::sin(phase * 6.8f) * 0.65f;
                transform.position.y = 0.85f + std::sin(phase * 8.0f) * 0.28f;
            } else if (tag == "TemporalLab_EmissiveSpinner") {
                transform.position.x = 1.35f + std::cos(phase * 7.1f) * 0.65f;
                transform.position.z = 0.15f + std::sin(phase * 5.4f) * 0.34f;
            } else if (tag == "TemporalLab_AlphaMaskPanel") {
                transform.position.x = std::sin(phase * 5.5f) * 0.75f;
            }
        }
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
        Graphics::ApplyDebugOverlayControl(*m_renderer, m_settingsOverlayVisible, m_settingsSection);
    }

    // === Engine Editor Mode: Selective Renderer Usage (Phase 7) ===
    // When in editor mode, use RenderFull() which controls individual render
    // passes based on EditorState. Otherwise, use the monolithic Render().
    if (m_engineEditorMode && m_editorModeController && m_editorModeController->IsInitialized()) {
        // Editor controls the full render flow (selective pass usage)
        m_editorModeController->RenderFull(deltaTime);
    } else {
        // Tech Demo mode: use monolithic render
        // Add editor debug overlays if present (for hybrid mode)
        if (m_editorModeController && m_editorModeController->IsInitialized()) {
            m_editorModeController->Render();
        }
        m_renderer->Render(m_registry.get(), deltaTime);
    }

    // Render HUD overlay using GDI on top of the swap chain. Even when the HUD
    // mode is off, keep RenderHUD() active while overlays are visible so the
    // menu legend and row labels remain accessible.
    if (m_hudMode != EngineHudMode::Off || m_settingsOverlayVisible || UI::DebugMenu::IsVisible()) {
        RenderHUD();
    }

    if (m_renderer) {
        m_perf.Update(*this, *m_renderer);

        m_qualityGovernorTimer += deltaTime;
        if (m_qualityGovernorTimer >= 1.0f) {
            m_qualityGovernorTimer = 0.0f;
            ApplyVRAMQualityGovernor();
            ApplyPerfQualityGovernor();
        }

        m_frameReportTimer += deltaTime;
        if (m_frameReportTimer >= 2.0f) {
            m_frameReportTimer = 0.0f;
            WriteFrameDiagnosticsReport();
        }

        if (!m_visualValidationCaptured && m_renderer->HasCapturedVisualValidation()) {
            m_visualValidationCaptured = true;
            m_visualValidationCapturedFrame = m_totalFrameCount;
        }

        if (m_exitAfterVisualValidationCapture && m_visualValidationCaptured) {
            constexpr uint64_t kReadbackSettleFrames = 3;
            const uint64_t framesSinceCapture =
                (m_totalFrameCount >= m_visualValidationCapturedFrame)
                    ? (m_totalFrameCount - m_visualValidationCapturedFrame)
                    : 0;
            if (framesSinceCapture >= kReadbackSettleFrames) {
                WriteFrameDiagnosticsReport();
                spdlog::info("Smoke automation captured visual validation image; exiting main loop");
                m_running = false;
            }
        }
    }
}

void Engine::WriteFrameDiagnosticsReport(bool shutdownSnapshot) {
    if (!m_renderer) {
        return;
    }

    namespace fs = std::filesystem;
    constexpr double kToMB = 1.0 / (1024.0 * 1024.0);

    json report;
    report["schema"] = "cortex.frame_report.v1";
    report["lifecycle"] = shutdownSnapshot ? "shutdown" : "active_frame";
    report["frame_time_ms"] = static_cast<double>(m_frameTime) * 1000.0;
    report["avg_frame_time_ms"] = m_avgFrameTimeMs;

    const auto sceneName = [this]() -> const char* {
        switch (m_currentScenePreset) {
        case ScenePreset::CornellBox: return "cornell_box";
        case ScenePreset::DragonOverWater: return "dragon_over_water";
        case ScenePreset::RTShowcase: return "rt_showcase";
        case ScenePreset::MaterialLab: return "material_lab";
        case ScenePreset::GlassWaterCourtyard: return "glass_water_courtyard";
        case ScenePreset::EffectsShowcase: return "effects_showcase";
        case ScenePreset::GodRays: return "god_rays";
        case ScenePreset::TemporalValidation: return "temporal_validation";
        case ScenePreset::ProceduralTerrain: return "procedural_terrain";
        default: return "default";
        }
    };
    report["scene"] = sceneName();

    if (m_window) {
        report["window"] = {
            {"width", m_window->GetWidth()},
            {"height", m_window->GetHeight()}
        };
    }

    if (m_registry &&
        m_activeCameraEntity != entt::null &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
        const auto& cameraTransform =
            m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        const glm::vec3 forward =
            glm::normalize(cameraTransform.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
        report["camera"] = {
            {"active", true},
            {"bookmark", m_activeCameraBookmark},
            {"position", {
                {"x", cameraTransform.position.x},
                {"y", cameraTransform.position.y},
                {"z", cameraTransform.position.z}
            }},
            {"forward", {
                {"x", forward.x},
                {"y", forward.y},
                {"z", forward.z}
            }}
        };
    } else {
        report["camera"] = {{"active", false}};
    }

    const auto quality = m_renderer->GetQualityState();
    const auto features = m_renderer->GetFeatureState();
    const auto rt = m_renderer->GetRayTracingState();

    report["renderer"] = {
        {"render_scale", quality.renderScale},
        {"ray_tracing_supported", rt.supported},
        {"ray_tracing_enabled", rt.enabled},
        {"rt_reflections_enabled", rt.reflectionsEnabled},
        {"rt_gi_enabled", rt.giEnabled},
        {"ssr_enabled", features.ssrEnabled},
        {"ssao_enabled", features.ssaoEnabled},
        {"taa_enabled", features.taaEnabled},
        {"fxaa_enabled", features.fxaaEnabled},
        {"ibl_enabled", features.iblEnabled},
        {"fog_enabled", features.fogEnabled},
        {"shadows_enabled", quality.shadowsEnabled},
        {"gpu_culling_enabled", m_renderer->IsGPUCullingEnabled()},
        {"voxel_backend_enabled", m_renderer->IsVoxelBackendEnabled()},
        {"debug_view_mode", quality.debugViewMode},
        {"device_removed", m_renderer->IsDeviceRemoved()}
    };

    const auto& contract = m_renderer->GetFrameContract();
    report["frame_contract"] = Graphics::FrameContractToJson(contract);

    const auto mem = m_renderer->GetEstimatedVRAMBreakdown();
    report["memory_mb"] = {
        {"total_estimated", static_cast<double>(mem.TotalBytes()) * kToMB},
        {"render_targets", static_cast<double>(mem.renderTargetBytes) * kToMB},
        {"post_process", static_cast<double>(mem.postProcessBytes) * kToMB},
        {"debug", static_cast<double>(mem.debugBytes) * kToMB},
        {"voxel", static_cast<double>(mem.voxelBytes) * kToMB},
        {"textures", static_cast<double>(mem.textureBytes) * kToMB},
        {"environment", static_cast<double>(mem.environmentBytes) * kToMB},
        {"geometry", static_cast<double>(mem.geometryBytes) * kToMB},
        {"rt_structures", static_cast<double>(mem.rtStructureBytes) * kToMB}
    };

    if (m_device) {
        auto vram = m_device->QueryVideoMemoryInfo();
        if (vram.IsOk()) {
            const auto& info = vram.Value();
            report["dxgi_memory_mb"] = {
                {"current_usage", static_cast<double>(info.currentUsageBytes) * kToMB},
                {"budget", static_cast<double>(info.budgetBytes) * kToMB},
                {"available_for_reservation", static_cast<double>(info.availableForReservationBytes) * kToMB}
            };
        }
    }

    const auto descriptors = m_renderer->GetDescriptorStats();
    report["descriptors"] = {
        {"rtv_used", descriptors.rtvUsed},
        {"rtv_capacity", descriptors.rtvCapacity},
        {"dsv_used", descriptors.dsvUsed},
        {"dsv_capacity", descriptors.dsvCapacity},
        {"shader_visible_used", descriptors.shaderVisibleUsed},
        {"shader_visible_capacity", descriptors.shaderVisibleCapacity},
        {"persistent_used", descriptors.persistentUsed},
        {"persistent_reserve", descriptors.persistentReserve},
        {"transient_start", descriptors.transientStart},
        {"transient_end", descriptors.transientEnd},
        {"staging_used", descriptors.stagingUsed},
        {"staging_capacity", descriptors.stagingCapacity},
        {"bindless_allocated", descriptors.bindlessAllocated},
        {"bindless_capacity", descriptors.bindlessCapacity}
    };

    report["cpu_pass_ms"] = {
        {"depth_prepass", m_renderer->GetLastDepthPrepassTimeMS()},
        {"shadow", m_renderer->GetLastShadowPassTimeMS()},
        {"main", m_renderer->GetLastMainPassTimeMS()},
        {"rt", m_renderer->GetLastRTTimeMS()},
        {"ssr", m_renderer->GetLastSSRTimeMS()},
        {"ssao", m_renderer->GetLastSSAOTimeMS()},
        {"bloom", m_renderer->GetLastBloomTimeMS()},
        {"post", m_renderer->GetLastPostTimeMS()}
    };

    report["jobs"] = {
        {"pending_mesh_uploads", m_renderer->GetPendingMeshJobs()},
        {"pending_blas_jobs", m_renderer->GetPendingBLASJobs()},
        {"rt_warming_up", m_renderer->IsRTWarmingUp()}
    };

    const auto& textureQueue = m_renderer->GetTextureUploadQueueStats();
    report["texture_upload_queue"] = {
        {"submitted", textureQueue.submittedJobs},
        {"completed", textureQueue.completedJobs},
        {"failed", textureQueue.failedJobs},
        {"pending", textureQueue.pendingJobs},
        {"uploaded_mb", static_cast<double>(textureQueue.uploadedResidentBytes) * kToMB},
        {"total_upload_ms", textureQueue.totalUploadMs},
        {"avg_upload_ms", textureQueue.AverageUploadMs()},
        {"last_upload_ms", textureQueue.lastUploadMs}
    };

    report["governors"] = {
        {"vram_reduced", DidVRAMGovernorReduce()},
        {"perf_adjusted", DidPerfGovernorAdjust()},
        {"perf_scale_reduced", WasPerfScaleReduced()}
    };

    const auto& registry = m_renderer->GetAssetRegistry();
    report["asset_budgets"] = {
        {"textures_over", registry.IsTextureBudgetExceeded()},
        {"environment_over", registry.IsEnvironmentBudgetExceeded()},
        {"geometry_over", registry.IsGeometryBudgetExceeded()},
        {"rt_over", registry.IsRTBudgetExceeded()}
    };

    json topTextures = json::array();
    for (const auto& asset : registry.GetHeaviestTextures(5)) {
        topTextures.push_back({{"key", asset.key}, {"mb", static_cast<double>(asset.bytes) * kToMB}});
    }
    json topMeshes = json::array();
    for (const auto& asset : registry.GetHeaviestMeshes(5)) {
        topMeshes.push_back({{"key", asset.key}, {"mb", static_cast<double>(asset.bytes) * kToMB}});
    }
    report["top_textures"] = std::move(topTextures);
    report["top_meshes"] = std::move(topMeshes);

    const auto* gpu = m_renderer->GetLastGPUProfile();
    if (gpu) {
        report["gpu_frame_ms"] = gpu->GetTotalGPUTimeMs();
        json passes = json::array();
        for (const auto& ts : gpu->timestamps) {
            passes.push_back({
                {"name", ts.name ? ts.name : ""},
                {"category", ts.category ? ts.category : ""},
                {"ms", ts.GetDurationMs(gpu->gpuFrequency)},
                {"depth", ts.depth}
            });
        }
        report["gpu_passes"] = std::move(passes);
    } else {
        report["gpu_frame_ms"] = nullptr;
        report["gpu_passes"] = json::array();
    }

    json health = json::array();
    for (const auto& warning : contract.warnings) {
        health.push_back("frame_contract:" + warning);
    }
    if (m_currentScenePreset == ScenePreset::RTShowcase &&
        rt.supported &&
        !rt.enabled) {
        health.push_back("rt_showcase_supported_but_ray_tracing_disabled");
    }
    if (!gpu) {
        health.push_back("gpu_profile_unavailable");
    } else if (gpu->GetTotalGPUTimeMs() <= 0.0) {
        health.push_back("gpu_frame_time_zero");
    }
    if (m_renderer->IsRTWarmingUp()) {
        health.push_back("rt_warming_up");
    }
    if (textureQueue.failedJobs > 0) {
        health.push_back("texture_upload_queue_failed");
    }
    if (descriptors.rtvCapacity > 0 &&
        descriptors.rtvUsed * 10u >= descriptors.rtvCapacity * 8u) {
        health.push_back("rtv_descriptor_heap_high_water");
    }
    if (descriptors.dsvCapacity > 0 &&
        descriptors.dsvUsed * 10u >= descriptors.dsvCapacity * 8u) {
        health.push_back("dsv_descriptor_heap_high_water");
    }
    if (descriptors.stagingCapacity > 0 &&
        descriptors.stagingUsed * 10u >= descriptors.stagingCapacity * 8u) {
        health.push_back("staging_descriptor_heap_high_water");
    }
    if (descriptors.transientEnd > descriptors.transientStart &&
        descriptors.shaderVisibleUsed > descriptors.transientStart) {
        const uint32_t transientCapacity = descriptors.transientEnd - descriptors.transientStart;
        const uint32_t transientUsed = descriptors.shaderVisibleUsed - descriptors.transientStart;
        if (transientUsed * 10u >= transientCapacity * 8u) {
            health.push_back("transient_descriptor_segment_high_water");
        }
    }

    const fs::path logDir = GetExecutableLogDirectory();
    const bool visualValidationRequested =
        std::getenv("CORTEX_CAPTURE_VISUAL_VALIDATION") != nullptr;
    const bool captureVisualValidation =
        visualValidationRequested &&
        !m_visualValidationCaptured;
    const fs::path visualValidationPath = logDir / "visual_validation_rt_showcase.bmp";

    std::error_code ec;
    fs::create_directories(logDir, ec);
    if (ec) {
        return;
    }

    const bool visualCaptured = visualValidationRequested && fs::exists(visualValidationPath);
    json visualStatsJson = nullptr;
    if (visualCaptured) {
        const VisualValidationStats stats = AnalyzeBMP(visualValidationPath);
        visualStatsJson = VisualStatsToJson(stats);
        if (!stats.valid) {
            health.push_back("visual_validation:" + stats.reason);
        } else {
            if (stats.nonBlackRatio < 0.05) {
                health.push_back("visual_validation_mostly_black");
            }
            if (stats.avgLuma < 2.0) {
                health.push_back("visual_validation_too_dark");
            }
            if (stats.saturatedRatio > 0.12 || stats.nearWhiteRatio > 0.14) {
                health.push_back("visual_validation_overexposed");
            }
            if (stats.centerAvgLuma > 210.0) {
                health.push_back("visual_validation_center_too_bright");
            }
            if (stats.width <= 0 || stats.height <= 0) {
                health.push_back("visual_validation_invalid_dimensions");
            }
        }
    }

    if (visualValidationRequested) {
        report["visual_validation"] = {
            {"enabled", true},
            {"capture_path", visualValidationPath.string()},
            {"captured", visualCaptured},
            {"image_stats", visualStatsJson}
        };
    } else {
        report["visual_validation"] = {
            {"enabled", false},
            {"capture_path", ""},
            {"captured", false},
            {"image_stats", nullptr}
        };
    }
    report["smoke_automation"] = {
        {"max_frames", m_maxFrames},
        {"total_frames", m_totalFrameCount},
        {"exit_after_visual_validation_capture", m_exitAfterVisualValidationCapture}
    };
    report["hud"] = {
        {"mode", HudModeName(m_hudMode)},
        {"visible", m_hudMode != EngineHudMode::Off},
        {"overlay_forced", m_settingsOverlayVisible || UI::DebugMenu::IsVisible()}
    };
    report["health_warnings"] = std::move(health);

    const fs::path lastPath =
        logDir / (shutdownSnapshot ? "frame_report_shutdown.json" : "frame_report_last.json");
    {
        std::ofstream out(lastPath, std::ios::trunc);
        if (out) {
            out << report.dump(2);
        }
    }

    const fs::path historyPath = logDir / "frame_report_history.jsonl";
    {
        std::ofstream out(historyPath, std::ios::app);
        if (out) {
            out << report.dump() << '\n';
        }
    }

    if (captureVisualValidation && visualCaptured) {
        m_visualValidationCaptured = true;
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
    case ScenePreset::MaterialLab:
    case ScenePreset::GlassWaterCourtyard:
    case ScenePreset::EffectsShowcase:
    case ScenePreset::GodRays:
    case ScenePreset::TemporalValidation:
        break;
    case ScenePreset::ProceduralTerrain:
        if (IsExperimentalTerrainEnabled()) {
            break;
        }
        spdlog::warn("Procedural terrain preset is hidden; falling back to RT Showcase");
        m_currentScenePreset = ScenePreset::RTShowcase;
        break;
    default:
        m_currentScenePreset = ScenePreset::RTShowcase;
        break;
    }

    RebuildScene(m_currentScenePreset);
}

} // namespace Cortex
