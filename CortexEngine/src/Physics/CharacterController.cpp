// CharacterController.cpp
// Physics-based character controller implementation.

#include "CharacterController.h"
#include <cmath>
#include <algorithm>

namespace Cortex::Physics {

CharacterController::CharacterController()
    : m_position(0)
    , m_velocity(0)
    , m_currentHeight(1.8f)
    , m_waterLevel(-1000.0f)
    , m_footstepAccumulator(0)
    , m_wasGrounded(false) {

    m_output.position = glm::vec3(0);
    m_output.velocity = glm::vec3(0);
    m_output.groundNormal = glm::vec3(0, 1, 0);
    m_output.state = CharacterState::Airborne;
    m_output.currentHeight = m_config.height;
    m_output.isGrounded = false;
    m_output.isSliding = false;
    m_output.isCrouching = false;
    m_output.canStandUp = true;
    m_output.slopeAngle = 0;
    m_output.waterDepth = 0;
}

CharacterController::~CharacterController() = default;

void CharacterController::Initialize(std::shared_ptr<TerrainCollider> terrain) {
    m_terrain = std::move(terrain);
}

void CharacterController::SetConfig(const CharacterConfig& config) {
    m_config = config;
    m_currentHeight = config.height;
}

void CharacterController::Update(float deltaTime, const CharacterInput& input) {
    if (!m_terrain) return;

    // Store previous grounded state
    m_wasGrounded = m_output.isGrounded;

    // Handle crouch state
    UpdateCrouch(input.wantCrouch, deltaTime);

    // Check water depth
    m_output.waterDepth = std::max(0.0f, m_waterLevel - m_position.y);
    bool inWater = m_output.waterDepth > m_currentHeight * 0.7f;

    // Update state based on conditions
    if (inWater) {
        if (m_output.state != CharacterState::Swimming) {
            TransitionToSwimming();
        }
    } else {
        UpdateState();
    }

    // Process movement based on state
    switch (m_output.state) {
        case CharacterState::Grounded:
            ProcessGrounded(deltaTime, input);
            break;
        case CharacterState::Airborne:
            ProcessAirborne(deltaTime, input);
            break;
        case CharacterState::Sliding:
            ProcessSliding(deltaTime, input);
            break;
        case CharacterState::Swimming:
            ProcessSwimming(deltaTime, input);
            break;
        default:
            break;
    }

    // Clamp velocity
    ClampVelocity();

    // Update output
    m_output.position = m_position;
    m_output.velocity = m_velocity;
    m_output.currentHeight = m_currentHeight;

    // Process footsteps
    if (m_output.isGrounded && !m_output.isSliding) {
        float horizontalSpeed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
        ProcessFootsteps(deltaTime, horizontalSpeed);
    }
}

glm::vec3 CharacterController::GetEyePosition() const {
    float eyeOffset = m_output.isCrouching ?
                      m_config.crouchHeight - 0.2f :
                      m_config.eyeHeight;
    return m_position + glm::vec3(0, eyeOffset, 0);
}

void CharacterController::SetPosition(const glm::vec3& position) {
    m_position = position;
    m_output.position = position;
}

void CharacterController::Teleport(const glm::vec3& position) {
    m_position = position;
    m_velocity = glm::vec3(0);
    m_output.position = position;
    m_output.velocity = glm::vec3(0);
    UpdateState();
}

void CharacterController::AddForce(const glm::vec3& force) {
    // F = ma, so a = F/m
    m_velocity += force / m_config.mass;
}

void CharacterController::AddImpulse(const glm::vec3& impulse) {
    // Impulse = m * delta_v, so delta_v = impulse / m
    m_velocity += impulse / m_config.mass;
}

void CharacterController::SetFootstepCallback(FootstepCallback callback) {
    m_footstepCallback = std::move(callback);
}

// ============================================================================
// Movement Processing
// ============================================================================

void CharacterController::ProcessGrounded(float deltaTime, const CharacterInput& input) {
    // Get movement direction
    glm::vec3 moveDir = GetMoveDirection(input);
    float targetSpeed = GetCurrentSpeed(input);

    // Handle jump
    if (input.wantJump) {
        m_velocity.y = m_config.jumpForce;
        TransitionToAirborne();
        return;
    }

    // Ground movement
    glm::vec3 targetVelocity = moveDir * targetSpeed;

    // Apply friction/acceleration
    if (glm::length(moveDir) > 0.1f) {
        // Accelerate toward target
        float accel = m_config.groundFriction * 2.0f;
        m_velocity.x = glm::mix(m_velocity.x, targetVelocity.x, accel * deltaTime);
        m_velocity.z = glm::mix(m_velocity.z, targetVelocity.z, accel * deltaTime);
    } else {
        // Apply friction
        ApplyFriction(m_config.groundFriction, deltaTime);
    }

    // Keep grounded (cancel vertical velocity)
    m_velocity.y = 0;

    // Move and collide
    glm::vec3 movement = m_velocity * deltaTime;

    // Project movement onto surface
    if (m_output.slopeAngle > 0.1f) {
        movement = m_terrain->ProjectOnSurface(m_position, movement);
    }

    MoveAndCollide(movement, deltaTime);

    // Snap to ground
    SnapToGround();

    // Re-check ground state
    GroundContact contact;
    if (!CheckGround(contact)) {
        TransitionToAirborne();
    } else if (contact.slope > m_config.maxWalkableSlope) {
        TransitionToSliding(contact);
    }
}

void CharacterController::ProcessAirborne(float deltaTime, const CharacterInput& input) {
    // Air control
    glm::vec3 moveDir = GetMoveDirection(input);

    if (glm::length(moveDir) > 0.1f) {
        glm::vec3 targetVelocity = moveDir * m_config.airSpeed;
        float accel = m_config.airAcceleration * deltaTime;

        m_velocity.x += (targetVelocity.x - m_velocity.x) * accel;
        m_velocity.z += (targetVelocity.z - m_velocity.z) * accel;
    }

    // Apply air friction
    ApplyFriction(m_config.airFriction, deltaTime);

    // Apply gravity
    ApplyGravity(deltaTime);

    // Move and collide
    glm::vec3 movement = m_velocity * deltaTime;
    MoveAndCollide(movement, deltaTime);

    // Check for landing
    GroundContact contact;
    if (CheckGround(contact)) {
        if (contact.slope <= m_config.maxWalkableSlope) {
            TransitionToGrounded(contact);
        } else {
            TransitionToSliding(contact);
        }
    }
}

void CharacterController::ProcessSliding(float deltaTime, const CharacterInput& input) {
    // Add slide velocity from slope
    glm::vec3 slideAccel = m_terrain->CalculateSlipVelocity(
        m_position, m_config.mass, m_output.surface.dynamicFriction);
    m_velocity += slideAccel * deltaTime;

    // Limited air control while sliding
    glm::vec3 moveDir = GetMoveDirection(input);
    if (glm::length(moveDir) > 0.1f) {
        glm::vec3 targetVelocity = moveDir * m_config.airSpeed * 0.5f;
        float accel = m_config.airAcceleration * 0.5f * deltaTime;

        m_velocity.x += (targetVelocity.x - m_velocity.x) * accel;
        m_velocity.z += (targetVelocity.z - m_velocity.z) * accel;
    }

    // Apply slide friction
    ApplyFriction(m_config.slideFriction, deltaTime);

    // Move and collide
    glm::vec3 movement = m_velocity * deltaTime;
    MoveAndCollide(movement, deltaTime);

    // Check if still on slope
    GroundContact contact;
    if (!CheckGround(contact)) {
        TransitionToAirborne();
    } else if (contact.slope <= m_config.maxWalkableSlope) {
        TransitionToGrounded(contact);
    }
}

void CharacterController::ProcessSwimming(float deltaTime, const CharacterInput& input) {
    // 3D movement in water
    glm::vec3 moveDir = GetMoveDirection(input);
    float targetSpeed = m_config.swimSpeed;

    // Vertical movement
    if (input.wantJump) {
        moveDir.y = 1.0f;
    } else if (input.wantCrouch) {
        moveDir.y = -1.0f;
    }

    glm::vec3 targetVelocity = glm::normalize(moveDir + glm::vec3(0.001f)) * targetSpeed;

    // Water resistance
    float waterFriction = 4.0f;
    m_velocity = glm::mix(m_velocity, targetVelocity, waterFriction * deltaTime);

    // Buoyancy
    float buoyancy = 2.0f;
    float depthFactor = std::min(1.0f, m_output.waterDepth / m_currentHeight);
    m_velocity.y += buoyancy * depthFactor * deltaTime;

    // Move
    glm::vec3 movement = m_velocity * deltaTime;
    MoveAndCollide(movement, deltaTime);

    // Check if exiting water
    if (m_output.waterDepth < m_currentHeight * 0.5f) {
        GroundContact contact;
        if (CheckGround(contact)) {
            TransitionToGrounded(contact);
        } else {
            TransitionToAirborne();
        }
    }
}

// ============================================================================
// Collision
// ============================================================================

void CharacterController::MoveAndCollide(const glm::vec3& movement, float deltaTime) {
    glm::vec3 remaining = movement;
    glm::vec3 newPos = m_position;

    for (int i = 0; i < m_config.maxBounces && glm::length(remaining) > 0.001f; ++i) {
        SweepHit hit;
        glm::vec3 end = newPos + remaining;

        if (m_terrain->CapsuleSweep(newPos, end, m_config.radius, m_currentHeight, hit)) {
            // Move to hit point with skin width
            float safeDistance = std::max(0.0f, hit.distance - m_config.skinWidth);
            glm::vec3 moveNormalized = glm::normalize(remaining);
            newPos += moveNormalized * safeDistance;

            // Calculate remaining movement
            float remainingDist = glm::length(remaining) - safeDistance;
            if (remainingDist <= 0.001f) break;

            // Slide along surface
            glm::vec3 slideDir = remaining - hit.normal * glm::dot(remaining, hit.normal);
            remaining = glm::normalize(slideDir) * remainingDist;

            // Adjust velocity
            m_velocity = m_terrain->ResolveVelocity(
                newPos, m_velocity, m_config.radius,
                hit.surface.restitution, hit.surface.dynamicFriction);

            m_output.surface = hit.surface;
        } else {
            // No collision, move full distance
            newPos = end;
            break;
        }
    }

    // Depenetrate from terrain
    newPos = m_terrain->DepenetratePosition(newPos, m_config.radius);

    m_position = newPos;
}

bool CharacterController::CheckGround(GroundContact& outContact) {
    TerrainQueryParams params;
    params.maxSlope = m_config.slideSlope;
    params.stepHeight = m_config.stepHeight;
    params.skinWidth = m_config.skinWidth;

    outContact = m_terrain->GetGroundContact(m_position, m_config.radius, params);
    return outContact.isGrounded;
}

bool CharacterController::TryStep(const glm::vec3& movement, glm::vec3& outPosition) {
    // Try to step up over obstacle
    glm::vec3 stepUp = m_position + glm::vec3(0, m_config.stepHeight, 0);

    SweepHit upHit;
    if (m_terrain->CapsuleSweep(m_position, stepUp, m_config.radius, m_currentHeight, upHit)) {
        stepUp = m_position + glm::vec3(0, upHit.distance - m_config.skinWidth, 0);
    }

    // Try to move forward at stepped-up height
    glm::vec3 stepForward = stepUp + movement;
    SweepHit forwardHit;
    if (m_terrain->CapsuleSweep(stepUp, stepForward, m_config.radius, m_currentHeight, forwardHit)) {
        return false;  // Can't step over
    }

    // Step down to ground
    glm::vec3 stepDown = stepForward - glm::vec3(0, m_config.stepHeight + m_config.groundCheckDistance, 0);
    SweepHit downHit;
    if (m_terrain->CapsuleSweep(stepForward, stepDown, m_config.radius, m_currentHeight, downHit)) {
        outPosition = stepForward + glm::vec3(0, -downHit.distance + m_config.skinWidth, 0);
        return true;
    }

    return false;
}

void CharacterController::SnapToGround() {
    if (!m_output.isGrounded) return;

    float terrainHeight = m_terrain->SampleHeight(m_position.x, m_position.z);
    float targetY = terrainHeight + m_config.radius;

    if (std::abs(m_position.y - targetY) < m_config.stepHeight) {
        m_position.y = targetY;
    }
}

// ============================================================================
// State Transitions
// ============================================================================

void CharacterController::UpdateState() {
    GroundContact contact;
    bool onGround = CheckGround(contact);

    if (onGround) {
        if (contact.slope > m_config.slideSlope) {
            if (m_output.state != CharacterState::Sliding) {
                TransitionToSliding(contact);
            }
        } else if (contact.slope > m_config.maxWalkableSlope) {
            // On steep but not sliding slope
            if (m_output.state != CharacterState::Sliding) {
                TransitionToSliding(contact);
            }
        } else {
            if (m_output.state != CharacterState::Grounded) {
                TransitionToGrounded(contact);
            }
        }

        m_output.groundNormal = contact.normal;
        m_output.slopeAngle = contact.slope;
        m_output.surface = contact.surface;
    } else {
        if (m_output.state != CharacterState::Airborne) {
            TransitionToAirborne();
        }
    }
}

void CharacterController::TransitionToGrounded(const GroundContact& contact) {
    m_output.state = CharacterState::Grounded;
    m_output.isGrounded = true;
    m_output.isSliding = false;
    m_output.groundNormal = contact.normal;
    m_output.slopeAngle = contact.slope;
    m_output.surface = contact.surface;

    // Cancel vertical velocity on landing
    if (m_velocity.y < 0) {
        m_velocity.y = 0;
    }
}

void CharacterController::TransitionToAirborne() {
    m_output.state = CharacterState::Airborne;
    m_output.isGrounded = false;
    m_output.isSliding = false;
}

void CharacterController::TransitionToSliding(const GroundContact& contact) {
    m_output.state = CharacterState::Sliding;
    m_output.isGrounded = true;
    m_output.isSliding = true;
    m_output.groundNormal = contact.normal;
    m_output.slopeAngle = contact.slope;
    m_output.surface = contact.surface;
}

void CharacterController::TransitionToSwimming() {
    m_output.state = CharacterState::Swimming;
    m_output.isGrounded = false;
    m_output.isSliding = false;
}

// ============================================================================
// Crouch Handling
// ============================================================================

void CharacterController::UpdateCrouch(bool wantCrouch, float deltaTime) {
    float targetHeight = wantCrouch ? m_config.crouchHeight : m_config.height;
    float crouchSpeed = 5.0f;

    if (wantCrouch) {
        m_currentHeight = glm::mix(m_currentHeight, targetHeight, crouchSpeed * deltaTime);
        m_output.isCrouching = true;
    } else {
        if (CanStandUp()) {
            m_currentHeight = glm::mix(m_currentHeight, targetHeight, crouchSpeed * deltaTime);
            m_output.isCrouching = std::abs(m_currentHeight - m_config.height) > 0.1f;
        }
    }

    m_output.canStandUp = CanStandUp();
}

bool CharacterController::CanStandUp() const {
    if (!m_terrain) return true;

    // Check if there's space above to stand up
    glm::vec3 headPos = m_position + glm::vec3(0, m_config.height - m_config.radius, 0);
    RaycastHit hit;

    return !m_terrain->Raycast(m_position + glm::vec3(0, m_currentHeight, 0),
                                glm::vec3(0, 1, 0),
                                m_config.height - m_currentHeight, hit);
}

// ============================================================================
// Helpers
// ============================================================================

glm::vec3 CharacterController::GetMoveDirection(const CharacterInput& input) const {
    if (glm::length(input.moveDirection) < 0.01f) {
        return glm::vec3(0);
    }

    // Transform input by look direction
    float yaw = input.lookYaw;
    float cosYaw = std::cos(yaw);
    float sinYaw = std::sin(yaw);

    glm::vec3 forward(sinYaw, 0, cosYaw);
    glm::vec3 right(cosYaw, 0, -sinYaw);

    glm::vec3 moveDir = forward * input.moveDirection.y + right * input.moveDirection.x;
    float len = glm::length(moveDir);

    if (len > 0.01f) {
        return moveDir / len;
    }

    return glm::vec3(0);
}

float CharacterController::GetCurrentSpeed(const CharacterInput& input) const {
    if (m_output.isCrouching) {
        return m_config.crouchSpeed;
    } else if (input.wantRun) {
        return m_config.runSpeed;
    }
    return m_config.walkSpeed;
}

void CharacterController::ApplyGravity(float deltaTime) {
    m_velocity.y -= m_config.gravity * deltaTime;
}

void CharacterController::ApplyFriction(float friction, float deltaTime) {
    float speed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    if (speed < 0.01f) {
        m_velocity.x = 0;
        m_velocity.z = 0;
        return;
    }

    float drop = friction * deltaTime;
    float newSpeed = std::max(0.0f, speed - drop);
    float scale = newSpeed / speed;

    m_velocity.x *= scale;
    m_velocity.z *= scale;
}

void CharacterController::ClampVelocity() {
    // Clamp fall speed
    if (m_velocity.y < -m_config.terminalVelocity) {
        m_velocity.y = -m_config.terminalVelocity;
    }

    // Clamp horizontal speed (sanity check)
    float horizontalSpeed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    float maxHorizontal = m_config.runSpeed * 2.0f;
    if (horizontalSpeed > maxHorizontal) {
        float scale = maxHorizontal / horizontalSpeed;
        m_velocity.x *= scale;
        m_velocity.z *= scale;
    }
}

void CharacterController::ProcessFootsteps(float deltaTime, float speed) {
    if (!m_footstepCallback || speed < 0.5f) {
        m_footstepAccumulator = 0;
        return;
    }

    float stepInterval = 0.5f / speed;  // Faster movement = more frequent steps
    stepInterval = std::clamp(stepInterval, 0.25f, 0.6f);

    m_footstepAccumulator += deltaTime;

    if (m_footstepAccumulator >= stepInterval) {
        m_footstepAccumulator = 0;
        m_footstepCallback(m_position, m_output.surface, speed);
    }
}

} // namespace Cortex::Physics
