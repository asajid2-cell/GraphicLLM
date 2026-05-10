Texture2D<float4> g_RTReflection : register(t0);
Texture2D<float4> g_RTReflectionHistory : register(t1);
RWByteAddressBuffer g_RTReflectionStats : register(u0);
RWByteAddressBuffer g_RTReflectionHistoryStats : register(u1);

static const uint RT_REFL_STATS_SCALE = 256u;
static const uint RT_REFL_STATS_LUMA_SUM_OFFSET = 0u;
static const uint RT_REFL_STATS_NONZERO_OFFSET = 4u;
static const uint RT_REFL_STATS_BRIGHT_OFFSET = 8u;
static const uint RT_REFL_STATS_PIXELS_OFFSET = 12u;
static const uint RT_REFL_STATS_MAX_LUMA_OFFSET = 16u;
static const uint RT_REFL_STATS_OUTLIER_OFFSET = 20u;

groupshared uint g_LumaSumGroup[64];
groupshared uint g_NonZeroGroup[64];
groupshared uint g_BrightGroup[64];
groupshared uint g_PixelGroup[64];
groupshared uint g_MaxLumaGroup[64];
groupshared uint g_OutlierGroup[64];

[numthreads(1, 1, 1)]
void ClearRTReflectionSignalStatsCS(uint3 id : SV_DispatchThreadID)
{
    g_RTReflectionStats.Store(RT_REFL_STATS_LUMA_SUM_OFFSET, 0u);
    g_RTReflectionStats.Store(RT_REFL_STATS_NONZERO_OFFSET, 0u);
    g_RTReflectionStats.Store(RT_REFL_STATS_BRIGHT_OFFSET, 0u);
    g_RTReflectionStats.Store(RT_REFL_STATS_PIXELS_OFFSET, 0u);
    g_RTReflectionStats.Store(RT_REFL_STATS_MAX_LUMA_OFFSET, 0u);
    g_RTReflectionStats.Store(RT_REFL_STATS_OUTLIER_OFFSET, 0u);
}

[numthreads(1, 1, 1)]
void ClearRTReflectionHistorySignalStatsCS(uint3 id : SV_DispatchThreadID)
{
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_LUMA_SUM_OFFSET, 0u);
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_NONZERO_OFFSET, 0u);
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_BRIGHT_OFFSET, 0u);
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_PIXELS_OFFSET, 0u);
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_MAX_LUMA_OFFSET, 0u);
    g_RTReflectionHistoryStats.Store(RT_REFL_STATS_OUTLIER_OFFSET, 0u);
}

[numthreads(8, 8, 1)]
void ReduceRTReflectionSignalStatsCS(uint3 id : SV_DispatchThreadID,
                                     uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    g_RTReflection.GetDimensions(width, height);

    const uint lane = groupThreadId.y * 8u + groupThreadId.x;
    uint lumaSum = 0u;
    uint nonZero = 0u;
    uint bright = 0u;
    uint pixels = 0u;
    uint maxLuma = 0u;
    uint outlier = 0u;

    if (id.x < width && id.y < height)
    {
        const float3 color = max(g_RTReflection.Load(int3(id.xy, 0)).rgb, 0.0f);
        const float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        const float finiteLuma = isfinite(luma) ? luma : 0.0f;
        lumaSum = (uint)round(saturate(finiteLuma) * RT_REFL_STATS_SCALE);
        nonZero = finiteLuma > (1.0f / 1024.0f) ? 1u : 0u;
        bright = finiteLuma > 0.05f ? 1u : 0u;
        pixels = 1u;
        maxLuma = (uint)round(min(finiteLuma, 16.0f) * RT_REFL_STATS_SCALE);
        outlier = finiteLuma > 4.0f ? 1u : 0u;
    }

    g_LumaSumGroup[lane] = lumaSum;
    g_NonZeroGroup[lane] = nonZero;
    g_BrightGroup[lane] = bright;
    g_PixelGroup[lane] = pixels;
    g_MaxLumaGroup[lane] = maxLuma;
    g_OutlierGroup[lane] = outlier;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride)
        {
            g_LumaSumGroup[lane] += g_LumaSumGroup[lane + stride];
            g_NonZeroGroup[lane] += g_NonZeroGroup[lane + stride];
            g_BrightGroup[lane] += g_BrightGroup[lane + stride];
            g_PixelGroup[lane] += g_PixelGroup[lane + stride];
            g_MaxLumaGroup[lane] = max(g_MaxLumaGroup[lane], g_MaxLumaGroup[lane + stride]);
            g_OutlierGroup[lane] += g_OutlierGroup[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane == 0u)
    {
        g_RTReflectionStats.InterlockedAdd(RT_REFL_STATS_LUMA_SUM_OFFSET, g_LumaSumGroup[0]);
        g_RTReflectionStats.InterlockedAdd(RT_REFL_STATS_NONZERO_OFFSET, g_NonZeroGroup[0]);
        g_RTReflectionStats.InterlockedAdd(RT_REFL_STATS_BRIGHT_OFFSET, g_BrightGroup[0]);
        g_RTReflectionStats.InterlockedAdd(RT_REFL_STATS_PIXELS_OFFSET, g_PixelGroup[0]);
        g_RTReflectionStats.InterlockedMax(RT_REFL_STATS_MAX_LUMA_OFFSET, g_MaxLumaGroup[0]);
        g_RTReflectionStats.InterlockedAdd(RT_REFL_STATS_OUTLIER_OFFSET, g_OutlierGroup[0]);
    }
}

