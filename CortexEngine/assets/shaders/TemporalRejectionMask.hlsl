Texture2D<float> g_Depth : register(t0);
Texture2D<float4> g_NormalRoughness : register(t1);
Texture2D<float2> g_Velocity : register(t2);

RWTexture2D<float4> g_TemporalRejectionMask : register(u0);
RWByteAddressBuffer g_TemporalMaskStats : register(u1);

static const uint TEMPORAL_STATS_SCALE = 256u;
static const uint TEMPORAL_STATS_ACCEPTED_OFFSET = 0u;
static const uint TEMPORAL_STATS_DISOCCLUSION_OFFSET = 4u;
static const uint TEMPORAL_STATS_HIGH_MOTION_OFFSET = 8u;
static const uint TEMPORAL_STATS_OUT_OF_BOUNDS_OFFSET = 12u;
static const uint TEMPORAL_STATS_PIXELS_OFFSET = 16u;

static float3 LoadNormal(uint2 p)
{
    float3 n = g_NormalRoughness.Load(int3(p, 0)).xyz * 2.0f - 1.0f;
    return normalize(n + 1e-5f);
}

[numthreads(8, 8, 1)]
void BuildTemporalRejectionMaskCS(uint3 id : SV_DispatchThreadID)
{
    uint width;
    uint height;
    g_TemporalRejectionMask.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }

    const uint2 p = id.xy;
    const float2 dim = max(float2(width, height), 1.0f);
    const float2 uv = (float2(p) + 0.5f) / dim;
    const float2 velocity = g_Velocity.Load(int3(p, 0));
    const float2 historyUv = uv + velocity;
    const bool inBounds =
        historyUv.x >= 0.0f && historyUv.x <= 1.0f &&
        historyUv.y >= 0.0f && historyUv.y <= 1.0f;
    const uint2 hp = min(uint2(saturate(historyUv) * dim), uint2(width - 1u, height - 1u));

    const float centerDepth = g_Depth.Load(int3(p, 0));
    const float historyDepth = g_Depth.Load(int3(hp, 0));
    const float3 centerNormal = LoadNormal(p);
    const float3 historyNormal = LoadNormal(hp);
    const float speedPixels = length(velocity * dim);

    const float depthAcceptance = exp2(-abs(centerDepth - historyDepth) * 160.0f);
    const float normalAcceptance = saturate((dot(centerNormal, historyNormal) - 0.78f) / 0.22f);
    const float motionAcceptance = saturate(1.0f - max(speedPixels - 2.0f, 0.0f) / 22.0f);
    const float boundsAcceptance = inBounds ? 1.0f : 0.0f;
    const float acceptance = depthAcceptance * normalAcceptance * motionAcceptance * boundsAcceptance;

    // x = accepted-history weight multiplier.
    // y = disocclusion rejection strength.
    // z = high-motion rejection strength.
    // w = reprojection in-bounds flag.
    g_TemporalRejectionMask[p] = float4(
        acceptance,
        1.0f - saturate(depthAcceptance * normalAcceptance * boundsAcceptance),
        1.0f - motionAcceptance,
        boundsAcceptance);
}

[numthreads(1, 1, 1)]
void ClearTemporalMaskStatsCS(uint3 id : SV_DispatchThreadID)
{
    g_TemporalMaskStats.Store(TEMPORAL_STATS_ACCEPTED_OFFSET, 0u);
    g_TemporalMaskStats.Store(TEMPORAL_STATS_DISOCCLUSION_OFFSET, 0u);
    g_TemporalMaskStats.Store(TEMPORAL_STATS_HIGH_MOTION_OFFSET, 0u);
    g_TemporalMaskStats.Store(TEMPORAL_STATS_OUT_OF_BOUNDS_OFFSET, 0u);
    g_TemporalMaskStats.Store(TEMPORAL_STATS_PIXELS_OFFSET, 0u);
}

groupshared uint g_AcceptedGroup[64];
groupshared uint g_DisocclusionGroup[64];
groupshared uint g_HighMotionGroup[64];
groupshared uint g_OutOfBoundsGroup[64];
groupshared uint g_PixelGroup[64];

[numthreads(8, 8, 1)]
void ReduceTemporalMaskStatsCS(uint3 id : SV_DispatchThreadID,
                               uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    g_NormalRoughness.GetDimensions(width, height);

    const uint lane = groupThreadId.y * 8u + groupThreadId.x;
    uint accepted = 0u;
    uint disocclusion = 0u;
    uint highMotion = 0u;
    uint outOfBounds = 0u;
    uint pixels = 0u;

    if (id.x < width && id.y < height)
    {
        const float4 mask = saturate(g_NormalRoughness.Load(int3(id.xy, 0)));
        accepted = (uint)round(mask.x * TEMPORAL_STATS_SCALE);
        disocclusion = (uint)round(mask.y * TEMPORAL_STATS_SCALE);
        highMotion = (uint)round(mask.z * TEMPORAL_STATS_SCALE);
        outOfBounds = (uint)round((1.0f - mask.w) * TEMPORAL_STATS_SCALE);
        pixels = 1u;
    }

    g_AcceptedGroup[lane] = accepted;
    g_DisocclusionGroup[lane] = disocclusion;
    g_HighMotionGroup[lane] = highMotion;
    g_OutOfBoundsGroup[lane] = outOfBounds;
    g_PixelGroup[lane] = pixels;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride)
        {
            g_AcceptedGroup[lane] += g_AcceptedGroup[lane + stride];
            g_DisocclusionGroup[lane] += g_DisocclusionGroup[lane + stride];
            g_HighMotionGroup[lane] += g_HighMotionGroup[lane + stride];
            g_OutOfBoundsGroup[lane] += g_OutOfBoundsGroup[lane + stride];
            g_PixelGroup[lane] += g_PixelGroup[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane == 0u)
    {
        g_TemporalMaskStats.InterlockedAdd(TEMPORAL_STATS_ACCEPTED_OFFSET, g_AcceptedGroup[0]);
        g_TemporalMaskStats.InterlockedAdd(TEMPORAL_STATS_DISOCCLUSION_OFFSET, g_DisocclusionGroup[0]);
        g_TemporalMaskStats.InterlockedAdd(TEMPORAL_STATS_HIGH_MOTION_OFFSET, g_HighMotionGroup[0]);
        g_TemporalMaskStats.InterlockedAdd(TEMPORAL_STATS_OUT_OF_BOUNDS_OFFSET, g_OutOfBoundsGroup[0]);
        g_TemporalMaskStats.InterlockedAdd(TEMPORAL_STATS_PIXELS_OFFSET, g_PixelGroup[0]);
    }
}
