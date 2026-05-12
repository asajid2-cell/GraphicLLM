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

    float3 velocity = emitter.initial_velocity_lifetime.xyz + jitter;
    velocity.y += emitter.velocity_random_gravity.w * age;

    float3 position = emitter.position_rate.xyz + velocity * age;
    position += float3(g_WindInfluence * ageT * 0.22f, 0.0f, g_WindInfluence * ageT * 0.08f);

    ParticleInstance instance;
    instance.position = position;
    instance.size = lerp(emitter.size_local_type.x, emitter.size_local_type.y, ageT);
    instance.color = lerp(emitter.color_start, emitter.color_end, ageT);
    instance.color.rgb *= g_BloomContribution;
    instance.color.a *= clamp(1.0f - g_SoftDepthFade * 0.18f, 0.55f, 1.0f);
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
