// EditorCamera.cpp
// Implementation of editor camera controller with multiple modes.

#include "EditorCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Cortex {

EditorCamera::EditorCamera() = default;

void EditorCamera::SetNearFar(float nearPlane, float farPlane) {
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;
}

void EditorCamera::SetYawPitch(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = std::clamp(pitch, -1.5f, 1.5f);  // ~86 degrees max
}

glm::vec3 EditorCamera::GetForward() const {
    return glm::vec3(
        std::cos(m_pitch) * std::sin(m_yaw),
        std::sin(m_pitch),
        std::cos(m_pitch) * std::cos(m_yaw)
    );
}

glm::vec3 EditorCamera::GetRight() const {
    return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 EditorCamera::GetUp() const {
    return glm::normalize(glm::cross(GetRight(), GetForward()));
}

void EditorCamera::SetMode(CameraMode mode) {
    if (m_mode == mode) return;

    CameraMode oldMode = m_mode;
    m_mode = mode;

    // Transition handling
    if (mode == CameraMode::Orbit && oldMode == CameraMode::Fly) {
        // When switching to orbit, set orbit target in front of camera
        m_orbitTarget = m_position + GetForward() * m_orbitDistance;
        m_orbitYaw = m_yaw;
        m_orbitPitch = m_pitch;
    }
}

void EditorCamera::SetOrbitTarget(const glm::vec3& target) {
    m_orbitTarget = target;
    // Recalculate orbit angles to maintain current view
    glm::vec3 toCamera = m_position - target;
    m_orbitDistance = glm::length(toCamera);
    if (m_orbitDistance > 0.01f) {
        toCamera = glm::normalize(toCamera);
        m_orbitYaw = std::atan2(toCamera.x, toCamera.z);
        m_orbitPitch = std::asin(std::clamp(toCamera.y, -1.0f, 1.0f));
    }
}

void EditorCamera::FocusOn(const glm::vec3& target, float transitionTime) {
    m_focusActive = true;
    m_focusTarget = target;
    m_focusStartPos = m_position;
    m_focusStartYaw = m_yaw;
    m_focusStartPitch = m_pitch;
    m_focusTransitionTime = transitionTime;
    m_focusElapsed = 0.0f;
}

void EditorCamera::ProcessMouseMove(float deltaX, float deltaY) {
    m_pendingMouseX += deltaX;
    m_pendingMouseY += deltaY;
}

void EditorCamera::ProcessMouseScroll(float deltaY) {
    m_pendingScroll += deltaY;
}

void EditorCamera::SetMovementInput(bool forward, bool back, bool left, bool right,
                                     bool up, bool down, bool sprint) {
    m_moveForward = forward;
    m_moveBack = back;
    m_moveLeft = left;
    m_moveRight = right;
    m_moveUp = up;
    m_moveDown = down;
    m_sprinting = sprint;
}

void EditorCamera::Update(float deltaTime) {
    // Handle focus transition first (overrides other modes temporarily)
    if (m_focusActive) {
        UpdateFocusTransition(deltaTime);
        return;
    }

    switch (m_mode) {
        case CameraMode::Fly:
            UpdateFlyMode(deltaTime);
            break;
        case CameraMode::Orbit:
            UpdateOrbitMode(deltaTime);
            break;
        case CameraMode::Focus:
            // Focus mode without active transition - just stay put
            break;
    }

    // Clear pending input
    m_pendingMouseX = 0.0f;
    m_pendingMouseY = 0.0f;
    m_pendingScroll = 0.0f;
}

void EditorCamera::UpdateFlyMode(float deltaTime) {
    // Apply mouse look
    // Positive mouseX (move right) should increase yaw (turn right)
    // With GetForward() = (cos(pitch)*sin(yaw), ...), increasing yaw rotates towards +X
    m_yaw += m_pendingMouseX * m_mouseSensitivity;
    // Positive mouseY (move down) should decrease pitch (look down)
    // With GetForward() Y = sin(pitch), decreasing pitch looks down (natural mouse)
    m_pitch -= m_pendingMouseY * m_mouseSensitivity;
    m_pitch = std::clamp(m_pitch, -1.5f, 1.5f);

    // Calculate movement
    float speed = m_flySpeed * (m_sprinting ? m_sprintMultiplier : 1.0f);
    glm::vec3 velocity{0.0f};

    glm::vec3 forward = GetForward();
    glm::vec3 right = GetRight();

    // Use horizontal-only forward/back when terrain callback exists
    // This prevents diving into terrain when looking down
    glm::vec3 moveForward = forward;
    if (m_heightFunc) {
        glm::vec3 horizontalForward = glm::vec3(forward.x, 0.0f, forward.z);
        float horizontalLen = glm::length(horizontalForward);
        if (horizontalLen > 0.001f) {
            moveForward = horizontalForward / horizontalLen;
        } else {
            // Looking straight up/down - use yaw-based forward
            moveForward = glm::vec3(std::sin(m_yaw), 0.0f, std::cos(m_yaw));
        }
    }

    if (m_moveForward) velocity += moveForward;
    if (m_moveBack) velocity -= moveForward;
    if (m_moveRight) velocity += right;
    if (m_moveLeft) velocity -= right;
    if (m_moveUp) velocity.y += 1.0f;
    if (m_moveDown) velocity.y -= 1.0f;

    if (glm::length(velocity) > 0.01f) {
        velocity = glm::normalize(velocity) * speed * deltaTime;
        m_position += velocity;
    }

    // Clamp to terrain if callback provided
    ClampToTerrain();
}

void EditorCamera::UpdateOrbitMode(float deltaTime) {
    // Apply mouse to orbit angles (same convention as fly mode)
    m_orbitYaw += m_pendingMouseX * m_mouseSensitivity;
    m_orbitPitch -= m_pendingMouseY * m_mouseSensitivity;
    m_orbitPitch = std::clamp(m_orbitPitch, -1.4f, 1.4f);

    // Apply scroll to distance
    m_orbitDistance -= m_pendingScroll * m_orbitDistance * 0.1f;
    m_orbitDistance = std::clamp(m_orbitDistance, 5.0f, 500.0f);

    // Pan with movement keys
    if (m_moveForward || m_moveBack || m_moveLeft || m_moveRight) {
        float panSpeed = m_flySpeed * 0.5f * deltaTime;
        glm::vec3 panDir{0.0f};

        // Get horizontal orbit directions
        glm::vec3 orbitForward = glm::vec3(
            std::sin(m_orbitYaw),
            0.0f,
            std::cos(m_orbitYaw)
        );
        glm::vec3 orbitRight = glm::vec3(orbitForward.z, 0.0f, -orbitForward.x);

        if (m_moveForward) panDir += orbitForward;
        if (m_moveBack) panDir -= orbitForward;
        if (m_moveRight) panDir += orbitRight;
        if (m_moveLeft) panDir -= orbitRight;

        if (glm::length(panDir) > 0.01f) {
            m_orbitTarget += glm::normalize(panDir) * panSpeed;
        }
    }

    // Vertical movement adjusts target height
    if (m_moveUp) m_orbitTarget.y += m_flySpeed * 0.5f * deltaTime;
    if (m_moveDown) m_orbitTarget.y -= m_flySpeed * 0.5f * deltaTime;

    // Calculate camera position from orbit parameters
    m_position = m_orbitTarget + glm::vec3(
        m_orbitDistance * std::cos(m_orbitPitch) * std::sin(m_orbitYaw),
        m_orbitDistance * std::sin(m_orbitPitch),
        m_orbitDistance * std::cos(m_orbitPitch) * std::cos(m_orbitYaw)
    );

    // Update yaw/pitch to look at target
    glm::vec3 toTarget = glm::normalize(m_orbitTarget - m_position);
    m_yaw = std::atan2(toTarget.x, toTarget.z);
    m_pitch = std::asin(std::clamp(toTarget.y, -1.0f, 1.0f));

    ClampToTerrain();
}

void EditorCamera::UpdateFocusTransition(float deltaTime) {
    m_focusElapsed += deltaTime;
    float t = std::min(m_focusElapsed / m_focusTransitionTime, 1.0f);
    float smoothT = SmoothStep(t);

    // Calculate target look direction
    glm::vec3 toTarget = m_focusTarget - m_focusStartPos;
    float targetDistance = glm::length(toTarget);

    // Target position: move closer to focus point
    glm::vec3 targetPos = m_focusTarget - glm::normalize(toTarget) * std::min(targetDistance * 0.5f, 20.0f);

    // Target orientation
    glm::vec3 finalDir = glm::normalize(m_focusTarget - targetPos);
    float targetYaw = std::atan2(finalDir.x, finalDir.z);
    float targetPitch = std::asin(std::clamp(finalDir.y, -1.0f, 1.0f));

    // Interpolate
    m_position = glm::mix(m_focusStartPos, targetPos, smoothT);
    m_yaw = glm::mix(m_focusStartYaw, targetYaw, smoothT);
    m_pitch = glm::mix(m_focusStartPitch, targetPitch, smoothT);

    // Complete transition
    if (t >= 1.0f) {
        m_focusActive = false;
        // Switch to orbit mode centered on target
        m_mode = CameraMode::Orbit;
        SetOrbitTarget(m_focusTarget);
    }
}

void EditorCamera::ClampToTerrain() {
    if (!m_heightFunc) return;

    float terrainHeight = m_heightFunc(m_position.x, m_position.z, m_heightUserData);
    float minHeight = terrainHeight + m_minHeightAboveTerrain;

    // Hard clamp - never allow camera below minimum height
    if (m_position.y < minHeight) {
        m_position.y = minHeight;
    }
}

glm::mat4 EditorCamera::GetViewMatrix() const {
    return glm::lookAt(m_position, m_position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 EditorCamera::GetProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(m_fov), aspectRatio, m_nearPlane, m_farPlane);
}

glm::mat4 EditorCamera::GetViewProjectionMatrix(float aspectRatio) const {
    return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
}

void EditorCamera::SetTerrainHeightCallback(HeightFunc func, void* userData) {
    m_heightFunc = func;
    m_heightUserData = userData;
}

float EditorCamera::SmoothStep(float t) {
    // Hermite interpolation for smooth ease in/out
    return t * t * (3.0f - 2.0f * t);
}

} // namespace Cortex
