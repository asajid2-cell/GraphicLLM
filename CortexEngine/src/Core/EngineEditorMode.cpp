// EngineEditorMode.cpp
// Implementation of the Engine Editor parallel architecture mode.

#include "EngineEditorMode.h"
#include "Engine.h"
#include "Window.h"
#include "Graphics/Renderer.h"
#include "Scene/ECS_Registry.h"
#include "Editor/EditorWorld.h"
#include "Editor/EditorCamera.h"
#include "Utils/ConfigLoader.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Cortex {

EngineEditorMode::EngineEditorMode() = default;

EngineEditorMode::~EngineEditorMode() {
    if (m_initialized) {
        Shutdown();
    }
}

Result<void> EngineEditorMode::Initialize(Engine* engine, Graphics::Renderer* renderer, Scene::ECS_Registry* registry) {
    if (m_initialized) {
        return Result<void>::Err("EngineEditorMode already initialized");
    }

    if (!engine) {
        return Result<void>::Err("Engine pointer is null");
    }

    if (!renderer) {
        return Result<void>::Err("Renderer pointer is null");
    }

    if (!registry) {
        return Result<void>::Err("Registry pointer is null");
    }

    m_engine = engine;
    m_renderer = renderer;
    m_registry = registry;

    // Load configuration from JSON files
    auto configResult = Utils::ConfigLoader::LoadEditorDefaults("assets/config");
    Utils::EditorConfig editorConfig;
    if (configResult.IsOk()) {
        editorConfig = configResult.Value();
        spdlog::info("Loaded editor configuration from assets/config/editor_defaults.json");
    } else {
        spdlog::warn("Using default editor configuration: {}", configResult.Error());
    }

    // Apply editor state from config
    m_state = EditorState{};
    m_state.showGrid = editorConfig.debug.showGrid;
    m_state.showGizmos = editorConfig.debug.showGizmos;
    m_state.showChunkBounds = editorConfig.debug.showChunkBounds;
    m_state.showStats = editorConfig.debug.showStats;
    m_state.timeOfDay = editorConfig.timeOfDay.defaultHour;
    m_state.timePaused = !editorConfig.timeOfDay.autoAdvance;
    m_state.timeScale = editorConfig.timeOfDay.scale;
    m_state.proceduralSky = editorConfig.rendering.proceduralSky;
    m_state.shadows = editorConfig.rendering.shadows;
    m_state.ssao = editorConfig.rendering.ssao;

    // Initialize EditorCamera with settings from config
    m_camera = std::make_unique<EditorCamera>();
    m_camera->SetFlySpeed(editorConfig.camera.flySpeed);
    m_camera->SetSprintMultiplier(3.0f);  // Default sprint multiplier
    m_camera->SetMouseSensitivity(editorConfig.camera.mouseSensitivity);
    m_camera->SetFOV(editorConfig.camera.fov);
    m_camera->SetNearFar(editorConfig.camera.nearPlane, editorConfig.camera.farPlane);

    // Load terrain presets
    auto presetsResult = Utils::ConfigLoader::LoadTerrainPresets("assets/config");
    Scene::TerrainNoiseParams terrainParams;
    if (presetsResult.IsOk()) {
        auto presets = presetsResult.Value();
        // Use first preset as default (the "default" key is typically parsed first)
        // Preset names in JSON are display names like "Rolling Hills", not "default"
        if (!presets.empty()) {
            terrainParams = presets[0].params;
            spdlog::info("Using terrain preset: {}", presets[0].name);
        }
    } else {
        // Fallback terrain params
        terrainParams = Scene::TerrainNoiseParams{
            .seed = 42,
            .amplitude = 20.0f,
            .frequency = 0.003f,
            .octaves = 6,
            .lacunarity = 2.0f,
            .gain = 0.5f,
            .warp = 15.0f
        };
    }

    // Initialize EditorWorld for terrain management
    EditorWorldConfig worldConfig;
    Utils::ConfigLoader::ApplyToWorldConfig(editorConfig, worldConfig);
    worldConfig.terrainParams = terrainParams;

    m_world = std::make_unique<EditorWorld>();
    auto worldResult = m_world->Initialize(renderer, registry, worldConfig);
    if (worldResult.IsErr()) {
        return Result<void>::Err("Failed to initialize EditorWorld: " + worldResult.Error());
    }

    // Set initial camera position (above terrain center)
    float terrainHeight = m_world->GetTerrainHeight(0.0f, 0.0f);
    m_camera->SetPosition(glm::vec3(0.0f, terrainHeight + 50.0f, 0.0f));
    m_camera->SetYawPitch(0.0f, -0.3f);

    // Set up terrain height callback for camera ground clamping
    // Use a static lambda with captured world pointer
    m_camera->SetTerrainHeightCallback(
        [](float x, float z, void* userData) -> float {
            auto* world = static_cast<EditorWorld*>(userData);
            return world->GetTerrainHeight(x, z);
        },
        m_world.get()
    );
    m_camera->SetMinHeightAboveTerrain(2.0f);

    // Configure renderer for editor mode
    // - Disable IBL (use procedural sky for terrain)
    // - Enable shadows
    // - Set initial sun position from time of day
    m_renderer->SetIBLEnabled(false);
    UpdateTimeOfDay(0.0f);  // Initialize sun position

    m_initialized = true;
    spdlog::info("EngineEditorMode initialized with EditorWorld");

    return Result<void>::Ok();
}

