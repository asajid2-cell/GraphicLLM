// ParticleSimulate.hlsl
// Compute shader for GPU particle simulation.
// Updates particle physics, collision, and lifetime.

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

// Force field structure
struct ForceField {
    float3 position;
    float radius;
    float strength;
    float falloff;
    float2 padding;
};

// Simulation constant buffer
cbuffer SimulateCB : register(b0) {
    float4 g_Gravity;           // xyz=gravity, w=deltaTime
    float4 g_Wind;              // xyz=wind, w=time
    float4 g_NoiseParams;       // x=strength, y=frequency, z=speed, w=unused
    float4 g_CollisionParams;   // x=bounce, y=friction, z=lifeLoss, w=enabled
    float4 g_StartColor;
    float4 g_EndColor;
    float4 g_CameraPosition;

    uint g_MaxParticles;
    uint g_NumForceFields;
    uint g_UseColorOverLife;
    uint g_Padding;
};

// Particle buffers
RWStructuredBuffer<Particle> g_Particles : register(u0);

// Dead particle indices (append buffer)
AppendStructuredBuffer<uint> g_DeadList : register(u1);

// Alive particle counter
RWStructuredBuffer<uint> g_AliveCount : register(u2);

// Force fields
StructuredBuffer<ForceField> g_ForceFields : register(t0);

// Depth texture for screen-space collision
Texture2D<float> g_DepthTexture : register(t1);
SamplerState g_PointSampler : register(s0);

// Heightfield for terrain collision
Texture2D<float> g_HeightField : register(t2);
SamplerState g_LinearSampler : register(s1);

// Heightfield parameters
cbuffer HeightFieldCB : register(b1) {
    float4 g_HeightFieldParams;     // x=minX, y=minZ, z=sizeX, w=sizeZ
    float4 g_HeightFieldScale;      // x=heightScale, y=heightOffset, zw=unused
};

