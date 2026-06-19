// Public GPU particle lifecycle path.
// Expands emitter descriptors into sorted billboard instances without using
// ECS-owned per-particle storage.

struct ParticleEmitter
{
    float4 position_rate;              // xyz = world position, w = rate
    float4 initial_velocity_lifetime;  // xyz = initial velocity, w = lifetime
    float4 velocity_random_gravity;    // xyz = deterministic jitter range, w = gravity
    float4 size_local_type;            // x = start, y = end, z = local flag, w = type
    float4 color_start;
    float4 color_end;
    float4 offset_count_seed;          // x = offset, y = count, z = seed, w = reserved
};

struct ParticleInstance
{
    float3 position;
    float size;
    float4 color;
    float4 params; // x = ageT, y = type, z = opacity curve, w = sparkle seed
};

cbuffer ParticleLifecycleCB : register(b1)
{
    uint g_EmitterCount;
    uint g_ParticleCount;
    float g_Time;
    float g_BloomContribution;

    float g_SoftDepthFade;
    float g_WindInfluence;
    float2 g_Padding0;

    float4 g_CameraPosition;
};

StructuredBuffer<ParticleEmitter> g_Emitters : register(t0);
RWStructuredBuffer<ParticleInstance> g_Output : register(u0);

float Hash01(float value)
{
    return frac(sin(value * 12.9898f + 78.233f) * 43758.5453f);
}

float Noise3(float3 p)
{
    return Hash01(dot(p, float3(37.1f, 57.7f, 91.3f)));
}

float3 CurlNoise(float3 p)
{
    const float e = 0.17f;
    float n1 = Noise3(p + float3(0.0f, e, 0.0f));
    float n2 = Noise3(p - float3(0.0f, e, 0.0f));
    float a = (n1 - n2) / (2.0f * e);

    n1 = Noise3(p + float3(0.0f, 0.0f, e));
    n2 = Noise3(p - float3(0.0f, 0.0f, e));
    float b = (n1 - n2) / (2.0f * e);

    n1 = Noise3(p + float3(e, 0.0f, 0.0f));
    n2 = Noise3(p - float3(e, 0.0f, 0.0f));
    float c = (n1 - n2) / (2.0f * e);

    return normalize(float3(a - b, b - c, c - a) + 1.0e-3f.xxx);
}

float BellCurve(float t)
{
    return saturate(sin(saturate(t) * 3.14159265f));
}

uint FindEmitterIndex(uint particleIndex, out uint localIndex)
{
    [loop]
    for (uint i = 0; i < g_EmitterCount; ++i)
    {
        const ParticleEmitter emitter = g_Emitters[i];
        const uint offset = (uint)emitter.offset_count_seed.x;
        const uint count = (uint)emitter.offset_count_seed.y;
        if (particleIndex >= offset && particleIndex < offset + count)
        {
            localIndex = particleIndex - offset;
            return i;
        }
    }
    localIndex = 0;
    return 0;
}

ParticleInstance BuildParticle(uint particleIndex)
{
    uint localIndex = 0;
    const uint emitterIndex = FindEmitterIndex(particleIndex, localIndex);
    const ParticleEmitter emitter = g_Emitters[emitterIndex];

    const float rate = max(emitter.position_rate.w, 0.001f);
    const float lifetime = max(emitter.initial_velocity_lifetime.w, 0.1f);
    const float spawnStride = 1.0f / rate;
    const float seed = emitter.offset_count_seed.z + (float)localIndex * 17.0f;
    const float spawnOffset = (float)localIndex * spawnStride;
    const float age = fmod(g_Time + lifetime - fmod(spawnOffset, lifetime), lifetime);
    const float ageT = saturate(age / lifetime);

    const float3 jitter = float3(
        Hash01(seed + 1.0f) * 2.0f - 1.0f,
        Hash01(seed + 2.0f) * 2.0f - 1.0f,
        Hash01(seed + 3.0f) * 2.0f - 1.0f) * emitter.velocity_random_gravity.xyz;

    const uint type = (uint)round(emitter.size_local_type.w);
    float3 velocity = emitter.initial_velocity_lifetime.xyz + jitter;
    velocity.y += emitter.velocity_random_gravity.w * age;

    float3 position = emitter.position_rate.xyz + velocity * age;
    position += float3(g_WindInfluence * ageT * 0.22f, 0.0f, g_WindInfluence * ageT * 0.08f);

    float opacityCurve = 1.0f - ageT;
    float sizeCurve = ageT;
    if (type == 2u) // dust motes: suspended, slow curl drift, persistent mid-life sparkle.
    {
        const float3 curl = CurlNoise(position * 0.75f + float3(g_Time * 0.055f, seed * 0.013f, g_Time * 0.037f));
        position += curl * (0.30f + 0.20f * Hash01(seed + 21.0f)) * BellCurve(ageT);
        position.y += sin(g_Time * 0.35f + seed) * 0.035f;
        opacityCurve = saturate(0.28f + 0.72f * BellCurve(ageT));
        sizeCurve = saturate(0.20f + 0.55f * BellCurve(ageT));
    }
    else if (type == 3u || type == 4u) // sparks / embers: buoyant rise, quick flicker.
    {
        const float flicker = 0.58f + 0.42f * Hash01(seed + floor(g_Time * 18.0f) * 7.0f);
        position.y += age * age * (type == 3u ? 1.25f : 0.46f);
        position.xz += CurlNoise(position * 1.6f + seed).xz * ageT * 0.10f;
        opacityCurve = pow(saturate(1.0f - ageT), type == 3u ? 1.7f : 0.85f) * flicker;
        sizeCurve = saturate(1.0f - ageT * (type == 3u ? 0.40f : 0.18f));
    }
    else if (type == 0u || type == 5u) // smoke / mist: soft curl-advection and slow expansion.
    {
        const float3 curl = CurlNoise(position * 0.45f + float3(g_Time * 0.035f, seed * 0.021f, 0.0f));
        position += curl * (type == 5u ? 0.42f : 0.28f) * BellCurve(ageT);
        opacityCurve = pow(BellCurve(ageT), type == 5u ? 1.25f : 1.0f);
        sizeCurve = smoothstep(0.0f, 1.0f, ageT);
    }

    ParticleInstance instance;
    instance.position = position;
    instance.size = lerp(emitter.size_local_type.x, emitter.size_local_type.y, sizeCurve);
    instance.color = lerp(emitter.color_start, emitter.color_end, ageT);
    if (type == 3u || type == 4u)
    {
        instance.color.rgb *= lerp(1.0f, 2.8f, g_BloomContribution);
    }
    instance.color.a *= opacityCurve * clamp(1.0f - g_SoftDepthFade * 0.08f, 0.72f, 1.0f);
    instance.params = float4(ageT, (float)type, opacityCurve, Hash01(seed + 31.0f));
    return instance;
}

float DepthSq(ParticleInstance instance)
{
    const float3 toCamera = instance.position - g_CameraPosition.xyz;
    return dot(toCamera, toCamera);
}

[numthreads(128, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    const uint index = dispatchID.x;
    if (index >= g_ParticleCount)
    {
        return;
    }

    const ParticleInstance instance = BuildParticle(index);
    const float depth = DepthSq(instance);

    uint rank = 0;
    for (uint otherIndex = 0; otherIndex < g_ParticleCount; ++otherIndex)
    {
        const float otherDepth = DepthSq(BuildParticle(otherIndex));
        if (otherDepth > depth || (otherDepth == depth && otherIndex < index))
        {
            ++rank;
        }
    }

    g_Output[rank] = instance;
}
