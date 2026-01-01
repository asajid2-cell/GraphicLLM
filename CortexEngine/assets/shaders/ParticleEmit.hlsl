// ParticleEmit.hlsl
// Compute shader for GPU particle emission.
// Spawns new particles based on emitter configuration.

#include "Common.hlsli"

// Particle structure (must match CPU)
struct Particle {
    float4 position;        // xyz=pos, w=size
    float4 velocity;        // xyz=vel, w=rotation
    float4 color;           // rgba
    float4 params;          // x=age, y=lifetime, z=emitterIdx, w=seed
    float4 params2;         // x=rotSpeed, y=sizeStart, z=sizeEnd, w=flags
    float4 params3;         // x=colorLerp, y=gravityMod, z=dragCoeff, w=unused
    float4 sortKey;         // x=depth, yzw=unused
};

// Emitter shapes
#define SHAPE_POINT         0
#define SHAPE_SPHERE        1
#define SHAPE_HEMISPHERE    2
#define SHAPE_CONE          3
#define SHAPE_BOX           4
#define SHAPE_CIRCLE        5
#define SHAPE_EDGE          6

// Emitter constant buffer
cbuffer EmitterCB : register(b0) {
    float4x4 g_EmitterMatrix;
    float4 g_EmitterPosition;       // xyz=pos, w=deltaTime
    float4 g_EmitterVelocity;       // xyz=vel, w=time
    float4 g_ShapeParams;           // x=radius, y=angle, z=arc, w=shape
    float4 g_ShapeSize;             // xyz=size, w=emitFromEdge
    float4 g_VelocityParams;        // xyz=direction, w=speed
    float4 g_VelocityVariation;     // x=speedVar, y=inheritScale, z=inherit, w=unused
    float4 g_LifetimeParams;        // x=lifetime, y=lifeVar, z=unused, w=unused
    float4 g_SizeParams;            // x=startSize, y=endSize, z=sizeVar, w=unused
    float4 g_StartColor;
    float4 g_EndColor;
    float4 g_RotationParams;        // x=startRot, y=rotVar, z=rotSpeed, w=rotSpeedVar
    float4 g_PhysicsParams;         // x=gravity, y=gravityMod, z=drag, w=unused

    uint g_EmitterIndex;
    uint g_ParticlesToEmit;
    uint g_MaxParticles;
    uint g_RandomSeed;
};

// Particle buffers
RWStructuredBuffer<Particle> g_Particles : register(u0);

// Dead particle indices (append buffer for allocation)
ConsumeStructuredBuffer<uint> g_DeadList : register(u1);

// Alive particle counter
RWStructuredBuffer<uint> g_AliveCount : register(u2);

// Random number generation (PCG hash)
uint PCGHash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat(uint seed) {
    return float(PCGHash(seed)) / 4294967295.0;
}

float RandomRange(uint seed, float minVal, float maxVal) {
    return minVal + RandomFloat(seed) * (maxVal - minVal);
}

float3 RandomInSphere(uint seed) {
    float u = RandomFloat(seed);
    float v = RandomFloat(seed + 1);
    float theta = u * 2.0 * 3.14159265;
    float phi = acos(2.0 * v - 1.0);
    float r = pow(RandomFloat(seed + 2), 1.0 / 3.0);

    float sinPhi = sin(phi);
    return float3(
        r * sinPhi * cos(theta),
        r * cos(phi),
        r * sinPhi * sin(theta)
    );
}

float3 RandomOnSphere(uint seed) {
    float u = RandomFloat(seed);
    float v = RandomFloat(seed + 1);
    float theta = u * 2.0 * 3.14159265;
    float phi = acos(2.0 * v - 1.0);

    float sinPhi = sin(phi);
    return float3(
        sinPhi * cos(theta),
        cos(phi),
        sinPhi * sin(theta)
    );
}

float3 RandomInCone(uint seed, float angle) {
    float cosAngle = cos(angle);
    float z = RandomRange(seed, cosAngle, 1.0);
    float phi = RandomRange(seed + 1, 0.0, 2.0 * 3.14159265);
    float sinTheta = sqrt(1.0 - z * z);

    return float3(
        sinTheta * cos(phi),
        z,
        sinTheta * sin(phi)
    );
}