void EngineEditorMode::Shutdown() {
    if (!m_initialized) {
        return;
    }

    spdlog::info("EngineEditorMode shutting down");

    // Shutdown EditorWorld first
    if (m_world) {
        m_world->Shutdown();
        m_world.reset();
    }

    // Release EditorCamera
    m_camera.reset();

    m_engine = nullptr;
    m_renderer = nullptr;
    m_registry = nullptr;
    m_initialized = false;
}

void EngineEditorMode::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update time of day (if not paused)
    if (!m_state.timePaused) {
        m_state.timeOfDay += deltaTime * m_state.timeScale / 3600.0f;
        if (m_state.timeOfDay >= 24.0f) {
            m_state.timeOfDay -= 24.0f;
        }
    }

    // Update sun position based on time
    UpdateTimeOfDay(deltaTime);

    // Update camera movement input from keyboard state
    UpdateCamera(deltaTime);

    // Update EditorCamera (handles movement, transitions, terrain clamping)
    if (m_camera) {
        m_camera->Update(deltaTime);
    }

    // Update EditorWorld (chunk streaming based on camera position)
    if (m_world && m_camera) {
        m_world->Update(m_camera->GetPosition(), deltaTime);
    }
}

float EngineEditorMode::GetTerrainHeight(float worldX, float worldZ) const {
    if (m_world) {
        return m_world->GetTerrainHeight(worldX, worldZ);
    }
    return 0.0f;
}

glm::vec3 EngineEditorMode::GetCameraPosition() const {
    if (m_camera) {
        return m_camera->GetPosition();
    }
    return glm::vec3(0.0f);
}

float EngineEditorMode::GetCameraYaw() const {
    if (m_camera) {
        return m_camera->GetYaw();
    }
    return 0.0f;
}

float EngineEditorMode::GetCameraPitch() const {
    if (m_camera) {
        return m_camera->GetPitch();
    }
    return 0.0f;
}

void EngineEditorMode::Render() {
    if (!m_initialized || !m_renderer) {
        return;
    }

    // The actual geometry rendering is handled by the Engine's existing Render() call.
    // EngineEditorMode configures renderer state and adds editor-specific overlays.

    // Add debug visualizations (grid, axes, chunk bounds)
    // These use Renderer::AddDebugLine which gets rendered during the debug pass
    RenderDebugOverlays();

    // Render stats overlay if enabled
    if (m_state.showStats) {
        RenderStats();
    }
}

