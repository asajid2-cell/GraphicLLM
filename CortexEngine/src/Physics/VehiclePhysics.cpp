// VehiclePhysics.cpp
// Arcade-style vehicle physics implementation.

#include "VehiclePhysics.h"
#include <cmath>
#include <algorithm>

namespace Cortex::Physics {

// ============================================================================
// Default Configurations
// ============================================================================

VehicleConfig VehiclePhysics::CreateDefaultCar() {
    VehicleConfig config;
    config.mass = 1400.0f;
    config.dimensions = glm::vec3(1.8f, 1.4f, 4.5f);
    config.centerOfMass = glm::vec3(0, 0.3f, 0);
    config.driveType = DriveType::RearWheelDrive;

    // Front wheels
    WheelConfig frontLeft, frontRight;
    frontLeft.localPosition = glm::vec3(-0.8f, 0.0f, 1.3f);
    frontRight.localPosition = glm::vec3(0.8f, 0.0f, 1.3f);
    frontLeft.isSteerable = frontRight.isSteerable = true;
    frontLeft.isDriven = frontRight.isDriven = false;

    // Rear wheels
    WheelConfig rearLeft, rearRight;
    rearLeft.localPosition = glm::vec3(-0.8f, 0.0f, -1.3f);
    rearRight.localPosition = glm::vec3(0.8f, 0.0f, -1.3f);
    rearLeft.isDriven = rearRight.isDriven = true;

    config.wheels = {frontLeft, frontRight, rearLeft, rearRight};

    return config;
}

VehicleConfig VehiclePhysics::CreateDefaultTruck() {
    VehicleConfig config = CreateDefaultCar();
    config.mass = 3500.0f;
    config.dimensions = glm::vec3(2.2f, 2.5f, 6.0f);
    config.maxEngineTorque = 800.0f;
    config.maxBrakeTorque = 8000.0f;

    // Adjust wheel positions
    for (auto& wheel : config.wheels) {
        wheel.radius = 0.5f;
        wheel.suspensionLength = 0.4f;
        wheel.suspensionStiffness = 80000.0f;
    }

    return config;
}

VehicleConfig VehiclePhysics::CreateDefaultOffroad() {
    VehicleConfig config = CreateDefaultCar();
    config.mass = 2000.0f;
    config.dimensions = glm::vec3(2.0f, 2.0f, 4.8f);
    config.driveType = DriveType::AllWheelDrive;
    config.maxEngineTorque = 500.0f;

    // All wheels driven
    for (auto& wheel : config.wheels) {
        wheel.radius = 0.45f;
        wheel.suspensionLength = 0.5f;
        wheel.isDriven = true;
        wheel.lateralFriction = 1.2f;  // Better off-road grip
    }

    return config;
}

// ============================================================================
// VehiclePhysics Implementation
// ============================================================================

VehiclePhysics::VehiclePhysics()
    : m_position(0)
    , m_orientation(1, 0, 0, 0)
    , m_velocity(0)
    , m_angularVelocity(0)
    , m_engineRPM(800.0f)
    , m_currentGear(0)
    , m_clutchEngagement(1.0f)
    , m_currentSteerAngle(0)
    , m_airTime(0)
    , m_wasAirborne(false) {
}

VehiclePhysics::~VehiclePhysics() = default;

void VehiclePhysics::Initialize(std::shared_ptr<TerrainCollider> terrain) {
    m_terrain = std::move(terrain);
}

void VehiclePhysics::SetConfig(const VehicleConfig& config) {
    m_config = config;
    InitializeWheels();
}

void VehiclePhysics::InitializeWheels() {
    m_wheelStates.resize(m_config.wheels.size());

    for (size_t i = 0; i < m_wheelStates.size(); ++i) {
        m_wheelStates[i] = WheelState();
        m_wheelStates[i].worldPosition = GetWheelWorldPosition(static_cast<int>(i));
    }
}

void VehiclePhysics::Update(float deltaTime, const VehicleInput& input) {
    if (!m_terrain) return;

    // Clamp delta time for stability
    deltaTime = std::min(deltaTime, 0.02f);

    // Update vehicle systems
    UpdateSteering(deltaTime, input);
    UpdateTransmission(deltaTime, input);
    UpdateWheels(deltaTime, input);
    UpdateSuspension(deltaTime);
    ApplyTireForces(deltaTime);
    ApplyAerodynamics(deltaTime);
    IntegrateState(deltaTime);
    UpdateOutput();

    // Audio callbacks
    if (m_engineAudioCallback) {
        float load = std::abs(input.throttle - input.brake);
        m_engineAudioCallback(m_engineRPM, input.throttle, load);
    }

    if (m_tireAudioCallback) {
        for (size_t i = 0; i < m_wheelStates.size(); ++i) {
            const auto& ws = m_wheelStates[i];
            float totalSlip = std::sqrt(ws.slipRatio * ws.slipRatio +
                                        ws.slipAngle * ws.slipAngle);
            m_tireAudioCallback(static_cast<int>(i), totalSlip, ws.surface);
        }
    }
}

void VehiclePhysics::UpdateSteering(float deltaTime, const VehicleInput& input) {
    float targetAngle = glm::radians(m_config.maxSteerAngle) * input.steer;
    float diff = targetAngle - m_currentSteerAngle;

    if (std::abs(input.steer) < 0.1f) {
        // Return to center
        float returnRate = m_config.steerReturnSpeed * deltaTime;
        if (std::abs(m_currentSteerAngle) < returnRate) {
            m_currentSteerAngle = 0;
        } else {
            m_currentSteerAngle -= glm::sign(m_currentSteerAngle) * returnRate;
        }
    } else {
        // Steer toward target
        float steerRate = m_config.steerSpeed * deltaTime;
        if (std::abs(diff) < steerRate) {
            m_currentSteerAngle = targetAngle;
        } else {
            m_currentSteerAngle += glm::sign(diff) * steerRate;
        }
    }

    // Apply to steerable wheels
    for (size_t i = 0; i < m_wheelStates.size(); ++i) {
        if (m_config.wheels[i].isSteerable) {
            m_wheelStates[i].steerAngle = m_currentSteerAngle;
        }
    }
}

void VehiclePhysics::UpdateTransmission(float deltaTime, const VehicleInput& input) {
    // Calculate wheel RPM from velocity
    glm::vec3 localVelocity = glm::inverse(m_orientation) * m_velocity;
    float forwardSpeed = localVelocity.z;

    // Find average driven wheel radius
    float avgRadius = 0.4f;
    int drivenCount = 0;
    for (size_t i = 0; i < m_config.wheels.size(); ++i) {
        if (m_config.wheels[i].isDriven) {
            avgRadius += m_config.wheels[i].radius;
            drivenCount++;
        }
    }
    if (drivenCount > 0) avgRadius /= drivenCount;

    float wheelRPM = (forwardSpeed / (2.0f * 3.14159f * avgRadius)) * 60.0f;

    // Auto transmission
    int gearCount = static_cast<int>(m_config.gearRatios.size()) - 2;  // Exclude R and N

    if (input.shiftUp && m_currentGear < gearCount) {
        m_currentGear++;
    } else if (input.shiftDown && m_currentGear > -1) {
        m_currentGear--;
    }

    // Auto gear selection (if no manual input)
    if (!input.shiftUp && !input.shiftDown && m_currentGear > 0) {
        float currentRatio = m_config.gearRatios[m_currentGear + 1];
        float engineRPM = std::abs(wheelRPM * currentRatio * m_config.finalDriveRatio);

        // Upshift
        if (engineRPM > m_config.maxEngineRPM * 0.9f && m_currentGear < gearCount) {
            m_currentGear++;
        }
        // Downshift
        else if (engineRPM < m_config.maxEngineRPM * 0.3f && m_currentGear > 1 && input.throttle > 0.5f) {
            m_currentGear--;
        }
    }

    // Calculate engine RPM
    if (m_currentGear == 0) {
        // Neutral
        m_engineRPM = m_config.idleRPM + (m_config.maxEngineRPM - m_config.idleRPM) * input.throttle * 0.5f;
    } else {
        float gearRatio = m_config.gearRatios[m_currentGear + 1];
        m_engineRPM = std::abs(wheelRPM * gearRatio * m_config.finalDriveRatio);
        m_engineRPM = std::clamp(m_engineRPM, m_config.idleRPM, m_config.maxEngineRPM);
    }
}

void VehiclePhysics::UpdateWheels(float deltaTime, const VehicleInput& input) {
    for (size_t i = 0; i < m_wheelStates.size(); ++i) {
        auto& ws = m_wheelStates[i];
        const auto& wc = m_config.wheels[i];

        ws.worldPosition = GetWheelWorldPosition(static_cast<int>(i));

        // Raycast for ground contact
        glm::vec3 down = m_orientation * glm::vec3(0, -1, 0);
        float rayLength = wc.suspensionLength + wc.radius;

        auto contact = m_terrain->GetWheelContact(ws.worldPosition, down, rayLength, wc.radius);

        ws.isGrounded = contact.isContact;
        if (contact.isContact) {
            ws.contactPoint = contact.contactPoint;
            ws.contactNormal = contact.normal;
            ws.surface = contact.surface;
            ws.grip = contact.grip * wc.lateralFriction;
            ws.suspensionCompression = 1.0f - (contact.suspensionLength / wc.suspensionLength);
            ws.suspensionCompression = std::clamp(ws.suspensionCompression, 0.0f, 1.0f);
        } else {
            ws.suspensionCompression = 0.0f;
        }

        // Calculate wheel spin
        glm::vec3 wheelForward = GetWheelForward(static_cast<int>(i));
        float groundSpeed = glm::dot(m_velocity, wheelForward);
        float wheelCircumference = 2.0f * 3.14159f * wc.radius;
        float targetAngularVel = groundSpeed / wc.radius;

        if (ws.isGrounded) {
            ws.angularVelocity = glm::mix(ws.angularVelocity, targetAngularVel, 10.0f * deltaTime);
        }

        // Calculate slip
        if (ws.isGrounded && std::abs(ws.angularVelocity) > 0.1f) {
            float wheelSpeed = ws.angularVelocity * wc.radius;
            ws.slipRatio = (wheelSpeed - groundSpeed) / std::max(std::abs(groundSpeed), 1.0f);
            ws.slipRatio = std::clamp(ws.slipRatio, -1.0f, 1.0f);

            glm::vec3 wheelRight = GetWheelRight(static_cast<int>(i));
            float lateralSpeed = glm::dot(m_velocity, wheelRight);
            ws.slipAngle = std::atan2(lateralSpeed, std::abs(groundSpeed) + 1.0f);
        } else {
            ws.slipRatio = 0;
            ws.slipAngle = 0;
        }
    }
}

void VehiclePhysics::UpdateSuspension(float deltaTime) {
    // This is handled in ApplyTireForces for simplicity
}

void VehiclePhysics::ApplyTireForces(float deltaTime) {
    glm::vec3 totalForce(0);
    glm::vec3 totalTorque(0);

    // Calculate engine torque
    float engineTorque = 0;
    if (m_currentGear != 0) {
        float gearRatio = m_config.gearRatios[m_currentGear + 1];
        engineTorque = CalculateTorque(m_output.velocity.z > 0 ? 1.0f : -1.0f, m_engineRPM);
        engineTorque *= gearRatio * m_config.finalDriveRatio;
    }

    // Count driven wheels for torque distribution
    int drivenCount = 0;
    for (const auto& wheel : m_config.wheels) {
        if (wheel.isDriven) drivenCount++;
    }
    if (drivenCount > 0) {
        engineTorque /= drivenCount;
    }

    // Apply forces from each wheel
    for (size_t i = 0; i < m_wheelStates.size(); ++i) {
        const auto& ws = m_wheelStates[i];
        const auto& wc = m_config.wheels[i];

        if (!ws.isGrounded) continue;

        // Suspension force
        float springForce = wc.suspensionStiffness * ws.suspensionCompression * wc.suspensionLength;

        // Damping (approximate)
        float dampingForce = wc.suspensionDamping * ws.suspensionCompression * 2.0f;

        float suspensionForce = springForce + dampingForce;
        suspensionForce = std::max(0.0f, suspensionForce);

        // Apply suspension force
        glm::vec3 worldUp = m_orientation * glm::vec3(0, 1, 0);
        glm::vec3 suspForceVec = worldUp * suspensionForce;
        totalForce += suspForceVec;

        // Torque from suspension
        glm::vec3 wheelOffset = ws.worldPosition - (m_position + m_orientation * m_config.centerOfMass);
        totalTorque += glm::cross(wheelOffset, suspForceVec);

        // Tire forces
        float wheelTorque = 0;
        if (wc.isDriven) {
            wheelTorque = engineTorque * m_clutchEngagement;
            ApplyTractionControl(wheelTorque, static_cast<int>(i));
        }

        float brakeTorque = CalculateBrakeTorque(0.0f, false, static_cast<int>(i));  // Will use input from Update

        glm::vec3 tireForce, tireTorque;
        CalculateTireForces(static_cast<int>(i), wheelTorque, brakeTorque, tireForce, tireTorque);

        totalForce += tireForce;
        totalTorque += tireTorque;
    }

    // Apply gravity
    totalForce += glm::vec3(0, -9.81f * m_config.mass, 0);

    // Calculate acceleration
    glm::vec3 linearAccel = totalForce / m_config.mass;

    // Calculate angular acceleration (simplified inertia)
    float inertiaY = m_config.mass * (m_config.dimensions.x * m_config.dimensions.x +
                                       m_config.dimensions.z * m_config.dimensions.z) / 12.0f;
    glm::vec3 angularAccel = totalTorque / glm::vec3(inertiaY * 2.0f, inertiaY, inertiaY * 2.0f);

    ApplyStabilityControl(angularAccel);

    // Integrate
    m_velocity += linearAccel * deltaTime;
    m_angularVelocity += angularAccel * deltaTime;

    // Angular damping
    m_angularVelocity *= 0.98f;
}

void VehiclePhysics::ApplyAerodynamics(float deltaTime) {
    float speed = glm::length(m_velocity);
    if (speed < 0.1f) return;

    glm::vec3 dragDir = -glm::normalize(m_velocity);
    float dragMagnitude = 0.5f * 1.225f * m_config.dragCoefficient *
                          m_config.frontalArea * speed * speed;

    glm::vec3 dragForce = dragDir * dragMagnitude;
    m_velocity += (dragForce / m_config.mass) * deltaTime;

    // Downforce
    float downforce = m_config.downforceCoefficient * speed * speed;
    m_velocity.y -= (downforce / m_config.mass) * deltaTime;
}

void VehiclePhysics::IntegrateState(float deltaTime) {
    // Update position
    m_position += m_velocity * deltaTime;

    // Update orientation
    float angSpeed = glm::length(m_angularVelocity);
    if (angSpeed > 0.001f) {
        glm::vec3 axis = m_angularVelocity / angSpeed;
        glm::quat deltaRot = glm::angleAxis(angSpeed * deltaTime, axis);
        m_orientation = deltaRot * m_orientation;
        m_orientation = glm::normalize(m_orientation);
    }

    // Ground collision
    float minHeight = 1000.0f;
    bool anyGrounded = false;

    for (const auto& ws : m_wheelStates) {
        if (ws.isGrounded) {
            anyGrounded = true;
            float wheelBottom = ws.worldPosition.y - m_config.wheels[0].radius;
            minHeight = std::min(minHeight, ws.contactPoint.y);
        }
    }

    if (!anyGrounded) {
        // All wheels in air - check body collision
        float terrainHeight = m_terrain->SampleHeight(m_position.x, m_position.z);
        float bodyBottom = m_position.y - m_config.dimensions.y * 0.3f;

        if (bodyBottom < terrainHeight) {
            m_position.y = terrainHeight + m_config.dimensions.y * 0.3f;
            m_velocity.y = std::max(0.0f, m_velocity.y);
        }

        m_airTime += deltaTime;
    } else {
        m_airTime = 0;
    }

    m_wasAirborne = !anyGrounded;
}

void VehiclePhysics::UpdateOutput() {
    m_output.position = m_position;
    m_output.orientation = m_orientation;
    m_output.velocity = m_velocity;
    m_output.angularVelocity = m_angularVelocity;
    m_output.speed = glm::length(m_velocity);
    m_output.speedKPH = m_output.speed * 3.6f;
    m_output.engineRPM = m_engineRPM;
    m_output.currentGear = m_currentGear;
    m_output.steerAngle = m_currentSteerAngle;
    m_output.wheels = m_wheelStates;
    m_output.isAirborne = m_wasAirborne;
    m_output.airTime = m_airTime;
}

// ============================================================================
// Helpers
// ============================================================================

glm::vec3 VehiclePhysics::GetWheelWorldPosition(int wheelIndex) const {
    if (wheelIndex < 0 || wheelIndex >= static_cast<int>(m_config.wheels.size())) {
        return m_position;
    }
    return m_position + m_orientation * m_config.wheels[wheelIndex].localPosition;
}

glm::vec3 VehiclePhysics::GetWheelForward(int wheelIndex) const {
    glm::vec3 forward = m_orientation * glm::vec3(0, 0, 1);

    if (wheelIndex >= 0 && wheelIndex < static_cast<int>(m_wheelStates.size())) {
        float steer = m_wheelStates[wheelIndex].steerAngle;
        if (std::abs(steer) > 0.001f) {
            glm::quat steerRot = glm::angleAxis(steer, glm::vec3(0, 1, 0));
            forward = m_orientation * steerRot * glm::vec3(0, 0, 1);
        }
    }

    return forward;
}

glm::vec3 VehiclePhysics::GetWheelRight(int wheelIndex) const {
    glm::vec3 forward = GetWheelForward(wheelIndex);
    glm::vec3 up = m_orientation * glm::vec3(0, 1, 0);
    return glm::normalize(glm::cross(up, forward));
}

float VehiclePhysics::CalculateTorque(float throttle, float rpm) const {
    // Simple torque curve - peak at 60% of max RPM
    float rpmNorm = rpm / m_config.maxEngineRPM;
    float torqueCurve = 1.0f - std::pow((rpmNorm - 0.6f) * 2.0f, 2);
    torqueCurve = std::max(0.2f, torqueCurve);

    return m_config.maxEngineTorque * throttle * torqueCurve;
}

float VehiclePhysics::CalculateBrakeTorque(float brake, bool handbrake, int wheelIndex) const {
    const auto& wc = m_config.wheels[wheelIndex];
    if (!wc.hasBrake) return 0;

    float brakeTorque = m_config.maxBrakeTorque * brake;

    // Front/rear bias
    bool isFront = wc.localPosition.z > 0;
    if (isFront) {
        brakeTorque *= m_config.brakeBias;
    } else {
        brakeTorque *= (1.0f - m_config.brakeBias);
    }

    if (handbrake && !isFront) {
        brakeTorque *= m_config.handbrakeMultiplier;
    }

    return brakeTorque;
}

void VehiclePhysics::CalculateTireForces(int wheelIndex, float engineTorque, float brakeTorque,
                                          glm::vec3& outForce, glm::vec3& outTorque) {
    auto& ws = m_wheelStates[wheelIndex];
    const auto& wc = m_config.wheels[wheelIndex];

    outForce = glm::vec3(0);
    outTorque = glm::vec3(0);

    if (!ws.isGrounded) return;

    // Normal force from suspension
    float normalForce = wc.suspensionStiffness * ws.suspensionCompression * wc.suspensionLength;
    normalForce = std::max(100.0f, normalForce);

    // Longitudinal force (acceleration/braking)
    float longForce = PacejkaLongitudinal(ws.slipRatio, normalForce, ws.grip);
    longForce += engineTorque / wc.radius;
    longForce -= brakeTorque / wc.radius * glm::sign(ws.angularVelocity);

    // Lateral force (cornering)
    float latForce = PacejkaLateral(ws.slipAngle, normalForce, ws.grip);

    // Apply in world space
    glm::vec3 wheelForward = GetWheelForward(wheelIndex);
    glm::vec3 wheelRight = GetWheelRight(wheelIndex);

    outForce = wheelForward * longForce + wheelRight * latForce;

    // Torque about vehicle center
    glm::vec3 wheelOffset = ws.worldPosition - (m_position + m_orientation * m_config.centerOfMass);
    outTorque = glm::cross(wheelOffset, outForce);
}

float VehiclePhysics::PacejkaLateral(float slipAngle, float load, float grip) const {
    // Simplified Pacejka formula for lateral force
    float B = 10.0f;  // Stiffness
    float C = 1.4f;   // Shape
    float D = load * grip;  // Peak force
    float E = -0.5f;  // Curvature

    float phi = (1.0f - E) * slipAngle + (E / B) * std::atan(B * slipAngle);
    return D * std::sin(C * std::atan(B * phi));
}

float VehiclePhysics::PacejkaLongitudinal(float slipRatio, float load, float grip) const {
    // Simplified Pacejka formula for longitudinal force
    float B = 12.0f;
    float C = 1.5f;
    float D = load * grip;
    float E = -0.3f;

    float phi = (1.0f - E) * slipRatio + (E / B) * std::atan(B * slipRatio);
    return D * std::sin(C * std::atan(B * phi));
}

void VehiclePhysics::ApplyTractionControl(float& torque, int wheelIndex) {
    if (m_config.tractionControl < 0.01f) return;

    auto& ws = m_wheelStates[wheelIndex];
    float maxSlip = 0.2f;

    if (std::abs(ws.slipRatio) > maxSlip) {
        float reduction = (std::abs(ws.slipRatio) - maxSlip) / maxSlip;
        reduction = std::clamp(reduction, 0.0f, 1.0f) * m_config.tractionControl;
        torque *= (1.0f - reduction);
    }
}

void VehiclePhysics::ApplyStabilityControl(glm::vec3& angularAccel) {
    if (m_config.stabilityControl < 0.01f) return;

    // Limit yaw rate
    float maxYawRate = 2.0f;  // rad/s
    if (std::abs(m_angularVelocity.y) > maxYawRate) {
        float correction = (std::abs(m_angularVelocity.y) - maxYawRate) * m_config.stabilityControl;
        angularAccel.y -= correction * glm::sign(m_angularVelocity.y);
    }
}

// ============================================================================
// Public Methods
// ============================================================================

void VehiclePhysics::SetPosition(const glm::vec3& position) {
    m_position = position;
}

void VehiclePhysics::SetOrientation(const glm::quat& orientation) {
    m_orientation = orientation;
}

void VehiclePhysics::SetVelocity(const glm::vec3& velocity) {
    m_velocity = velocity;
}

void VehiclePhysics::Reset(const glm::vec3& position, const glm::quat& orientation) {
    m_position = position;
    m_orientation = orientation;
    m_velocity = glm::vec3(0);
    m_angularVelocity = glm::vec3(0);
    m_engineRPM = m_config.idleRPM;
    m_currentGear = 1;
    m_airTime = 0;
    InitializeWheels();
}

glm::mat4 VehiclePhysics::GetWorldMatrix() const {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_position);
    transform *= glm::mat4_cast(m_orientation);
    return transform;
}

glm::mat4 VehiclePhysics::GetWheelMatrix(int wheelIndex) const {
    if (wheelIndex < 0 || wheelIndex >= static_cast<int>(m_wheelStates.size())) {
        return glm::mat4(1.0f);
    }

    const auto& ws = m_wheelStates[wheelIndex];
    const auto& wc = m_config.wheels[wheelIndex];

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), ws.worldPosition);
    transform *= glm::mat4_cast(m_orientation);

    // Apply steering
    if (std::abs(ws.steerAngle) > 0.001f) {
        transform = glm::rotate(transform, ws.steerAngle, glm::vec3(0, 1, 0));
    }

    // Apply wheel spin
    float spinAngle = ws.angularVelocity * 0.016f;  // Approximate for visual
    transform = glm::rotate(transform, spinAngle, glm::vec3(1, 0, 0));

    return transform;
}

void VehiclePhysics::SetEngineAudioCallback(EngineAudioCallback callback) {
    m_engineAudioCallback = std::move(callback);
}

void VehiclePhysics::SetTireAudioCallback(TireAudioCallback callback) {
    m_tireAudioCallback = std::move(callback);
}

} // namespace Cortex::Physics
