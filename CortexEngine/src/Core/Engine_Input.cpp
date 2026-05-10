#include "Engine.h"
#include "Graphics/RendererControlApplier.h"
#include "EngineEditorMode.h"
#include "Editor/EditorWorld.h"
#include "Graphics/Renderer.h"
#include "Scene/Components.h"
#include "UI/TextPrompt.h"
#include "UI/DebugMenu.h"
#include "UI/QuickSettingsWindow.h"
#include "UI/QualitySettingsWindow.h"
#include "UI/GraphicsSettingsWindow.h"
#include "UI/SceneEditorWindow.h"
#include "UI/PerformanceWindow.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Cortex {
namespace {
    using nlohmann::json;
    constexpr float kHeroPoolZ = -3.0f;

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

    std::filesystem::path GetDebugMenuStatePath() {
        return std::filesystem::current_path() / "debug_menu_state.json";
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
        }
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

    inline void ComputeGizmoScale(float distance,
                                  float& outAxisLength,
                                  float& outThreshold) {
        distance = std::max(distance, 0.1f);
        float axisLength = distance * 0.15f;
        axisLength = std::clamp(axisLength, 0.5f, 10.0f);
        float threshold = axisLength * 0.15f;

        outAxisLength = axisLength;
        outThreshold = threshold;
    }
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