void EngineEditorMode::RenderFull(float deltaTime) {
    if (!m_initialized || !m_renderer || !m_registry) {
        return;
    }

    // === Phase 7: Selective Renderer Usage ===
    // Instead of calling the monolithic Renderer::Render(), we control the
    // render flow by calling individual passes. This allows the Engine Editor
    // to selectively enable/disable passes based on EditorState.

    // Begin frame (handles swap chain, command list allocation, etc.)
    m_renderer->BeginFrameForEditor();

    // Update per-frame constants (camera, lights, time, etc.)
    m_renderer->UpdateFrameConstantsForEditor(deltaTime, m_registry);

    // Prewarm material descriptors for all entities (required for terrain chunks)
    m_renderer->PrewarmMaterialDescriptorsForEditor(m_registry);

    // Prepare main render target
    m_renderer->PrepareMainPassForEditor();

    // Render sky (procedural sky since IBL is disabled for terrain)
    m_renderer->RenderSkyboxForEditor();

    // Render shadows if enabled
    if (m_state.shadows) {
        m_renderer->RenderShadowPassForEditor(m_registry);
    }

    // Render scene geometry (terrain chunks, entities)
    m_renderer->RenderSceneForEditor(m_registry);

    // Screen-space effects
    if (m_state.ssao) {
        m_renderer->RenderSSAOForEditor();
    }

    // Screen-space reflections (if enabled in future)
    // m_renderer->RenderSSRForEditor();

    // Temporal anti-aliasing
    m_renderer->RenderTAAForEditor();

    // Bloom
    m_renderer->RenderBloomForEditor();

    // Add debug overlays before post-process
    RenderDebugOverlays();

    // Post-process (tonemapping, color grading, FXAA)
    m_renderer->RenderPostProcessForEditor();

    // Debug lines (drawn after post for visibility)
    m_renderer->RenderDebugLinesForEditor();

    // Stats overlay
    if (m_state.showStats) {
        RenderStats();
    }

    // End frame (present swap chain)
    m_renderer->EndFrameForEditor();
}

void EngineEditorMode::ProcessInput(const SDL_Event& event) {
    if (!m_initialized) {
        return;
    }

    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            SDL_Keycode key = event.key.key;

            // G - Toggle grid
            if (key == SDLK_G && !event.key.repeat) {
                m_state.showGrid = !m_state.showGrid;
                spdlog::info("Editor grid: {}", m_state.showGrid ? "ON" : "OFF");
            }

            // B - Toggle chunk bounds visualization
            if (key == SDLK_B && !event.key.repeat) {
                m_state.showChunkBounds = !m_state.showChunkBounds;
                spdlog::info("Chunk bounds: {}", m_state.showChunkBounds ? "ON" : "OFF");
            }

            // Period/Comma - Adjust time of day
            if (key == SDLK_PERIOD) {
                AdvanceTimeOfDay(1.0f);
                spdlog::info("Time of day: {:.1f}h", m_state.timeOfDay);
            }
            if (key == SDLK_COMMA) {
                AdvanceTimeOfDay(-1.0f);
                spdlog::info("Time of day: {:.1f}h", m_state.timeOfDay);
            }

            // L - Toggle time pause
            if (key == SDLK_L && !event.key.repeat) {
                m_state.timePaused = !m_state.timePaused;
                spdlog::info("Time: {}", m_state.timePaused ? "PAUSED" : "RUNNING");
            }

            // F3 - Toggle stats overlay
            if (key == SDLK_F3 && !event.key.repeat) {
                m_state.showStats = !m_state.showStats;
            }

            // F - Focus on world origin (or selected entity in future)
            if (key == SDLK_F && !event.key.repeat && m_camera) {
                // Focus on terrain at current XZ position (smooth camera transition)
                glm::vec3 pos = m_camera->GetPosition();
                float terrainY = GetTerrainHeight(pos.x, pos.z);
                m_camera->FocusOn(glm::vec3(pos.x, terrainY, pos.z), 0.5f);
                spdlog::info("Camera focusing on terrain");
            }

            // Tab - Cycle camera mode (Fly -> Orbit -> Fly)
            if (key == SDLK_TAB && !event.key.repeat && m_camera) {
                CameraMode currentMode = m_camera->GetMode();
                if (currentMode == CameraMode::Fly) {
                    // Set orbit target to point in front of camera
                    glm::vec3 pos = m_camera->GetPosition();
                    glm::vec3 forward = m_camera->GetForward();
                    glm::vec3 orbitTarget = pos + forward * 50.0f;
                    orbitTarget.y = GetTerrainHeight(orbitTarget.x, orbitTarget.z);
                    m_camera->SetOrbitTarget(orbitTarget);
                    m_camera->SetOrbitDistance(glm::length(pos - orbitTarget));
                    m_camera->SetMode(CameraMode::Orbit);
                    spdlog::info("Camera mode: Orbit");
                } else {
                    m_camera->SetMode(CameraMode::Fly);
                    spdlog::info("Camera mode: Fly");
                }
            }

            // === Phase 8: Entity Manipulation Hotkeys ===

            // H - Toggle gizmos visibility
            if (key == SDLK_H && !event.key.repeat) {
                m_state.gizmosEnabled = !m_state.gizmosEnabled;
                m_state.showGizmos = m_state.gizmosEnabled;
                spdlog::info("Gizmos: {}", m_state.gizmosEnabled ? "ON" : "OFF");
            }

            // F5 - Toggle Edit/Play mode
            if (key == SDLK_F5 && !event.key.repeat) {
                m_state.editMode = !m_state.editMode;
                m_state.entityPickingEnabled = m_state.editMode;
                spdlog::info("Editor mode: {}", m_state.editMode ? "EDIT" : "PLAY");
            }

            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            // Forward mouse motion to EditorCamera when camera control is active
            if (m_cameraControlActive && m_camera) {
                m_camera->ProcessMouseMove(
                    static_cast<float>(event.motion.xrel),
                    static_cast<float>(event.motion.yrel)
                );
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            // Forward scroll to EditorCamera (for orbit zoom or fly speed adjustment)
            if (m_camera) {
                m_camera->ProcessMouseScroll(static_cast<float>(event.wheel.y));
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                m_cameraControlActive = true;
                if (m_engine && m_engine->GetWindow()) {
                    SDL_SetWindowRelativeMouseMode(m_engine->GetWindow()->GetSDLWindow(), true);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                m_cameraControlActive = false;
                if (m_engine && m_engine->GetWindow()) {
                    SDL_SetWindowRelativeMouseMode(m_engine->GetWindow()->GetSDLWindow(), false);
                }
            }
            break;
        }
    }
}