float3 GetEmissionPosition(uint seed, uint shape) {
    float radius = g_ShapeParams.x;
    float3 size = g_ShapeSize.xyz;
    bool fromEdge = g_ShapeSize.w > 0.5;

    switch (shape) {
        case SHAPE_POINT:
            return float3(0, 0, 0);

        case SHAPE_SPHERE:
            if (fromEdge) {
                return RandomOnSphere(seed) * radius;
            } else {
                return RandomInSphere(seed) * radius;
            }

        case SHAPE_HEMISPHERE: {
            float3 p = fromEdge ? RandomOnSphere(seed) : RandomInSphere(seed);
            p.y = abs(p.y);
            return p * radius;
        }

        case SHAPE_CONE: {
            float angle = g_ShapeParams.y;
            float3 dir = RandomInCone(seed, angle);
            float dist = RandomFloat(seed + 3) * radius;
            return dir * dist;
        }

        case SHAPE_BOX:
            return float3(
                RandomRange(seed, -size.x, size.x) * 0.5,
                RandomRange(seed + 1, -size.y, size.y) * 0.5,
                RandomRange(seed + 2, -size.z, size.z) * 0.5
            );

        case SHAPE_CIRCLE: {
            float arc = g_ShapeParams.z;
            float theta = RandomFloat(seed) * arc;
            float r = fromEdge ? radius : RandomFloat(seed + 1) * radius;
            return float3(cos(theta) * r, 0, sin(theta) * r);
        }

        case SHAPE_EDGE: {
            float t = RandomFloat(seed);
            return float3(t * size.x - size.x * 0.5, 0, 0);
        }

        default:
            return float3(0, 0, 0);
    }
}

float3 GetEmissionVelocity(uint seed, uint shape) {
    float3 direction = normalize(g_VelocityParams.xyz);
    float speed = g_VelocityParams.w;
    float speedVar = g_VelocityVariation.x;

    // Modify direction based on shape
    if (shape == SHAPE_SPHERE || shape == SHAPE_HEMISPHERE) {
        direction = RandomOnSphere(seed + 10);
        if (shape == SHAPE_HEMISPHERE) {
            direction.y = abs(direction.y);
        }
    } else if (shape == SHAPE_CONE) {
        direction = RandomInCone(seed + 10, g_ShapeParams.y);
    }

    // Apply speed variation
    speed *= (1.0 + RandomRange(seed + 20, -speedVar, speedVar));

    return direction * speed;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID) {
    uint particleIndex = dispatchID.x;

    // Check if we should emit this particle
    if (particleIndex >= g_ParticlesToEmit) {
        return;
    }

    // Try to consume a dead particle slot
    uint deadIndex = g_DeadList.Consume();
    if (deadIndex >= g_MaxParticles) {
        return;  // No free slots
    }

    // Generate random seed for this particle
    uint seed = g_RandomSeed + particleIndex * 1973 + deadIndex * 9277;

    // Get shape type
    uint shape = uint(g_ShapeParams.w);

    // Calculate emission position in local space
    float3 localPos = GetEmissionPosition(seed, shape);

    // Transform to world space
    float3 worldPos = mul(g_EmitterMatrix, float4(localPos, 1.0)).xyz;

    // Calculate emission velocity in local space
    float3 localVel = GetEmissionVelocity(seed + 100, shape);

    // Transform velocity to world space
    float3 worldVel = mul((float3x3)g_EmitterMatrix, localVel);

    // Add inherited velocity
    if (g_VelocityVariation.z > 0.5) {
        worldVel += g_EmitterVelocity.xyz * g_VelocityVariation.y;
    }

    // Calculate lifetime
    float lifetime = g_LifetimeParams.x;
    float lifeVar = g_LifetimeParams.y;
    lifetime *= (1.0 + RandomRange(seed + 200, -lifeVar, lifeVar));

    // Calculate size
    float startSize = g_SizeParams.x;
    float sizeVar = g_SizeParams.z;
    startSize *= (1.0 + RandomRange(seed + 300, -sizeVar, sizeVar));

    // Calculate rotation
    float startRot = g_RotationParams.x;
    float rotVar = g_RotationParams.y;
    startRot += RandomRange(seed + 400, -rotVar, rotVar);
    startRot = startRot * 3.14159265 / 180.0;  // Convert to radians

    float rotSpeed = g_RotationParams.z;
    float rotSpeedVar = g_RotationParams.w;
    rotSpeed += RandomRange(seed + 500, -rotSpeedVar, rotSpeedVar);
    rotSpeed = rotSpeed * 3.14159265 / 180.0;

    // Initialize particle
    Particle p;
    p.position = float4(worldPos, startSize);
    p.velocity = float4(worldVel, startRot);
    p.color = g_StartColor;
    p.params = float4(0.0, lifetime, float(g_EmitterIndex), float(seed));
    p.params2 = float4(rotSpeed, startSize, g_SizeParams.y, 0.0);
    p.params3 = float4(0.0, g_PhysicsParams.y, g_PhysicsParams.z, 0.0);
    p.sortKey = float4(0, 0, 0, 0);

    // Write particle
    g_Particles[deadIndex] = p;

    // Increment alive counter
    InterlockedAdd(g_AliveCount[0], 1);
}