        // Forward events to Engine Editor Mode controller if active
        if (m_editorModeController && m_editorModeController->IsInitialized()) {
            m_editorModeController->ProcessInput(event);
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
                    } else if (UI::GraphicsSettingsWindow::IsVisible()) {
                        UI::GraphicsSettingsWindow::SetVisible(false);
                        spdlog::info("Graphics settings window HIDDEN (ESC)");
                    } else if (UI::QualitySettingsWindow::IsVisible()) {
                        UI::QualitySettingsWindow::SetVisible(false);
                        spdlog::info("Quality settings window HIDDEN (ESC)");
                    } else if (UI::PerformanceWindow::IsVisible()) {
                        UI::PerformanceWindow::SetVisible(false);
                        spdlog::info("Performance window HIDDEN (ESC)");
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
                      // Unified Phase 3 graphics tuning window backed by
                      // RendererTuningState and renderer control appliers.
                      UI::GraphicsSettingsWindow::Toggle();
                      spdlog::info("Graphics settings window toggled (F8)");
                      break;
                  }
                  if (key == SDLK_B) {
                      ApplyHeroVisualBaseline();
                      break;
                  }
                  if (key == SDLK_K) {
                      if (m_renderer) {
                          const bool enabled = Graphics::ToggleGPUCullingFreezeControl(*m_renderer);
                          spdlog::info("GPU culling freeze {} (K)", enabled ? "ENABLED" : "DISABLED");
                      }
                      break;
                  }
                  if (key == SDLK_LEFTBRACKET || key == SDLK_RIGHTBRACKET) {
                      if (m_renderer && m_renderer->GetQualityState().debugViewMode == 32) {
                          const int delta = (key == SDLK_LEFTBRACKET) ? -1 : 1;
                          Graphics::ApplyHZBDebugMipDeltaControl(*m_renderer, delta);
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
                if (key == SDLK_F5) {
                    // Toggle play mode (FPS controls with terrain collision)
                    if (m_playModeActive) {
                        ExitPlayMode();
                    } else {
                        EnterPlayMode();
                    }
                    break;
                }
                if (key == SDLK_E && m_playModeActive) {
                    // Interact with hovered object
                    m_interactionSystem.OnInteractPressed();
                    break;
                }
                if (key == SDLK_Q && m_playModeActive) {
                    // Drop held object
                    m_interactionSystem.OnDropPressed();
                    break;
                }
                if (key == SDLK_G && m_playModeActive) {
                    // Throw held object
                    m_interactionSystem.OnThrowPressed();
                    break;
                }
                if (key == SDLK_N) {
                    // Scene preset toggle: cycles through curated IBL scenes
                    ToggleScenePreset();
                    break;
                }
                if (key == SDLK_J) {
                    if (!IsExperimentalTerrainEnabled()) {
                        spdlog::info("Experimental terrain is hidden. Set CORTEX_ENABLE_EXPERIMENTAL_TERRAIN=1 to enable it.");
                        break;
                    }

                    // Toggle play mode for terrain exploration
                    // When EditorWorld is active, it already manages terrain - just toggle play mode
                    if (m_editorModeController && m_editorModeController->IsInitialized()) {
                        if (m_playModeActive) {
                            ExitPlayMode();
                            spdlog::info("Exited Terrain Play Mode (J)");
                        } else {
                            // Enable terrain for physics before entering play mode
                            m_terrainEnabled = true;
                            if (m_editorModeController->GetWorld()) {
                                m_terrainParams = m_editorModeController->GetWorld()->GetTerrainParams();
                            }
                            EnterPlayMode();
                            spdlog::info("Entered Terrain Play Mode (J) - WASD to move, Space to jump");
                        }
                    } else {
                        // Legacy behavior: rebuild scene if EditorWorld not active
                        if (m_currentScenePreset == ScenePreset::ProceduralTerrain) {
                            RebuildScene(ScenePreset::RTShowcase);
                            spdlog::info("Exited Terrain World (J)");
                        } else {
                            RebuildScene(ScenePreset::ProceduralTerrain);
                            if (!m_playModeActive) { EnterPlayMode(); }
                            spdlog::info("Entered Terrain World (J) - WASD to move, Space to jump");
                        }
                    }
                    break;
                }
                if (key == SDLK_1 || key == SDLK_2 || key == SDLK_3) {
                    const char* bookmarkId =
                        (key == SDLK_1) ? "hero" :
                        (key == SDLK_2) ? "reflection_closeup" :
                                          "material_overview";
                    if (ApplyShowcaseCameraBookmark(bookmarkId)) {
                        break;
                    }

                    // Camera bookmarks for the current scene preset.
                    if (m_registry && m_activeCameraEntity != entt::null &&
                        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity)) {
                        m_autoDemoEnabled = false;
                        m_activeCameraBookmark.clear();
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
                        t.rotation = glm::quatLookAtLH(forward, up);

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
                    // GPU overlay (in-shader menu) toggle M does not affect
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
                // Time-of-Day Controls (works in any scene, but most visible in terrain)
                // -----------------------------------------------------------------
                if (key == SDLK_PERIOD) {  // '.' = advance time +1 hour
                    m_worldState.AdvanceTime(1.0f);
                    m_worldState.Update(0.0f);  // Recalculate sun immediately
                    spdlog::info("Time: {:.1f}h ({})", m_worldState.timeOfDay,
                        m_worldState.timeOfDay < 6.0f || m_worldState.timeOfDay >= 18.0f ? "night" :
                        m_worldState.timeOfDay < 12.0f ? "morning" : "afternoon");
                    break;
                }
                if (key == SDLK_COMMA) {  // ',' = rewind time -1 hour
                    m_worldState.AdvanceTime(-1.0f);
                    m_worldState.Update(0.0f);  // Recalculate sun immediately
                    spdlog::info("Time: {:.1f}h ({})", m_worldState.timeOfDay,
                        m_worldState.timeOfDay < 6.0f || m_worldState.timeOfDay >= 18.0f ? "night" :
                        m_worldState.timeOfDay < 12.0f ? "morning" : "afternoon");
                    break;
                }
                if (key == SDLK_L) {  // 'L' = toggle time pause
                    m_worldState.timePaused = !m_worldState.timePaused;
                    spdlog::info("Time {} at {:.1f}h",
                        m_worldState.timePaused ? "PAUSED" : "RUNNING",
                        m_worldState.timeOfDay);
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
                                if (renderer && renderer->GetRayTracingState().supported) {
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
                            if (renderer && renderer->GetRayTracingState().supported) {
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
                        bool enabled = Graphics::ToggleFeatureControl(*m_renderer, Graphics::RendererFeatureToggle::FXAA);
                        spdlog::info("FXAA {}", enabled ? "ENABLED" : "DISABLED");
                    }
                }
                else if (key == SDLK_Z) {
                    if (m_renderer) {
                        Graphics::ToggleFeatureControl(*m_renderer, Graphics::RendererFeatureToggle::TAA);
                    }
                }
                else if (key == SDLK_F5) {
                    if (m_renderer) {
                        Graphics::ApplyShadowPCFRadiusDeltaControl(*m_renderer, 0.5f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F7) {
                    if (m_renderer) {
                        Graphics::ApplyShadowBiasDeltaControl(*m_renderer, -0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F8) {
                    if (m_renderer) {
                        Graphics::ApplyShadowBiasDeltaControl(*m_renderer, 0.0002f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F9) {
                    if (m_renderer) {
                        Graphics::ApplyCascadeSplitLambdaDeltaControl(*m_renderer, -0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F10) {
                    if (m_renderer) {
                        Graphics::ApplyCascadeSplitLambdaDeltaControl(*m_renderer, 0.05f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F11) {
                    if (m_renderer) {
                        Graphics::ApplyCascadeResolutionScaleDeltaControl(*m_renderer, 0, -0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_F12) {
                    if (m_renderer) {
                        Graphics::ApplyCascadeResolutionScaleDeltaControl(*m_renderer, 0, 0.1f);
                        SyncDebugMenuFromRenderer();
                    }
                }
                else if (key == SDLK_V) {
                    if (m_renderer) {
                        const auto rt = m_renderer->GetRayTracingState();
                        if (!rt.supported) {
                            spdlog::info("Ray tracing not supported on this GPU; V toggle ignored");
                        } else {
                            bool enabled = !rt.enabled;
                            Graphics::ApplyFeatureToggleControl(*m_renderer, Graphics::RendererFeatureToggle::RayTracing, enabled);
                            // Keep debug menu state in sync with renderer
                            SyncDebugMenuFromRenderer();
                            spdlog::info("Ray tracing {}", enabled ? "ENABLED" : "DISABLED");
                        }
                    }
                }
                else if (event.key.key == SDLK_F3) {
                    if (m_renderer) {
                        Graphics::ToggleFeatureControl(*m_renderer, Graphics::RendererFeatureToggle::Shadows);
                    }
                }
                else if (event.key.key == SDLK_F4) {
                    if (m_renderer) {
                        Graphics::CycleDebugViewControl(*m_renderer);
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
                        Graphics::CycleEnvironmentPresetControl(*m_renderer);
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
                } else if (m_cameraControlActive || m_playModeActive) {
                    // Accumulate mouse deltas for camera look (both editor mode and play mode)
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

} // namespace Cortex