void EngineEditorMode::SetTimeOfDay(float hour) {
    m_state.timeOfDay = std::fmod(hour, 24.0f);
    if (m_state.timeOfDay < 0.0f) {
        m_state.timeOfDay += 24.0f;
    }
    UpdateTimeOfDay(0.0f);
}

void EngineEditorMode::AdvanceTimeOfDay(float hours) {
    SetTimeOfDay(m_state.timeOfDay + hours);
}

void EngineEditorMode::UpdateCamera(float /*deltaTime*/) {
    if (!m_camera) {
        return;
    }

    // Poll keyboard state and pass to EditorCamera for movement
    // Only process movement when camera control is active (right mouse button held)
    if (m_cameraControlActive) {
        const bool* keyState = SDL_GetKeyboardState(nullptr);

        bool forward = keyState[SDL_SCANCODE_W];
        bool back = keyState[SDL_SCANCODE_S];
        bool left = keyState[SDL_SCANCODE_A];
        bool right = keyState[SDL_SCANCODE_D];
        bool up = keyState[SDL_SCANCODE_E] || keyState[SDL_SCANCODE_SPACE];
        bool down = keyState[SDL_SCANCODE_Q] || keyState[SDL_SCANCODE_LCTRL];
        bool sprint = keyState[SDL_SCANCODE_LSHIFT];

        m_camera->SetMovementInput(forward, back, left, right, up, down, sprint);
    } else {
        // No movement when not controlling camera
        m_camera->SetMovementInput(false, false, false, false, false, false, false);
    }
}

void EngineEditorMode::UpdateTimeOfDay(float /*deltaTime*/) {
    if (!m_renderer) {
        return;
    }

    glm::vec3 sunDir = CalculateSunDirection();
    glm::vec3 sunColor = CalculateSunColor();
    float sunIntensity = CalculateSunIntensity();

    m_renderer->SetSunDirection(sunDir);
    m_renderer->SetSunColor(sunColor);
    m_renderer->SetSunIntensity(sunIntensity);
}

