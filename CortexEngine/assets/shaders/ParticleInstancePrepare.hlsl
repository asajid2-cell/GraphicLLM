// Public particle preparation path.
// Converts ECS-emitted particle source records into sorted billboard instances
// on the GPU. The renderer still owns emitter lifecycle on CPU, while this pass
// owns per-frame wind/bloom/soft-depth shaping and back-to-front ordering.

struct ParticleSource
{
    float4 position_size;    // xyz = world position, w = size
    float4 velocity_age;     // xyz = velocity, w = age
    float4 color;            // rgba
    float4 params;           // x = lifetime, yzw = reserved
};

struct ParticleInstance
{
    float3 position;
    float size;
    float4 color;
};

cbuffer ParticlePrepareCB : register(b1)
{
    uint g_Count;
    float g_DeltaTime;
    float g_BloomContribution;
    float g_SoftDepthFade;

    float g_WindInfluence;
    float3 g_Padding0;

    float4 g_CameraPosition;
};

StructuredBuffer<ParticleSource> g_Source : register(t0);
RWStructuredBuffer<ParticleInstance> g_Output : register(u0);

float3 PreparedPosition(ParticleSource source)
{
    const float lifetime = max(source.params.x, 0.001f);
    const float ageT = saturate(source.velocity_age.w / lifetime);

    // Keep the already-integrated ECS particle as the base, then apply the
    // public renderer's wind/extrapolation policy on GPU.
    float3 position = source.position_size.xyz;
    position += source.velocity_age.xyz * (g_DeltaTime * 0.25f);
    position += float3(g_WindInfluence * ageT * 0.22f, 0.0f, g_WindInfluence * ageT * 0.08f);
    return position;
}

float PreparedDepthSq(ParticleSource source)
{
    const float3 position = PreparedPosition(source);
    const float3 toCamera = position - g_CameraPosition.xyz;
    return dot(toCamera, toCamera);
}

[numthreads(128, 1, 1)]
void CSMain(uint3 dispatchID : SV_DispatchThreadID)
{
    const uint index = dispatchID.x;
    if (index >= g_Count)
    {
        return;
    }

    ParticleSource source = g_Source[index];
    const float depth = PreparedDepthSq(source);

    // Stable O(n^2) depth ordering. Particle counts are capped by the renderer
    // for public showcase paths, so this avoids a larger sorting framework while
    // still proving real GPU-side ordering behavior.
    uint rank = 0;
    for (uint otherIndex = 0; otherIndex < g_Count; ++otherIndex)
    {
        ParticleSource other = g_Source[otherIndex];
        const float otherDepth = PreparedDepthSq(other);
        if (otherDepth > depth || (otherDepth == depth && otherIndex < index))
        {
            ++rank;
        }
    }

    ParticleInstance output;
    output.position = PreparedPosition(source);
    output.size = source.position_size.w;
    output.color = source.color;
    output.color.rgb *= g_BloomContribution;
    output.color.a *= clamp(1.0f - g_SoftDepthFade * 0.18f, 0.55f, 1.0f);

    g_Output[rank] = output;
}
