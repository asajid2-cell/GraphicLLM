#pragma once

// EngineEditorMode.h
// Core controller for the Engine Editor parallel architecture mode.
// This class manages the editor state and uses the Renderer as a library
// rather than calling its monolithic Render() function.

#include <memory>
#include <glm/glm.hpp>
#include "Utils/Result.h"

// Forward declarations
union SDL_Event;

namespace Cortex {

class Engine;
class EditorWorld;

namespace Graphics {
    class Renderer;
}

namespace Scene {
    class ECS_Registry;
    struct TerrainNoiseParams;
}

// Engine Editor Mode - parallel clean architecture for game engine development
// Unlike the Tech Demo mode which uses scattered boolean flags and manual pass
// sequencing, the Engine Editor mode provides:
// - Centralized state management (EditorState struct)
// - Selective renderer usage (uses renderer as library)
// - Data-driven configuration (future: JSON config files)
// - Clean separation between edit mode and play mode
class EngineEditorMode {
public:
    EngineEditorMode();
    ~EngineEditorMode();

    EngineEditorMode(const EngineEditorMode&) = delete;
    EngineEditorMode& operator=(const EngineEditorMode&) = delete;

    // Lifecycle
    Result<void> Initialize(Engine* engine, Graphics::Renderer* renderer, Scene::ECS_Registry* registry);
    void Shutdown();

    // Frame update
    void Update(float deltaTime);
    void Render();
    void ProcessInput(const SDL_Event& event);

    // State queries
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    // Editor state accessors
    [[nodiscard]] bool IsGridVisible() const { return m_state.showGrid; }
    [[nodiscard]] bool IsGizmosVisible() const { return m_state.showGizmos; }
    [[nodiscard]] bool IsWireframeMode() const { return m_state.wireframeMode; }
    [[nodiscard]] float GetTimeOfDay() const { return m_state.timeOfDay; }

    // Editor state modifiers
    void SetGridVisible(bool visible) { m_state.showGrid = visible; }
    void SetGizmosVisible(bool visible) { m_state.showGizmos = visible; }
    void SetWireframeMode(bool enabled) { m_state.wireframeMode = enabled; }
    void SetTimeOfDay(float hour);
    void AdvanceTimeOfDay(float hours);

    // Camera control
    [[nodiscard]] glm::vec3 GetCameraPosition() const { return m_cameraPosition; }
    [[nodiscard]] float GetCameraYaw() const { return m_cameraYaw; }
    [[nodiscard]] float GetCameraPitch() const { return m_cameraPitch; }

    // Terrain access
    [[nodiscard]] EditorWorld* GetWorld() { return m_world.get(); }
    [[nodiscard]] const EditorWorld* GetWorld() const { return m_world.get(); }
    [[nodiscard]] float GetTerrainHeight(float worldX, float worldZ) const;

private:
    // Core references (not owned)
    Engine* m_engine = nullptr;
    Graphics::Renderer* m_renderer = nullptr;  // Used as library
    Scene::ECS_Registry* m_registry = nullptr;

    bool m_initialized = false;

    // EditorWorld manages terrain chunks and entities
    std::unique_ptr<EditorWorld> m_world;

    // Centralized editor state - replaces scattered boolean flags
    struct EditorState {
        // Visual toggles
        bool showGrid = true;
        bool showGizmos = true;
        bool wireframeMode = false;

        // World state
        float timeOfDay = 10.0f;       // 0-24 hours (default: 10am)
        float timeScale = 60.0f;       // 1 real second = 1 game minute
        bool timePaused = true;        // Paused by default in editor

        // Rendering options
        bool proceduralSky = true;
        bool shadows = true;
        bool ssao = false;             // Off by default for performance

        // Debug options
        bool showStats = true;
        bool showChunkBounds = false;
    } m_state;

    // Camera state (separate from terrain player camera)
    glm::vec3 m_cameraPosition{0.0f, 50.0f, 0.0f};
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = -0.3f;       // Slight downward angle
    float m_cameraSpeed = 20.0f;
    float m_mouseSensitivity = 0.003f;

    // Input state
    bool m_cameraControlActive = false;
    float m_pendingMouseDeltaX = 0.0f;
    float m_pendingMouseDeltaY = 0.0f;

    // Internal methods
    void UpdateCamera(float deltaTime);
    void UpdateTimeOfDay(float deltaTime);
    void RenderTerrain();
    void RenderSky();
    void RenderDebugOverlays();

    // Debug visualization (uses Renderer's AddDebugLine)
    void RenderDebugGrid();
    void RenderChunkBounds();
    void RenderOriginAxes();
    void RenderStats();

    // Sun direction calculation from time of day
    glm::vec3 CalculateSunDirection() const;
    glm::vec3 CalculateSunColor() const;
    float CalculateSunIntensity() const;
};

} // namespace Cortex
