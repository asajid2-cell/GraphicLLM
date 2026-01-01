#pragma once

// VehiclePhysics.h
// Arcade-style vehicle physics for terrain navigation.
// Supports cars, trucks, and off-road vehicles with terrain surface interaction.

#include "TerrainCollider.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <functional>

namespace Cortex::Physics {

// Drive type configuration
enum class DriveType : uint8_t {
    RearWheelDrive = 0,
    FrontWheelDrive = 1,
    AllWheelDrive = 2
};

// Wheel configuration
struct WheelConfig {
    glm::vec3 localPosition;        // Position relative to vehicle center
    float radius = 0.4f;            // Wheel radius
    float width = 0.2f;             // Wheel width
    float suspensionLength = 0.3f;  // Max suspension travel
    float suspensionStiffness = 50000.0f;  // Spring constant (N/m)
    float suspensionDamping = 4000.0f;     // Damping coefficient
    float lateralFriction = 1.0f;   // Grip multiplier
    bool isSteerable = false;       // Can this wheel steer
    bool isDriven = true;           // Does this wheel receive engine power
    bool hasBrake = true;           // Does this wheel have brakes
};

// Wheel runtime state
struct WheelState {
    glm::vec3 worldPosition;        // World position of wheel center
    glm::vec3 contactPoint;         // Ground contact point
    glm::vec3 contactNormal;        // Ground surface normal
    float suspensionCompression;    // Current suspension compression (0-1)
    float angularVelocity;          // Wheel spin (rad/s)
    float steerAngle;               // Current steering angle (radians)
    float slipRatio;                // Longitudinal slip
    float slipAngle;                // Lateral slip angle
    float grip;                     // Current grip based on surface
    TerrainSurfaceProperties surface;
    bool isGrounded;                // Wheel touching ground
};

// Vehicle configuration
struct VehicleConfig {
    // Dimensions
    float mass = 1500.0f;           // Vehicle mass (kg)
    glm::vec3 centerOfMass = glm::vec3(0, 0.3f, 0);  // Local COM offset
    glm::vec3 dimensions = glm::vec3(2.0f, 1.5f, 4.5f);  // Width, height, length

    // Wheels
    std::vector<WheelConfig> wheels;

    // Drive configuration
    DriveType driveType = DriveType::RearWheelDrive;

    // Engine
    float maxEngineTorque = 400.0f;     // Peak torque (Nm)
    float maxEngineRPM = 7000.0f;       // Redline RPM
    float idleRPM = 800.0f;             // Idle RPM
    float engineBraking = 0.5f;         // Engine braking coefficient

    // Transmission
    std::vector<float> gearRatios = {-3.5f, 0.0f, 3.5f, 2.5f, 1.8f, 1.3f, 1.0f, 0.8f};  // R, N, 1-6
    float finalDriveRatio = 3.5f;

    // Braking
    float maxBrakeTorque = 4000.0f;     // Maximum brake torque (Nm)
    float brakeBias = 0.6f;             // Front brake bias (0-1)
    float handbrakeMultiplier = 1.5f;   // Handbrake strength

    // Steering
    float maxSteerAngle = 35.0f;        // Maximum steering angle (degrees)
    float steerSpeed = 3.0f;            // Steering speed (radians/s)
    float steerReturnSpeed = 5.0f;      // Steering return to center speed

    // Aerodynamics
    float dragCoefficient = 0.35f;      // Cd
    float frontalArea = 2.2f;           // m^2
    float downforceCoefficient = 0.1f;  // Downforce at speed

    // Handling tweaks
    float antirollStiffness = 20000.0f; // Anti-roll bar stiffness
    float tractionControl = 0.0f;       // 0 = off, 1 = full
    float stabilityControl = 0.0f;      // 0 = off, 1 = full
};

// Vehicle input
struct VehicleInput {
    float throttle = 0.0f;          // 0-1
    float brake = 0.0f;             // 0-1
    float steer = 0.0f;             // -1 to 1
    bool handbrake = false;
    bool shiftUp = false;
    bool shiftDown = false;
    bool clutch = false;            // Clutch input for manual transmission
};

// Vehicle output state
struct VehicleOutput {
    glm::vec3 position;             // World position
    glm::quat orientation;          // World orientation
    glm::vec3 velocity;             // Linear velocity
    glm::vec3 angularVelocity;      // Angular velocity
    float speed;                    // Speed (m/s)
    float speedKPH;                 // Speed (km/h)
    float engineRPM;                // Current engine RPM
    int currentGear;                // Current gear (-1=R, 0=N, 1-6=forward)
    float steerAngle;               // Current steering angle
    std::vector<WheelState> wheels; // Per-wheel state
    bool isAirborne;                // All wheels off ground
    float airTime;                  // Time in air
};

class VehiclePhysics {
public:
    VehiclePhysics();
    ~VehiclePhysics();