glm::vec3 EngineEditorMode::CalculateSunDirection() const {
    // Convert time of day to sun angle
    // 0h = midnight (sun below horizon), 6h = sunrise (east), 12h = noon (zenith), 18h = sunset (west)
    float hourAngle = (m_state.timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);

    // Simple sun arc: rises in east (+X), peaks at noon (Y=1), sets in west (-X)
    float sunY = std::cos(hourAngle);  // Height: 1 at noon, -1 at midnight
    float sunX = std::sin(hourAngle);  // East-west position
    float sunZ = 0.3f;                 // Slight offset to prevent perfectly vertical sun

    return glm::normalize(glm::vec3(sunX, std::max(sunY, -0.2f), sunZ));
}

glm::vec3 EngineEditorMode::CalculateSunColor() const {
    // Sun color varies with altitude
    float hourAngle = (m_state.timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);
    float sunAltitude = std::cos(hourAngle);

    // Warm colors at sunrise/sunset, white at noon
    if (sunAltitude > 0.5f) {
        // Midday: white-ish
        return glm::vec3(1.0f, 0.98f, 0.95f);
    } else if (sunAltitude > 0.0f) {
        // Golden hour: warm orange
        float t = sunAltitude / 0.5f;
        glm::vec3 noon(1.0f, 0.98f, 0.95f);
        glm::vec3 sunset(1.0f, 0.6f, 0.3f);
        return glm::mix(sunset, noon, t);
    } else {
        // Below horizon: dim blue (twilight)
        float t = std::clamp(-sunAltitude / 0.3f, 0.0f, 1.0f);
        glm::vec3 twilight(0.3f, 0.4f, 0.6f);
        glm::vec3 sunset(1.0f, 0.6f, 0.3f);
        return glm::mix(sunset, twilight, t);
    }
}

float EngineEditorMode::CalculateSunIntensity() const {
    float hourAngle = (m_state.timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);
    float sunAltitude = std::cos(hourAngle);

    // Intensity scales with altitude
    if (sunAltitude > 0.0f) {
        // Daytime: full intensity at noon
        return 5.0f + sunAltitude * 5.0f;  // 5-10 range
    } else {
        // Night: very dim
        return std::max(0.1f, 0.5f + sunAltitude * 2.0f);
    }
}

void EngineEditorMode::RenderTerrain() {
    // Terrain rendering is currently handled by Engine's BuildProceduralTerrainScene
    // and the standard render pipeline. In future phases, this may be moved here
    // for more selective control.
}

void EngineEditorMode::RenderSky() {
    // Sky rendering is handled by the procedural sky shader when IBL is disabled.
    // This method is a placeholder for future editor-specific sky rendering.
}

void EngineEditorMode::RenderDebugOverlays() {
    if (!m_renderer) {
        return;
    }

    // Draw origin axes (always visible in editor)
    RenderOriginAxes();

    // Draw grid if enabled
    if (m_state.showGrid) {
        RenderDebugGrid();
    }

    // Draw chunk bounds if enabled
    if (m_state.showChunkBounds && m_world) {
        RenderChunkBounds();
    }
}

