// TerrainCollider.cpp
// Heightfield-based terrain collision implementation.

#include "TerrainCollider.h"
#include <cmath>
#include <algorithm>

namespace Cortex::Physics {

// ============================================================================
// Default Surface Properties
// ============================================================================

TerrainSurfaceProperties GetDefaultSurfaceProperties(TerrainSurfaceType type) {
    TerrainSurfaceProperties props;
    props.type = type;

    switch (type) {
        case TerrainSurfaceType::Dirt:
            props.staticFriction = 0.7f;
            props.dynamicFriction = 0.5f;
            props.restitution = 0.1f;
            props.softness = 0.2f;
            props.roughness = 0.6f;
            break;

        case TerrainSurfaceType::Grass:
            props.staticFriction = 0.8f;
            props.dynamicFriction = 0.6f;
            props.restitution = 0.15f;
            props.softness = 0.1f;
            props.roughness = 0.4f;
            break;

        case TerrainSurfaceType::Rock:
            props.staticFriction = 0.9f;
            props.dynamicFriction = 0.7f;
            props.restitution = 0.3f;
            props.softness = 0.0f;
            props.roughness = 0.8f;
            break;

        case TerrainSurfaceType::Sand:
            props.staticFriction = 0.5f;
            props.dynamicFriction = 0.3f;
            props.restitution = 0.05f;
            props.softness = 0.5f;
            props.roughness = 0.7f;
            break;

        case TerrainSurfaceType::Snow:
            props.staticFriction = 0.3f;
            props.dynamicFriction = 0.2f;
            props.restitution = 0.1f;
            props.softness = 0.4f;
            props.roughness = 0.3f;
            break;

        case TerrainSurfaceType::Mud:
            props.staticFriction = 0.4f;
            props.dynamicFriction = 0.2f;
            props.restitution = 0.0f;
            props.softness = 0.8f;
            props.roughness = 0.9f;
            break;

        case TerrainSurfaceType::Gravel:
            props.staticFriction = 0.75f;
            props.dynamicFriction = 0.55f;
            props.restitution = 0.2f;
            props.softness = 0.1f;
            props.roughness = 0.9f;
            break;

        case TerrainSurfaceType::Water:
            props.staticFriction = 0.1f;
            props.dynamicFriction = 0.05f;
            props.restitution = 0.0f;
            props.softness = 1.0f;
            props.roughness = 0.0f;
            break;

        case TerrainSurfaceType::Ice:
            props.staticFriction = 0.1f;
            props.dynamicFriction = 0.05f;
            props.restitution = 0.2f;
            props.softness = 0.0f;
            props.roughness = 0.1f;
            break;

        case TerrainSurfaceType::Pavement:
            props.staticFriction = 0.95f;
            props.dynamicFriction = 0.8f;
            props.restitution = 0.25f;
            props.softness = 0.0f;
            props.roughness = 0.5f;
            break;

        default:
            break;
    }

    return props;
}

float CalculateFriction(const TerrainSurfaceProperties& surface,
                        const glm::vec3& velocity, bool isMoving) {
    if (!isMoving) {
        return surface.staticFriction;
    }

    float speed = glm::length(velocity);
    // Blend from static to dynamic friction based on speed
    float t = std::min(1.0f, speed / 2.0f);
    return glm::mix(surface.staticFriction, surface.dynamicFriction, t);
}

float CalculateTireGrip(const TerrainSurfaceProperties& surface,
                        float speed, float slipAngle) {
    // Pacejka-like tire model simplified
    float baseFriction = surface.dynamicFriction;

    // Reduce grip on soft surfaces
    float softnessReduction = 1.0f - surface.softness * 0.5f;

    // Slip angle affects grip (peak around 8-12 degrees)
    float slipFactor = 1.0f;
    float optimalSlip = glm::radians(10.0f);
    if (std::abs(slipAngle) > optimalSlip) {
        slipFactor = optimalSlip / std::abs(slipAngle);
        slipFactor = std::max(0.5f, slipFactor);
    }

    // High speed reduces grip on loose surfaces
    float speedFactor = 1.0f - (surface.roughness > 0.6f ? speed * 0.01f : 0.0f);
    speedFactor = std::max(0.5f, speedFactor);

    return baseFriction * softnessReduction * slipFactor * speedFactor;
}

// ============================================================================
// TerrainCollider Implementation
// ============================================================================

TerrainCollider::TerrainCollider() {
    InitDefaultSurfaceProperties();
}

TerrainCollider::~TerrainCollider() = default;

void TerrainCollider::Initialize(std::shared_ptr<IHeightfieldSource> heightSource) {
    m_heightSource = std::move(heightSource);
    if (m_heightSource) {
        m_heightSource->GetBounds(m_boundsMin, m_boundsMax);
    }
}

void TerrainCollider::InitDefaultSurfaceProperties() {
    for (int i = 0; i < static_cast<int>(TerrainSurfaceType::COUNT); ++i) {
        m_surfaceProperties[i] = GetDefaultSurfaceProperties(static_cast<TerrainSurfaceType>(i));
    }
}

void TerrainCollider::SetSurfaceProperties(TerrainSurfaceType type,
                                            const TerrainSurfaceProperties& props) {
    size_t index = static_cast<size_t>(type);
    if (index < static_cast<size_t>(TerrainSurfaceType::COUNT)) {
        m_surfaceProperties[index] = props;
    }
}

const TerrainSurfaceProperties& TerrainCollider::GetSurfaceProperties(TerrainSurfaceType type) const {
    size_t index = static_cast<size_t>(type);
    if (index < static_cast<size_t>(TerrainSurfaceType::COUNT)) {
        return m_surfaceProperties[index];
    }
    return m_surfaceProperties[0];
}

// ============================================================================
// Height Queries
// ============================================================================

float TerrainCollider::SampleHeight(float x, float z) const {
    if (!m_heightSource || !IsWithinBounds(x, z)) {
        return 0.0f;
    }
    return m_heightSource->SampleHeight(x, z);
}

float TerrainCollider::SampleHeightInternal(float x, float z) const {
    return m_heightSource ? m_heightSource->SampleHeight(x, z) : 0.0f;
}

HeightSample TerrainCollider::SampleHeightFull(float x, float z) const {
    HeightSample sample;

    if (!m_heightSource || !IsWithinBounds(x, z)) {
        return sample;
    }

    sample.height = m_heightSource->SampleHeight(x, z);
    sample.normal = m_heightSource->SampleNormal(x, z);
    sample.surface = GetSurfacePropertiesAt(x, z);
    sample.valid = true;

    // Calculate gradient using central differences
    const float eps = 0.1f;
    float hL = SampleHeightInternal(x - eps, z);
    float hR = SampleHeightInternal(x + eps, z);
    float hD = SampleHeightInternal(x, z - eps);
    float hU = SampleHeightInternal(x, z + eps);

    sample.gradient.x = (hR - hL) / (2.0f * eps);
    sample.gradient.y = (hU - hD) / (2.0f * eps);

    return sample;
}

glm::vec3 TerrainCollider::SampleNormal(float x, float z) const {
    if (!m_heightSource) {
        return glm::vec3(0, 1, 0);
    }
    return m_heightSource->SampleNormal(x, z);
}

glm::vec3 TerrainCollider::SampleNormalInternal(float x, float z) const {
    return m_heightSource ? m_heightSource->SampleNormal(x, z) : glm::vec3(0, 1, 0);
}

float TerrainCollider::SampleHeightBilinear(float x, float z) const {
    if (!m_heightSource) return 0.0f;

    // Sample 4 corners and bilinearly interpolate
    float fx = std::floor(x);
    float fz = std::floor(z);
    float tx = x - fx;
    float tz = z - fz;

    float h00 = SampleHeightInternal(fx, fz);
    float h10 = SampleHeightInternal(fx + 1.0f, fz);
    float h01 = SampleHeightInternal(fx, fz + 1.0f);
    float h11 = SampleHeightInternal(fx + 1.0f, fz + 1.0f);

    float h0 = glm::mix(h00, h10, tx);
    float h1 = glm::mix(h01, h11, tx);

    return glm::mix(h0, h1, tz);
}

TerrainSurfaceProperties TerrainCollider::GetSurfacePropertiesAt(float x, float z) const {
    if (!m_heightSource) {
        return m_surfaceProperties[0];
    }

    TerrainSurfaceType type = m_heightSource->GetSurfaceType(x, z);
    return GetSurfaceProperties(type);
}

// ============================================================================
// Raycasting
// ============================================================================

bool TerrainCollider::Raycast(const glm::vec3& origin, const glm::vec3& direction,
                               float maxDistance, RaycastHit& outHit) const {
    if (!m_heightSource) {
        outHit.hit = false;
        return false;
    }

    // Use stepped raycast for heightfield
    float stepSize = 0.5f;  // Adaptive step size
    return RaycastStepped(origin, direction, maxDistance, stepSize, outHit);
}

bool TerrainCollider::RaycastFiltered(const glm::vec3& origin, const glm::vec3& direction,
                                       float maxDistance, uint32_t surfaceMask,
                                       RaycastHit& outHit) const {
    RaycastHit tempHit;
    if (!Raycast(origin, direction, maxDistance, tempHit)) {
        outHit.hit = false;
        return false;
    }

    // Check if surface type matches mask
    uint32_t surfaceBit = 1u << static_cast<uint32_t>(tempHit.surface.type);
    if ((surfaceMask & surfaceBit) != 0) {
        outHit = tempHit;
        return true;
    }

    outHit.hit = false;
    return false;
}

void TerrainCollider::RaycastMultiple(const std::vector<glm::vec3>& origins,
                                       const std::vector<glm::vec3>& directions,
                                       float maxDistance,
                                       std::vector<RaycastHit>& outHits) const {
    outHits.resize(origins.size());

    // Parallel raycast (could be further optimized with SIMD)
    #pragma omp parallel for
    for (int i = 0; i < static_cast<int>(origins.size()); ++i) {
        Raycast(origins[i], directions[i], maxDistance, outHits[i]);
    }
}

bool TerrainCollider::RaycastStepped(const glm::vec3& origin, const glm::vec3& direction,
                                      float maxDistance, float stepSize, RaycastHit& outHit) const {
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 pos = origin;

    float traveled = 0.0f;
    float prevHeight = origin.y;
    float terrainHeight = SampleHeightInternal(pos.x, pos.z);

    // If starting below terrain, immediate hit
    if (pos.y < terrainHeight) {
        outHit.point = pos;
        outHit.point.y = terrainHeight;
        outHit.normal = SampleNormalInternal(pos.x, pos.z);
        outHit.distance = 0.0f;
        outHit.surface = GetSurfacePropertiesAt(pos.x, pos.z);
        outHit.hit = true;
        return true;
    }

    // Step along ray
    while (traveled < maxDistance) {
        pos += dir * stepSize;
        traveled += stepSize;

        if (!IsWithinBounds(pos.x, pos.z)) {
            break;
        }

        terrainHeight = SampleHeightInternal(pos.x, pos.z);

        // Check if ray crossed terrain surface
        if (pos.y <= terrainHeight && prevHeight > terrainHeight) {
            // Binary search for precise intersection
            glm::vec3 above = pos - dir * stepSize;
            glm::vec3 below = pos;
            outHit.point = RefineHitPoint(above, below, 8);
            outHit.normal = SampleNormalInternal(outHit.point.x, outHit.point.z);
            outHit.distance = glm::length(outHit.point - origin);
            outHit.surface = GetSurfacePropertiesAt(outHit.point.x, outHit.point.z);
            outHit.hit = true;
            return true;
        }

        prevHeight = terrainHeight;
    }

    outHit.hit = false;
    return false;
}

glm::vec3 TerrainCollider::RefineHitPoint(const glm::vec3& above, const glm::vec3& below,
                                           int iterations) const {
    glm::vec3 a = above;
    glm::vec3 b = below;

    for (int i = 0; i < iterations; ++i) {
        glm::vec3 mid = (a + b) * 0.5f;
        float terrainHeight = SampleHeightInternal(mid.x, mid.z);

        if (mid.y > terrainHeight) {
            a = mid;
        } else {
            b = mid;
        }
    }

    glm::vec3 result = (a + b) * 0.5f;
    result.y = SampleHeightInternal(result.x, result.z);
    return result;
}

// ============================================================================
// Sweep Tests
// ============================================================================

bool TerrainCollider::SphereSweep(const glm::vec3& start, const glm::vec3& end,
                                   float radius, SweepHit& outHit) const {
    glm::vec3 direction = end - start;
    float distance = glm::length(direction);

    if (distance < 0.001f) {
        // Check static overlap
        float terrainHeight = SampleHeight(start.x, start.z);
        float penetration = (terrainHeight + radius) - start.y;

        if (penetration > 0) {
            outHit.point = glm::vec3(start.x, terrainHeight, start.z);
            outHit.normal = SampleNormal(start.x, start.z);
            outHit.distance = 0.0f;
            outHit.penetration = penetration;
            outHit.surface = GetSurfacePropertiesAt(start.x, start.z);
            outHit.hit = true;
            return true;
        }

        outHit.hit = false;
        return false;
    }

    direction /= distance;

    // Step along path checking sphere vs heightfield
    float stepSize = radius * 0.5f;
    float traveled = 0.0f;
    glm::vec3 pos = start;

    while (traveled < distance) {
        float terrainHeight = SampleHeightInternal(pos.x, pos.z);
        float bottomOfSphere = pos.y - radius;

        if (bottomOfSphere <= terrainHeight) {
            // Hit detected - refine
            outHit.point = glm::vec3(pos.x, terrainHeight, pos.z);
            outHit.normal = SampleNormalInternal(pos.x, pos.z);
            outHit.distance = traveled;
            outHit.penetration = terrainHeight - bottomOfSphere;
            outHit.surface = GetSurfacePropertiesAt(pos.x, pos.z);
            outHit.hit = true;
            return true;
        }

        pos += direction * stepSize;
        traveled += stepSize;
    }

    outHit.hit = false;
    return false;
}

bool TerrainCollider::CapsuleSweep(const glm::vec3& start, const glm::vec3& end,
                                    float radius, float height, SweepHit& outHit) const {
    // Treat capsule as sphere at bottom + height check
    // More accurate would sample multiple points along capsule axis

    glm::vec3 bottomStart = start - glm::vec3(0, height * 0.5f - radius, 0);
    glm::vec3 bottomEnd = end - glm::vec3(0, height * 0.5f - radius, 0);

    return SphereSweep(bottomStart, bottomEnd, radius, outHit);
}

bool TerrainCollider::BoxSweep(const glm::vec3& start, const glm::vec3& end,
                                const glm::vec3& halfExtents, const glm::quat& orientation,
                                SweepHit& outHit) const {
    // Approximate box with multiple ray casts from corners
    glm::vec3 direction = end - start;
    float distance = glm::length(direction);
    if (distance < 0.001f) {
        direction = glm::vec3(0, -1, 0);
        distance = 1.0f;
    } else {
        direction /= distance;
    }

    // Transform corners
    glm::vec3 corners[4] = {
        glm::vec3(-halfExtents.x, -halfExtents.y, -halfExtents.z),
        glm::vec3( halfExtents.x, -halfExtents.y, -halfExtents.z),
        glm::vec3(-halfExtents.x, -halfExtents.y,  halfExtents.z),
        glm::vec3( halfExtents.x, -halfExtents.y,  halfExtents.z)
    };

    float closestDist = distance;
    bool anyHit = false;

    for (int i = 0; i < 4; ++i) {
        glm::vec3 cornerWorld = start + orientation * corners[i];
        RaycastHit rayHit;
        if (Raycast(cornerWorld, direction, distance, rayHit)) {
            if (rayHit.distance < closestDist) {
                closestDist = rayHit.distance;
                outHit.point = rayHit.point;
                outHit.normal = rayHit.normal;
                outHit.surface = rayHit.surface;
                anyHit = true;
            }
        }
    }

    if (anyHit) {
        outHit.distance = closestDist;
        outHit.penetration = 0.0f;
        outHit.hit = true;
        return true;
    }

    outHit.hit = false;
    return false;
}

// ============================================================================
// Ground Detection
// ============================================================================

GroundContact TerrainCollider::GetGroundContact(const glm::vec3& position, float radius,
                                                  const TerrainQueryParams& params) const {
    GroundContact contact;

    if (!m_heightSource) {
        return contact;
    }

    float terrainHeight = SampleHeight(position.x, position.z);
    float bottomOfObject = position.y - radius;
    float groundCheck = params.stepHeight + params.skinWidth;

    // Check if close enough to ground
    float distToGround = bottomOfObject - terrainHeight;

    if (distToGround <= groundCheck) {
        contact.isGrounded = true;
        contact.height = terrainHeight;
        contact.point = glm::vec3(position.x, terrainHeight, position.z);
        contact.normal = SampleNormal(position.x, position.z);
        contact.surface = GetSurfacePropertiesAt(position.x, position.z);

        // Calculate slope
        float slopeCos = glm::dot(contact.normal, glm::vec3(0, 1, 0));
        contact.slope = glm::degrees(std::acos(std::clamp(slopeCos, -1.0f, 1.0f)));

        // Calculate slope direction
        glm::vec3 slopeDir = GetSlopeDirection(position.x, position.z);
        contact.slopeDirection = std::atan2(slopeDir.z, slopeDir.x);

        // Check if walkable
        if (contact.slope > params.maxSlope) {
            contact.isGrounded = false;  // Too steep to stand on
        }
    }

    return contact;
}

bool TerrainCollider::IsOnWalkableGround(const glm::vec3& position, float radius,
                                          float maxSlope) const {
    TerrainQueryParams params;
    params.maxSlope = maxSlope;
    GroundContact contact = GetGroundContact(position, radius, params);
    return contact.isGrounded;
}

glm::vec3 TerrainCollider::GetSlopeDirection(float x, float z) const {
    HeightSample sample = SampleHeightFull(x, z);
    if (!sample.valid) {
        return glm::vec3(0, -1, 0);
    }

    // Gradient points uphill, so negate for downhill direction
    glm::vec2 grad = sample.gradient;
    float len = glm::length(grad);

    if (len < 0.001f) {
        return glm::vec3(0, 0, 0);  // Flat
    }

    grad /= len;
    return glm::normalize(glm::vec3(-grad.x, -len, -grad.y));
}

glm::vec3 TerrainCollider::CalculateSlipVelocity(const glm::vec3& position,
                                                  float mass, float friction) const {
    HeightSample sample = SampleHeightFull(position.x, position.z);
    if (!sample.valid) {
        return glm::vec3(0);
    }

    float slopeCos = glm::dot(sample.normal, glm::vec3(0, 1, 0));
    float slopeAngle = std::acos(std::clamp(slopeCos, -1.0f, 1.0f));

    // Calculate gravity component along slope
    float gravity = 9.81f;
    float slopeForce = gravity * std::sin(slopeAngle);

    // Apply friction
    float frictionForce = gravity * std::cos(slopeAngle) * friction;

    if (slopeForce <= frictionForce) {
        return glm::vec3(0);  // Friction prevents sliding
    }

    // Net acceleration
    float accel = (slopeForce - frictionForce);

    // Direction is down the slope
    glm::vec3 slopeDir = GetSlopeDirection(position.x, position.z);

    return slopeDir * accel;
}

// ============================================================================
// Vehicle Physics
// ============================================================================

TerrainCollider::WheelContact TerrainCollider::GetWheelContact(
    const glm::vec3& wheelPos, const glm::vec3& wheelDown,
    float suspensionLength, float wheelRadius) const {

    WheelContact contact;
    contact.isContact = false;

    RaycastHit hit;
    if (Raycast(wheelPos, wheelDown, suspensionLength + wheelRadius, hit)) {
        contact.contactPoint = hit.point;
        contact.normal = hit.normal;
        contact.surface = hit.surface;
        contact.suspensionLength = hit.distance - wheelRadius;
        contact.grip = hit.surface.dynamicFriction * (1.0f - hit.surface.softness * 0.5f);
        contact.isContact = true;
    }

    return contact;
}

glm::quat TerrainCollider::CalculateVehicleOrientation(
    const std::vector<WheelContact>& wheels) const {

    if (wheels.size() < 3) {
        return glm::quat(1, 0, 0, 0);
    }

    // Average the contact normals
    glm::vec3 avgNormal(0);
    int contactCount = 0;

    for (const auto& wheel : wheels) {
        if (wheel.isContact) {
            avgNormal += wheel.normal;
            contactCount++;
        }
    }

    if (contactCount == 0) {
        return glm::quat(1, 0, 0, 0);
    }

    avgNormal = glm::normalize(avgNormal / static_cast<float>(contactCount));

    // Create orientation from up vector
    glm::vec3 forward(0, 0, 1);
    glm::vec3 right = glm::normalize(glm::cross(avgNormal, forward));
    forward = glm::cross(right, avgNormal);

    glm::mat3 rotMat(right, avgNormal, forward);
    return glm::quat_cast(rotMat);
}

// ============================================================================
// Collision Response
// ============================================================================

glm::vec3 TerrainCollider::DepenetratePosition(const glm::vec3& position, float radius) const {
    float terrainHeight = SampleHeight(position.x, position.z);
    float requiredY = terrainHeight + radius;

    if (position.y < requiredY) {
        return glm::vec3(position.x, requiredY, position.z);
    }

    return position;
}

glm::vec3 TerrainCollider::ResolveVelocity(const glm::vec3& position, const glm::vec3& velocity,
                                            float radius, float restitution,
                                            float friction) const {
    glm::vec3 normal = SampleNormal(position.x, position.z);

    // Decompose velocity into normal and tangent components
    float normalSpeed = glm::dot(velocity, normal);

    if (normalSpeed >= 0) {
        return velocity;  // Moving away from surface
    }

    glm::vec3 normalVel = normal * normalSpeed;
    glm::vec3 tangentVel = velocity - normalVel;

    // Apply restitution to normal component
    glm::vec3 newNormalVel = -normalVel * restitution;

    // Apply friction to tangent component
    float tangentSpeed = glm::length(tangentVel);
    if (tangentSpeed > 0.001f) {
        float frictionDecel = -normalSpeed * friction;
        float newTangentSpeed = std::max(0.0f, tangentSpeed - frictionDecel);
        tangentVel *= newTangentSpeed / tangentSpeed;
    }

    return newNormalVel + tangentVel;
}

glm::vec3 TerrainCollider::ProjectOnSurface(const glm::vec3& position,
                                             const glm::vec3& movement) const {
    glm::vec3 normal = SampleNormal(position.x, position.z);

    // Remove component of movement that goes into the surface
    float dot = glm::dot(movement, normal);
    if (dot < 0) {
        return movement - normal * dot;
    }

    return movement;
}

// ============================================================================
// Utility
// ============================================================================

bool TerrainCollider::IsWithinBounds(float x, float z) const {
    if (!m_heightSource) return false;
    return m_heightSource->IsValidPosition(x, z);
}

void TerrainCollider::GetBounds(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = m_boundsMin;
    outMax = m_boundsMax;
}

void TerrainCollider::GetResolution(int& outWidth, int& outHeight) const {
    // Default resolution estimate from bounds
    outWidth = static_cast<int>(m_boundsMax.x - m_boundsMin.x);
    outHeight = static_cast<int>(m_boundsMax.z - m_boundsMin.z);
}

} // namespace Cortex::Physics