    // Initialize with terrain
    void Initialize(std::shared_ptr<TerrainCollider> terrain);

    // Configuration
    void SetConfig(const VehicleConfig& config);
    const VehicleConfig& GetConfig() const { return m_config; }

    // Create a default car configuration
    static VehicleConfig CreateDefaultCar();
    static VehicleConfig CreateDefaultTruck();
    static VehicleConfig CreateDefaultOffroad();

    // Simulation
    void Update(float deltaTime, const VehicleInput& input);

    // State
    const VehicleOutput& GetOutput() const { return m_output; }
    glm::vec3 GetPosition() const { return m_output.position; }
    glm::quat GetOrientation() const { return m_output.orientation; }
    glm::vec3 GetVelocity() const { return m_output.velocity; }
    float GetSpeed() const { return m_output.speed; }

    // Position/velocity control
    void SetPosition(const glm::vec3& position);
    void SetOrientation(const glm::quat& orientation);
    void SetVelocity(const glm::vec3& velocity);
    void Reset(const glm::vec3& position, const glm::quat& orientation);

    // Get transform matrices
    glm::mat4 GetWorldMatrix() const;
    glm::mat4 GetWheelMatrix(int wheelIndex) const;

    // Audio callbacks
    using EngineAudioCallback = std::function<void(float rpm, float throttle, float load)>;
    using TireAudioCallback = std::function<void(int wheel, float slip, const TerrainSurfaceProperties& surface)>;
    void SetEngineAudioCallback(EngineAudioCallback callback);
    void SetTireAudioCallback(TireAudioCallback callback);

private:
    std::shared_ptr<TerrainCollider> m_terrain;
    VehicleConfig m_config;
    VehicleOutput m_output;

    // Physics state
    glm::vec3 m_position;
    glm::quat m_orientation;
    glm::vec3 m_velocity;
    glm::vec3 m_angularVelocity;

    // Engine/transmission
    float m_engineRPM;
    int m_currentGear;
    float m_clutchEngagement;

    // Steering
    float m_currentSteerAngle;

    // Wheel states
    std::vector<WheelState> m_wheelStates;

    // Air time tracking
    float m_airTime;
    bool m_wasAirborne;

    // Audio callbacks
    EngineAudioCallback m_engineAudioCallback;
    TireAudioCallback m_tireAudioCallback;

    // Simulation steps
    void UpdateSteering(float deltaTime, const VehicleInput& input);
    void UpdateTransmission(float deltaTime, const VehicleInput& input);
    void UpdateWheels(float deltaTime, const VehicleInput& input);
    void UpdateSuspension(float deltaTime);
    void ApplyTireForces(float deltaTime);
    void ApplyAerodynamics(float deltaTime);
    void IntegrateState(float deltaTime);
    void UpdateOutput();

    // Physics helpers
    glm::vec3 GetWheelWorldPosition(int wheelIndex) const;
    glm::vec3 GetWheelForward(int wheelIndex) const;
    glm::vec3 GetWheelRight(int wheelIndex) const;
    float CalculateTorque(float throttle, float rpm) const;
    float CalculateBrakeTorque(float brake, bool handbrake, int wheelIndex) const;
    void CalculateTireForces(int wheelIndex, float engineTorque, float brakeTorque,
                             glm::vec3& outForce, glm::vec3& outTorque);

    // Tire model
    float PacejkaLateral(float slipAngle, float load, float grip) const;
    float PacejkaLongitudinal(float slipRatio, float load, float grip) const;

    // Stability systems
    void ApplyTractionControl(float& torque, int wheelIndex);
    void ApplyStabilityControl(glm::vec3& angularAccel);

    void InitializeWheels();
};

// ECS Component
struct VehicleComponent {
    VehicleConfig config;
    VehicleInput input;
    VehicleOutput output;
    std::shared_ptr<VehiclePhysics> physics;
    bool enabled = true;

    void Initialize(std::shared_ptr<TerrainCollider> terrain) {
        physics = std::make_shared<VehiclePhysics>();
        physics->Initialize(terrain);
        physics->SetConfig(config);
    }

    void Update(float deltaTime) {
        if (enabled && physics) {
            physics->Update(deltaTime, input);
            output = physics->GetOutput();
        }
    }
};

} // namespace Cortex::Physics
