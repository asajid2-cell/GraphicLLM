#pragma once

// EditorCamera.h
// Editor camera controller with multiple modes (fly, orbit, focus).
// Provides smooth camera transitions and terrain-aware movement.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Cortex {

// Camera control modes
enum class CameraMode {
    Fly,      // Free-flying WASD + mouse look (default for terrain)
    Orbit,    // Orbit around a focus point
    Focus     // Smoothly transition to focus on a target
};

// EditorCamera - advanced camera controller for Engine Editor
class EditorCamera {
public:
    EditorCamera();
    ~EditorCamera() = default;

    // Configuration
    void SetFlySpeed(float speed) { m_flySpeed = speed; }
    void SetSprintMultiplier(float mult) { m_sprintMultiplier = mult; }
    void SetMouseSensitivity(float sens) { m_mouseSensitivity = sens; }
    void SetFOV(float fov) { m_fov = fov; }
    void SetNearFar(float nearPlane, float farPlane);

    [[nodiscard]] float GetFlySpeed() const { return m_flySpeed; }
    [[nodiscard]] float GetFOV() const { return m_fov; }

    // Position and orientation
    void SetPosition(const glm::vec3& pos) { m_position = pos; }
    void SetYawPitch(float yaw, float pitch);

    [[nodiscard]] glm::vec3 GetPosition() const { return m_position; }
    [[nodiscard]] float GetYaw() const { return m_yaw; }
    [[nodiscard]] float GetPitch() const { return m_pitch; }
    [[nodiscard]] glm::vec3 GetForward() const;
    [[nodiscard]] glm::vec3 GetRight() const;
    [[nodiscard]] glm::vec3 GetUp() const;

    // Mode switching
    void SetMode(CameraMode mode);
    [[nodiscard]] CameraMode GetMode() const { return m_mode; }

    // Orbit mode configuration
    void SetOrbitTarget(const glm::vec3& target);
    void SetOrbitDistance(float distance) { m_orbitDistance = distance; }
    [[nodiscard]] glm::vec3 GetOrbitTarget() const { return m_orbitTarget; }

    // Focus mode - smooth transition to look at target
    void FocusOn(const glm::vec3& target, float transitionTime = 0.5f);
    [[nodiscard]] bool IsFocusing() const { return m_focusActive; }

    // Input handling
    void ProcessMouseMove(float deltaX, float deltaY);
    void ProcessMouseScroll(float deltaY);

    // Movement input state (set each frame based on key state)
    void SetMovementInput(bool forward, bool back, bool left, bool right,
                          bool up, bool down, bool sprint);

    // Frame update
    void Update(float deltaTime);

    // Matrix getters
    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspectRatio) const;
    [[nodiscard]] glm::mat4 GetViewProjectionMatrix(float aspectRatio) const;

    // Terrain awareness (optional height callback)
    using HeightFunc = float(*)(float x, float z, void* userData);
    void SetTerrainHeightCallback(HeightFunc func, void* userData);
    void SetMinHeightAboveTerrain(float height) { m_minHeightAboveTerrain = height; }

private:
    // Camera state
    glm::vec3 m_position{0.0f, 50.0f, 0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;  // Radians, clamped to prevent flip

    CameraMode m_mode = CameraMode::Fly;

    // Movement settings
    float m_flySpeed = 20.0f;
    float m_sprintMultiplier = 3.0f;
    float m_mouseSensitivity = 0.003f;

    // Projection settings
    float m_fov = 60.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 2000.0f;

    // Orbit mode state
    glm::vec3 m_orbitTarget{0.0f};
    float m_orbitDistance = 50.0f;
    float m_orbitYaw = 0.0f;
    float m_orbitPitch = -0.3f;

    // Focus transition state
    bool m_focusActive = false;
    glm::vec3 m_focusTarget{0.0f};
    glm::vec3 m_focusStartPos{0.0f};
    float m_focusStartYaw = 0.0f;
    float m_focusStartPitch = 0.0f;
    float m_focusTransitionTime = 0.5f;
    float m_focusElapsed = 0.0f;

    // Movement input state
    bool m_moveForward = false;
    bool m_moveBack = false;
    bool m_moveLeft = false;
    bool m_moveRight = false;
    bool m_moveUp = false;
    bool m_moveDown = false;
    bool m_sprinting = false;

    // Pending mouse input (accumulated between updates)
    float m_pendingMouseX = 0.0f;
    float m_pendingMouseY = 0.0f;
    float m_pendingScroll = 0.0f;

    // Terrain height callback
    HeightFunc m_heightFunc = nullptr;
    void* m_heightUserData = nullptr;
    float m_minHeightAboveTerrain = 2.0f;

    // Internal update methods
    void UpdateFlyMode(float deltaTime);
    void UpdateOrbitMode(float deltaTime);
    void UpdateFocusTransition(float deltaTime);
    void ClampToTerrain();

    // Smooth interpolation helper
    static float SmoothStep(float t);
};

} // namespace Cortex
