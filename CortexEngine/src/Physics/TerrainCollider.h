#pragma once

// TerrainCollider.h
// Heightfield-based terrain collision system for character/vehicle physics.
// Provides efficient height queries, raycasting, and normal sampling.

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace Cortex::Physics {

// Forward declarations
struct RaycastHit;
struct SweepHit;
class TerrainCollider;

// Surface material types for friction/sound effects
enum class TerrainSurfaceType : uint8_t {
    Dirt = 0,
    Grass = 1,
    Rock = 2,
    Sand = 3,
    Snow = 4,
    Mud = 5,
    Gravel = 6,
    Water = 7,
    Ice = 8,
    Pavement = 9,
    COUNT
};

// Surface properties for physics simulation
struct TerrainSurfaceProperties {
    TerrainSurfaceType type = TerrainSurfaceType::Grass;
    float staticFriction = 0.8f;        // Friction when stationary
    float dynamicFriction = 0.6f;       // Friction when moving
    float restitution = 0.1f;           // Bounciness (0-1)
    float softness = 0.0f;              // Deformation factor (0 = hard, 1 = soft)
    float roughness = 0.5f;             // Affects tire grip, footstep sounds
};

// Raycast hit result
struct RaycastHit {
    glm::vec3 point;                    // World-space hit point
    glm::vec3 normal;                   // Surface normal at hit
    float distance;                     // Distance from ray origin
    TerrainSurfaceProperties surface;   // Surface properties at hit
    bool hit;                           // Whether ray hit terrain

    RaycastHit() : point(0), normal(0, 1, 0), distance(0), hit(false) {}
};

// Sphere/capsule sweep hit result
struct SweepHit {
    glm::vec3 point;                    // First contact point
    glm::vec3 normal;                   // Surface normal at contact
    float distance;                     // Distance traveled before hit
    float penetration;                  // Penetration depth (negative if overlapping)
    TerrainSurfaceProperties surface;
    bool hit;

    SweepHit() : point(0), normal(0, 1, 0), distance(0), penetration(0), hit(false) {}
};

// Ground contact information for character/vehicle
struct GroundContact {
    glm::vec3 point;                    // Contact point
    glm::vec3 normal;                   // Ground normal
    float height;                       // Ground height at position
    float slope;                        // Slope angle (0 = flat, 90 = vertical)
    float slopeDirection;               // Direction of steepest descent (radians)
    TerrainSurfaceProperties surface;
    bool isGrounded;                    // Whether contact exists

    GroundContact() : point(0), normal(0, 1, 0), height(0), slope(0),
                      slopeDirection(0), isGrounded(false) {}
};

// Height sample with derivatives for physics
struct HeightSample {
    float height;                       // Height at sample point
    glm::vec3 normal;                   // Surface normal
    glm::vec2 gradient;                 // Height gradient (dh/dx, dh/dz)
    TerrainSurfaceProperties surface;
    bool valid;                         // Whether sample is within terrain bounds

    HeightSample() : height(0), normal(0, 1, 0), gradient(0), valid(false) {}
};

// Terrain collision query parameters
struct TerrainQueryParams {
    float maxSlope = 60.0f;             // Maximum walkable slope (degrees)
    float stepHeight = 0.5f;            // Maximum step height for characters
    float skinWidth = 0.01f;            // Collision skin thickness
    bool includeSoftSurfaces = true;    // Include water/mud in queries
};

// Heightfield data source interface
class IHeightfieldSource {
public:
    virtual ~IHeightfieldSource() = default;
    virtual float SampleHeight(float x, float z) const = 0;
    virtual glm::vec3 SampleNormal(float x, float z) const = 0;
    virtual TerrainSurfaceType GetSurfaceType(float x, float z) const = 0;
    virtual bool IsValidPosition(float x, float z) const = 0;
    virtual void GetBounds(glm::vec3& outMin, glm::vec3& outMax) const = 0;
};

// Terrain collision system
class TerrainCollider {
public:
    TerrainCollider();
    ~TerrainCollider();

    // Initialize with heightfield source
    void Initialize(std::shared_ptr<IHeightfieldSource> heightSource);

    // Configure surface properties per type
    void SetSurfaceProperties(TerrainSurfaceType type, const TerrainSurfaceProperties& props);
    const TerrainSurfaceProperties& GetSurfaceProperties(TerrainSurfaceType type) const;

    // ========================================================================
    // Height Queries
    // ========================================================================

    // Sample height at world position
    float SampleHeight(float x, float z) const;

    // Sample height with full derivative info
    HeightSample SampleHeightFull(float x, float z) const;

    // Sample normal at world position
    glm::vec3 SampleNormal(float x, float z) const;

    // Bilinear interpolated height sample (smoother for physics)
    float SampleHeightBilinear(float x, float z) const;

    // ========================================================================
    // Raycasting
    // ========================================================================

    // Cast ray against terrain
    bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
                 float maxDistance, RaycastHit& outHit) const;

    // Cast ray with layer mask (for selective collision)
    bool RaycastFiltered(const glm::vec3& origin, const glm::vec3& direction,
                         float maxDistance, uint32_t surfaceMask,
                         RaycastHit& outHit) const;

    // Cast multiple rays for tire simulation
    void RaycastMultiple(const std::vector<glm::vec3>& origins,
                         const std::vector<glm::vec3>& directions,
                         float maxDistance,
                         std::vector<RaycastHit>& outHits) const;

    // ========================================================================
    // Sweep Tests
    // ========================================================================

    // Sphere sweep against terrain
    bool SphereSweep(const glm::vec3& start, const glm::vec3& end,
                     float radius, SweepHit& outHit) const;

    // Capsule sweep (for character controllers)
    bool CapsuleSweep(const glm::vec3& start, const glm::vec3& end,
                      float radius, float height, SweepHit& outHit) const;

    // Box sweep (for vehicles)
    bool BoxSweep(const glm::vec3& start, const glm::vec3& end,
                  const glm::vec3& halfExtents, const glm::quat& orientation,
                  SweepHit& outHit) const;

    // ========================================================================
    // Ground Detection
    // ========================================================================

    // Get ground contact for character at position
    GroundContact GetGroundContact(const glm::vec3& position, float radius,
                                   const TerrainQueryParams& params = {}) const;

    // Check if position is on walkable ground
    bool IsOnWalkableGround(const glm::vec3& position, float radius,
                            float maxSlope = 60.0f) const;

    // Get slope direction at position (for sliding)
    glm::vec3 GetSlopeDirection(float x, float z) const;

    // Get slip velocity for slope (gravity-induced sliding)
    glm::vec3 CalculateSlipVelocity(const glm::vec3& position,
                                     float mass, float friction) const;

    // ========================================================================
    // Vehicle Physics Helpers
    // ========================================================================

    // Sample terrain under all wheels
    struct WheelContact {
        glm::vec3 contactPoint;
        glm::vec3 normal;
        float suspensionLength;
        float grip;                     // Tire grip based on surface
        TerrainSurfaceProperties surface;
        bool isContact;
    };

    WheelContact GetWheelContact(const glm::vec3& wheelPos,
                                  const glm::vec3& wheelDown,
                                  float suspensionLength, float wheelRadius) const;

    // Calculate vehicle orientation from wheel contacts
    glm::quat CalculateVehicleOrientation(const std::vector<WheelContact>& wheels) const;

    // ========================================================================
    // Collision Response
    // ========================================================================

    // Push position out of terrain (depenetration)
    glm::vec3 DepenetratePosition(const glm::vec3& position, float radius) const;

    // Resolve velocity against terrain (for bouncing/sliding)
    glm::vec3 ResolveVelocity(const glm::vec3& position, const glm::vec3& velocity,
                               float radius, float restitution = 0.0f,
                               float friction = 1.0f) const;

    // Project movement onto terrain surface (for grounded movement)
    glm::vec3 ProjectOnSurface(const glm::vec3& position, const glm::vec3& movement) const;

    // ========================================================================
    // Utility
    // ========================================================================

    // Check if position is within terrain bounds
    bool IsWithinBounds(float x, float z) const;

    // Get terrain bounds
    void GetBounds(glm::vec3& outMin, glm::vec3& outMax) const;

    // Get resolution of heightfield
    void GetResolution(int& outWidth, int& outHeight) const;

private:
    std::shared_ptr<IHeightfieldSource> m_heightSource;
    TerrainSurfaceProperties m_surfaceProperties[static_cast<size_t>(TerrainSurfaceType::COUNT)];

    glm::vec3 m_boundsMin;
    glm::vec3 m_boundsMax;

    // Internal helpers
    float SampleHeightInternal(float x, float z) const;
    glm::vec3 SampleNormalInternal(float x, float z) const;
    TerrainSurfaceProperties GetSurfacePropertiesAt(float x, float z) const;

    // Raycast stepping (DDA-like algorithm for heightfield)
    bool RaycastStepped(const glm::vec3& origin, const glm::vec3& direction,
                        float maxDistance, float stepSize, RaycastHit& outHit) const;

    // Binary search for precise hit point
    glm::vec3 RefineHitPoint(const glm::vec3& above, const glm::vec3& below,
                             int iterations = 8) const;

    void InitDefaultSurfaceProperties();
};

// Convenience functions

// Get default surface properties for a type
TerrainSurfaceProperties GetDefaultSurfaceProperties(TerrainSurfaceType type);

// Calculate friction coefficient from surface and velocity
float CalculateFriction(const TerrainSurfaceProperties& surface,
                        const glm::vec3& velocity, bool isMoving);

// Calculate tire grip based on surface and speed
float CalculateTireGrip(const TerrainSurfaceProperties& surface,
                        float speed, float slipAngle);

} // namespace Cortex::Physics
