#pragma once

// CharacterController.h
// Physics-based character controller for terrain navigation.
// Handles ground detection, slope movement, stepping, and collision response.

#include "TerrainCollider.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <functional>

namespace Cortex::Physics {

// Character movement state
enum class CharacterState : uint8_t {
    Grounded = 0,       // On walkable ground
    Airborne = 1,       // In the air (jumping/falling)
    Sliding = 2,        // On steep slope, sliding down
    Swimming = 3,       // In water
    Climbing = 4        // On ladder/climbable surface
};

// Character controller configuration
struct CharacterConfig {
    // Dimensions
    float radius = 0.4f;            // Capsule radius
    float height = 1.8f;            // Total height standing
    float crouchHeight = 1.0f;      // Height when crouching
    float eyeHeight = 1.6f;         // Camera/eye offset from feet

    // Movement speeds
    float walkSpeed = 4.0f;         // Normal walk speed (m/s)
    float runSpeed = 8.0f;          // Running speed (m/s)
    float crouchSpeed = 2.0f;       // Crouch walk speed (m/s)
    float airSpeed = 2.0f;          // Air control speed (m/s)
    float swimSpeed = 3.0f;         // Swimming speed (m/s)

    // Physics
    float mass = 80.0f;             // Character mass (kg)
    float gravity = 20.0f;          // Gravity acceleration (m/s^2)
    float jumpForce = 8.0f;         // Jump velocity (m/s)
    float terminalVelocity = 50.0f; // Maximum fall speed (m/s)

    // Ground detection
    float groundCheckDistance = 0.2f;   // Distance to check for ground
    float stepHeight = 0.5f;            // Maximum step-up height
    float maxWalkableSlope = 45.0f;     // Maximum slope (degrees)
    float slideSlope = 60.0f;           // Slope where sliding begins

    // Air control
    float airAcceleration = 10.0f;  // Acceleration in air
    float airFriction = 0.1f;       // Air resistance

    // Ground friction
    float groundFriction = 8.0f;    // Deceleration on ground
    float slideFriction = 2.0f;     // Friction while sliding

    // Collision
    float skinWidth = 0.02f;        // Collision skin thickness
    int maxBounces = 4;             // Max collision iterations per frame
};

// Character input for movement
struct CharacterInput {
    glm::vec2 moveDirection;        // Normalized XZ input (-1 to 1)
    float lookYaw;                  // Camera yaw for movement direction
    bool wantJump;                  // Jump input
    bool wantRun;                   // Run modifier
    bool wantCrouch;                // Crouch modifier
    bool wantSwim;                  // Swimming input (for diving/surfacing)

    CharacterInput() : moveDirection(0), lookYaw(0),
                       wantJump(false), wantRun(false),
                       wantCrouch(false), wantSwim(false) {}
};

// Character controller output/state
struct CharacterOutput {
    glm::vec3 position;             // World position (feet)
    glm::vec3 velocity;             // Current velocity
    glm::vec3 groundNormal;         // Normal of ground surface
    CharacterState state;           // Current movement state
    TerrainSurfaceProperties surface; // Ground surface properties
    float currentHeight;            // Current capsule height
    bool isGrounded;                // On ground
    bool isSliding;                 // Sliding on steep slope
    bool isCrouching;               // In crouch state
    bool canStandUp;                // Space to stand from crouch
    float slopeAngle;               // Current slope angle (degrees)
    float waterDepth;               // Depth underwater (0 if not in water)
};

class CharacterController {
public:
    CharacterController();
    ~CharacterController();

    // Initialize with terrain collider
    void Initialize(std::shared_ptr<TerrainCollider> terrain);

    // Configuration
    void SetConfig(const CharacterConfig& config);
    const CharacterConfig& GetConfig() const { return m_config; }

    // Movement
    void Update(float deltaTime, const CharacterInput& input);

    // State queries
    const CharacterOutput& GetOutput() const { return m_output; }
    glm::vec3 GetPosition() const { return m_output.position; }
    glm::vec3 GetVelocity() const { return m_output.velocity; }
    glm::vec3 GetEyePosition() const;
    bool IsGrounded() const { return m_output.isGrounded; }
    CharacterState GetState() const { return m_output.state; }

    // Position control
    void SetPosition(const glm::vec3& position);
    void Teleport(const glm::vec3& position);  // Resets velocity too
    void AddForce(const glm::vec3& force);     // Apply external force
    void AddImpulse(const glm::vec3& impulse); // Apply instant velocity change

    // Footstep callback for audio
    using FootstepCallback = std::function<void(const glm::vec3& position,
                                                 const TerrainSurfaceProperties& surface,
                                                 float speed)>;
    void SetFootstepCallback(FootstepCallback callback);

    // Water level (set by game for swimming detection)
    void SetWaterLevel(float level) { m_waterLevel = level; }
    float GetWaterLevel() const { return m_waterLevel; }

private:
    std::shared_ptr<TerrainCollider> m_terrain;
    CharacterConfig m_config;
    CharacterOutput m_output;

    // Internal state
    glm::vec3 m_position;
    glm::vec3 m_velocity;
    float m_currentHeight;
    float m_waterLevel;
    float m_footstepAccumulator;
    bool m_wasGrounded;

    FootstepCallback m_footstepCallback;

    // Movement processing
    void ProcessGrounded(float deltaTime, const CharacterInput& input);
    void ProcessAirborne(float deltaTime, const CharacterInput& input);
    void ProcessSliding(float deltaTime, const CharacterInput& input);
    void ProcessSwimming(float deltaTime, const CharacterInput& input);

    // Collision
    void MoveAndCollide(const glm::vec3& movement, float deltaTime);
    bool CheckGround(GroundContact& outContact);
    bool TryStep(const glm::vec3& movement, glm::vec3& outPosition);
    void SnapToGround();

    // State transitions
    void UpdateState();
    void TransitionToGrounded(const GroundContact& contact);
    void TransitionToAirborne();
    void TransitionToSliding(const GroundContact& contact);
    void TransitionToSwimming();

    // Crouch handling
    void UpdateCrouch(bool wantCrouch, float deltaTime);
    bool CanStandUp() const;

    // Helper functions
    glm::vec3 GetMoveDirection(const CharacterInput& input) const;
    float GetCurrentSpeed(const CharacterInput& input) const;
    void ApplyGravity(float deltaTime);
    void ApplyFriction(float friction, float deltaTime);
    void ClampVelocity();

    // Footstep processing
    void ProcessFootsteps(float deltaTime, float speed);
};

// Character component for ECS integration
struct CharacterControllerComponent {
    CharacterConfig config;
    CharacterInput input;
    CharacterOutput output;
    std::shared_ptr<CharacterController> controller;
    bool enabled = true;

    void Initialize(std::shared_ptr<TerrainCollider> terrain) {
        controller = std::make_shared<CharacterController>();
        controller->Initialize(terrain);
        controller->SetConfig(config);
    }

    void Update(float deltaTime) {
        if (enabled && controller) {
            controller->Update(deltaTime, input);
            output = controller->GetOutput();
        }
    }
};

} // namespace Cortex::Physics