[numthreads(8, 8, 1)]
void ReduceRTReflectionHistorySignalStatsCS(uint3 id : SV_DispatchThreadID,
                                            uint3 groupThreadId : SV_GroupThreadID)
{
    uint width;
    uint height;
    g_RTReflectionHistory.GetDimensions(width, height);

    const uint lane = groupThreadId.y * 8u + groupThreadId.x;
    uint lumaSum = 0u;
    uint nonZero = 0u;
    uint bright = 0u;
    uint pixels = 0u;
    uint maxLuma = 0u;
    uint outlier = 0u;

    if (id.x < width && id.y < height)
    {
        const float3 color = max(g_RTReflectionHistory.Load(int3(id.xy, 0)).rgb, 0.0f);
        const float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        const float finiteLuma = isfinite(luma) ? luma : 0.0f;
        lumaSum = (uint)round(saturate(finiteLuma) * RT_REFL_STATS_SCALE);
        nonZero = finiteLuma > (1.0f / 1024.0f) ? 1u : 0u;
        bright = finiteLuma > 0.05f ? 1u : 0u;
        pixels = 1u;
        maxLuma = (uint)round(min(finiteLuma, 16.0f) * RT_REFL_STATS_SCALE);
        outlier = finiteLuma > 4.0f ? 1u : 0u;
    }

    g_LumaSumGroup[lane] = lumaSum;
    g_NonZeroGroup[lane] = nonZero;
    g_BrightGroup[lane] = bright;
    g_PixelGroup[lane] = pixels;
    g_MaxLumaGroup[lane] = maxLuma;
    g_OutlierGroup[lane] = outlier;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride)
        {
            g_LumaSumGroup[lane] += g_LumaSumGroup[lane + stride];
            g_NonZeroGroup[lane] += g_NonZeroGroup[lane + stride];
            g_BrightGroup[lane] += g_BrightGroup[lane + stride];
            g_PixelGroup[lane] += g_PixelGroup[lane + stride];
            g_MaxLumaGroup[lane] = max(g_MaxLumaGroup[lane], g_MaxLumaGroup[lane + stride]);
            g_OutlierGroup[lane] += g_OutlierGroup[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane == 0u)
    {
        g_RTReflectionHistoryStats.InterlockedAdd(RT_REFL_STATS_LUMA_SUM_OFFSET, g_LumaSumGroup[0]);
        g_RTReflectionHistoryStats.InterlockedAdd(RT_REFL_STATS_NONZERO_OFFSET, g_NonZeroGroup[0]);
        g_RTReflectionHistoryStats.InterlockedAdd(RT_REFL_STATS_BRIGHT_OFFSET, g_BrightGroup[0]);
        g_RTReflectionHistoryStats.InterlockedAdd(RT_REFL_STATS_PIXELS_OFFSET, g_PixelGroup[0]);
        g_RTReflectionHistoryStats.InterlockedMax(RT_REFL_STATS_MAX_LUMA_OFFSET, g_MaxLumaGroup[0]);
        g_RTReflectionHistoryStats.InterlockedAdd(RT_REFL_STATS_OUTLIER_OFFSET, g_OutlierGroup[0]);
    }
}