// Simplex noise for turbulence
float3 mod289(float3 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

float4 mod289(float4 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

float4 permute(float4 x) {
    return mod289(((x * 34.0) + 1.0) * x);
}

float4 taylorInvSqrt(float4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float SimplexNoise3D(float3 v) {
    const float2 C = float2(1.0 / 6.0, 1.0 / 3.0);
    const float4 D = float4(0.0, 0.5, 1.0, 2.0);

    float3 i = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    float3 g = step(x0.yzx, x0.xyz);
    float3 l = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy;
    float3 x3 = x0 - D.yyy;

    i = mod289(i);
    float4 p = permute(permute(permute(
        i.z + float4(0.0, i1.z, i2.z, 1.0)) +
        i.y + float4(0.0, i1.y, i2.y, 1.0)) +
        i.x + float4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    float3 ns = n_ * D.wyz - D.xzx;

    float4 j = p - 49.0 * floor(p * ns.z * ns.z);

    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0 * x_);

    float4 x = x_ * ns.x + ns.yyyy;
    float4 y = y_ * ns.x + ns.yyyy;
    float4 h = 1.0 - abs(x) - abs(y);

    float4 b0 = float4(x.xy, y.xy);
    float4 b1 = float4(x.zw, y.zw);

    float4 s0 = floor(b0) * 2.0 + 1.0;
    float4 s1 = floor(b1) * 2.0 + 1.0;
    float4 sh = -step(h, float4(0, 0, 0, 0));

    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);

    float4 norm = taylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, float4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// Curl noise for turbulence
float3 CurlNoise(float3 pos, float time) {
    float eps = 0.01;

    float3 p = pos * g_NoiseParams.y + time * g_NoiseParams.z;

    float n1 = SimplexNoise3D(p + float3(eps, 0, 0));
    float n2 = SimplexNoise3D(p - float3(eps, 0, 0));
    float n3 = SimplexNoise3D(p + float3(0, eps, 0));
    float n4 = SimplexNoise3D(p - float3(0, eps, 0));
    float n5 = SimplexNoise3D(p + float3(0, 0, eps));
    float n6 = SimplexNoise3D(p - float3(0, 0, eps));

    float dx = (n3 - n4 - n5 + n6) / (2.0 * eps);
    float dy = (n5 - n6 - n1 + n2) / (2.0 * eps);
    float dz = (n1 - n2 - n3 + n4) / (2.0 * eps);

    return float3(dx, dy, dz);
}

// Sample heightfield for terrain collision
float SampleHeightField(float3 worldPos) {
    float2 uv;
    uv.x = (worldPos.x - g_HeightFieldParams.x) / g_HeightFieldParams.z;
    uv.y = (worldPos.z - g_HeightFieldParams.y) / g_HeightFieldParams.w;

    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) {
        return -1000.0;  // Off heightfield
    }

    float height = g_HeightField.SampleLevel(g_LinearSampler, uv, 0).r;
    return height * g_HeightFieldScale.x + g_HeightFieldScale.y;
}

// Calculate force field contribution
float3 CalculateForceFieldForce(float3 position) {
    float3 totalForce = float3(0, 0, 0);

    for (uint i = 0; i < g_NumForceFields; i++) {
        ForceField field = g_ForceFields[i];

        float3 toField = field.position - position;
        float distance = length(toField);

        if (distance < field.radius && distance > 0.001) {
            float falloff = pow(1.0 - distance / field.radius, field.falloff);
            float3 direction = toField / distance;
            totalForce += direction * field.strength * falloff;
        }
    }

    return totalForce;
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID) {
    uint particleIndex = dispatchID.x;

    if (particleIndex >= g_MaxParticles) {
        return;
    }

    Particle p = g_Particles[particleIndex];

    // Check if particle is dead
    if (p.params.y < 0.0) {
        return;
    }

    float deltaTime = g_Gravity.w;
    float time = g_Wind.w;

    // Update age
    p.params.x += deltaTime;

    // Check if particle should die
    if (p.params.x >= p.params.y) {
        p.params.y = -1.0;  // Mark as dead
        g_Particles[particleIndex] = p;
        g_DeadList.Append(particleIndex);
        InterlockedAdd(g_AliveCount[0], -1);
        return;
    }

    float normalizedAge = p.params.x / p.params.y;

    // Get position and velocity
    float3 pos = p.position.xyz;
    float3 vel = p.velocity.xyz;

    // Apply gravity
    float3 gravity = g_Gravity.xyz * p.params3.y;
    vel += gravity * deltaTime;

    // Apply wind
    vel += g_Wind.xyz * deltaTime;

    // Apply force fields
    float3 forceFieldForce = CalculateForceFieldForce(pos);
    vel += forceFieldForce * deltaTime;

    // Apply curl noise turbulence
    if (g_NoiseParams.x > 0.0) {
        float3 noise = CurlNoise(pos, time);
        vel += noise * g_NoiseParams.x * deltaTime;
    }

    // Apply drag
    float drag = p.params3.z;
    vel *= 1.0 / (1.0 + drag * deltaTime);

    // Update position
    pos += vel * deltaTime;

    // Terrain collision
    if (g_CollisionParams.w > 0.5) {
        float groundHeight = SampleHeightField(pos);

        if (pos.y < groundHeight + 0.01) {
            pos.y = groundHeight + 0.01;

            // Reflect velocity (assuming up normal)
            float3 normal = float3(0, 1, 0);
            float3 reflected = reflect(vel, normal);

            // Apply bounce
            vel = reflected * g_CollisionParams.x;

            // Apply friction
            float3 tangent = vel - dot(vel, normal) * normal;
            vel -= tangent * g_CollisionParams.y;

            // Reduce lifetime
            p.params.y *= (1.0 - g_CollisionParams.z);
        }
    }

    // Update rotation
    p.velocity.w += p.params2.x * deltaTime;

    // Update size over lifetime
    float size = lerp(p.params2.y, p.params2.z, normalizedAge);
    p.position.w = size;

    // Update color over lifetime
    if (g_UseColorOverLife > 0) {
        p.color = lerp(g_StartColor, g_EndColor, normalizedAge);
    }
    p.params3.x = normalizedAge;

    // Update sort key (distance to camera squared)
    float3 toCamera = pos - g_CameraPosition.xyz;
    p.sortKey.x = dot(toCamera, toCamera);

    // Write back
    p.position.xyz = pos;
    p.velocity.xyz = vel;
    g_Particles[particleIndex] = p;
}