void EngineEditorMode::RenderDebugGrid() {
    if (!m_renderer || !m_camera) {
        return;
    }

    // Draw a grid centered around the camera's XZ position
    const float gridSize = 64.0f;  // Match chunk size
    const int gridLines = 17;      // 17 lines = 16 cells = 4x4 chunks visible
    const float gridExtent = gridSize * 8.0f;

    glm::vec3 cameraPos = m_camera->GetPosition();

    // Snap grid to chunk boundaries
    float snapX = std::floor(cameraPos.x / gridSize) * gridSize;
    float snapZ = std::floor(cameraPos.z / gridSize) * gridSize;

    // Get terrain height at grid center for Y offset
    float gridY = 0.0f;
    if (m_world) {
        gridY = m_world->GetTerrainHeight(snapX, snapZ) + 0.5f;  // Slightly above terrain
    }

    // Grid color (subtle gray)
    glm::vec4 gridColor(0.4f, 0.4f, 0.4f, 0.5f);
    glm::vec4 majorColor(0.6f, 0.6f, 0.6f, 0.7f);  // Every 4th line is brighter

    // Draw X-aligned lines (running along X axis)
    for (int i = 0; i < gridLines; ++i) {
        float z = snapZ - gridExtent + i * gridSize;
        bool isMajor = (i % 4 == 0);
        m_renderer->AddDebugLine(
            glm::vec3(snapX - gridExtent, gridY, z),
            glm::vec3(snapX + gridExtent, gridY, z),
            isMajor ? majorColor : gridColor
        );
    }

    // Draw Z-aligned lines (running along Z axis)
    for (int i = 0; i < gridLines; ++i) {
        float x = snapX - gridExtent + i * gridSize;
        bool isMajor = (i % 4 == 0);
        m_renderer->AddDebugLine(
            glm::vec3(x, gridY, snapZ - gridExtent),
            glm::vec3(x, gridY, snapZ + gridExtent),
            isMajor ? majorColor : gridColor
        );
    }
}

void EngineEditorMode::RenderChunkBounds() {
    if (!m_renderer || !m_world) {
        return;
    }

    // Get visible chunks
    auto visibleChunks = m_world->GetVisibleChunks();
    const float chunkSize = m_world->GetConfig().chunkSize;

    // Chunk bound color (yellow)
    glm::vec4 boundColor(1.0f, 0.8f, 0.2f, 0.6f);

    for (const auto& coord : visibleChunks) {
        float minX = coord.x * chunkSize;
        float minZ = coord.z * chunkSize;
        float maxX = minX + chunkSize;
        float maxZ = minZ + chunkSize;

        // Get terrain heights at corners for more accurate bounds
        float y0 = m_world->GetTerrainHeight(minX, minZ);
        float y1 = m_world->GetTerrainHeight(maxX, minZ);
        float y2 = m_world->GetTerrainHeight(maxX, maxZ);
        float y3 = m_world->GetTerrainHeight(minX, maxZ);
        float yMin = std::min({y0, y1, y2, y3}) - 5.0f;
        float yMax = std::max({y0, y1, y2, y3}) + 5.0f;

        // Draw vertical edges
        m_renderer->AddDebugLine(glm::vec3(minX, yMin, minZ), glm::vec3(minX, yMax, minZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMin, minZ), glm::vec3(maxX, yMax, minZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMin, maxZ), glm::vec3(maxX, yMax, maxZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(minX, yMin, maxZ), glm::vec3(minX, yMax, maxZ), boundColor);

        // Draw bottom edges
        m_renderer->AddDebugLine(glm::vec3(minX, yMin, minZ), glm::vec3(maxX, yMin, minZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMin, minZ), glm::vec3(maxX, yMin, maxZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMin, maxZ), glm::vec3(minX, yMin, maxZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(minX, yMin, maxZ), glm::vec3(minX, yMin, minZ), boundColor);

        // Draw top edges
        m_renderer->AddDebugLine(glm::vec3(minX, yMax, minZ), glm::vec3(maxX, yMax, minZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMax, minZ), glm::vec3(maxX, yMax, maxZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(maxX, yMax, maxZ), glm::vec3(minX, yMax, maxZ), boundColor);
        m_renderer->AddDebugLine(glm::vec3(minX, yMax, maxZ), glm::vec3(minX, yMax, minZ), boundColor);
    }
}

void EngineEditorMode::RenderOriginAxes() {
    if (!m_renderer) {
        return;
    }

    // Draw world origin axes (XYZ = RGB)
    const float axisLength = 10.0f;

    // X axis (red)
    m_renderer->AddDebugLine(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(axisLength, 0.0f, 0.0f),
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)
    );

    // Y axis (green)
    m_renderer->AddDebugLine(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, axisLength, 0.0f),
        glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)
    );

    // Z axis (blue)
    m_renderer->AddDebugLine(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, axisLength),
        glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)
    );
}

void EngineEditorMode::RenderStats() {
    // Stats overlay will be implemented in future phases
    // For now, Engine's existing HUD handles this
}

} // namespace Cortex
