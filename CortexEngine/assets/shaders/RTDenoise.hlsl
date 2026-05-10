Texture2D<float> g_ShadowCurrent : register(t0);
Texture2D<float> g_ShadowHistory : register(t1);

Texture2D<float4> g_ReflectionCurrent : register(t2);
Texture2D<float4> g_ReflectionHistory : register(t3);
Texture2D<float4> g_GICurrent : register(t4);
Texture2D<float4> g_GIHistory : register(t5);

Texture2D<float> g_Depth : register(t6);
Texture2D<float4> g_NormalRoughness : register(t7);
Texture2D<float2> g_Velocity : register(t8);
Texture2D<float4> g_TemporalMask : register(t9);

RWTexture2D<float> g_ShadowOut : register(u0);
RWTexture2D<float4> g_ReflectionOut : register(u1);
RWTexture2D<float4> g_GIOut : register(u2);

static uint2 MapToDepthPixel(uint2 p, uint2 outDim)
{
    uint depthW;
    uint depthH;
    g_Depth.GetDimensions(depthW, depthH);
    float2 uv = (float2(p) + 0.5f) / max(float2(outDim), 1.0f);
    return min(uint2(uv * float2(depthW, depthH)), uint2(depthW - 1u, depthH - 1u));
}

static float3 LoadNormal(uint2 depthPixel)
{
    float3 n = g_NormalRoughness.Load(int3(depthPixel, 0)).xyz * 2.0f - 1.0f;
    const float len2 = max(dot(n, n), 1e-4f);
    return n * rsqrt(len2);
}

static float EdgeWeight(uint2 centerDepthPixel, uint2 sampleDepthPixel)
{
    const float centerDepth = g_Depth.Load(int3(centerDepthPixel, 0));
    const float sampleDepth = g_Depth.Load(int3(sampleDepthPixel, 0));
    const float3 centerNormal = LoadNormal(centerDepthPixel);
    const float3 sampleNormal = LoadNormal(sampleDepthPixel);

    const float depthWeight = exp2(-abs(centerDepth - sampleDepth) * 192.0f);
    const float normalWeight = saturate((dot(centerNormal, sampleNormal) - 0.72f) / 0.28f);
    return depthWeight * normalWeight;
}

static float SpatialShadow(uint2 p, uint2 outDim)
{
    uint w;
    uint h;
    g_ShadowCurrent.GetDimensions(w, h);
    const uint2 centerDepthPixel = MapToDepthPixel(p, outDim);

    float sum = g_ShadowCurrent.Load(int3(p, 0));
    float weightSum = 1.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            const uint2 q = uint2(clamp(int2(p) + int2(x, y), int2(0, 0), int2(int(w) - 1, int(h) - 1)));
            const uint2 sampleDepthPixel = MapToDepthPixel(q, outDim);
            const float weight = EdgeWeight(centerDepthPixel, sampleDepthPixel);
            sum += g_ShadowCurrent.Load(int3(q, 0)) * weight;
            weightSum += weight;
        }
    }

    return saturate(sum / max(weightSum, 1e-4f));
}

static float4 SpatialReflectionColor(uint2 p, uint2 outDim)
{
    uint w;
    uint h;
    g_ReflectionCurrent.GetDimensions(w, h);
    const uint2 centerDepthPixel = MapToDepthPixel(p, outDim);

    float4 sum = g_ReflectionCurrent.Load(int3(p, 0));
    float weightSum = 1.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            const uint2 q = uint2(clamp(int2(p) + int2(x, y), int2(0, 0), int2(int(w) - 1, int(h) - 1)));
            const uint2 sampleDepthPixel = MapToDepthPixel(q, outDim);
            const float weight = EdgeWeight(centerDepthPixel, sampleDepthPixel);
            sum += g_ReflectionCurrent.Load(int3(q, 0)) * weight;
            weightSum += weight;
        }
    }

    return max(sum / max(weightSum, 1e-4f), 0.0f);
}

static float4 SpatialGIColor(uint2 p, uint2 outDim)
{
    uint w;
    uint h;
    g_GICurrent.GetDimensions(w, h);
    const uint2 centerDepthPixel = MapToDepthPixel(p, outDim);

    float4 sum = g_GICurrent.Load(int3(p, 0));
    float weightSum = 1.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            const uint2 q = uint2(clamp(int2(p) + int2(x, y), int2(0, 0), int2(int(w) - 1, int(h) - 1)));
            const uint2 sampleDepthPixel = MapToDepthPixel(q, outDim);
            const float weight = EdgeWeight(centerDepthPixel, sampleDepthPixel);
            sum += g_GICurrent.Load(int3(q, 0)) * weight;
            weightSum += weight;
        }
    }

    return max(sum / max(weightSum, 1e-4f), 0.0f);
}

static uint2 ReprojectHistoryPixel(uint2 p, uint2 outDim, out float reprojectionValid)
{
    const uint2 depthPixel = MapToDepthPixel(p, outDim);
    const float2 velocity = g_Velocity.Load(int3(depthPixel, 0));
    const float2 uv = (float2(p) + 0.5f) / max(float2(outDim), 1.0f);
    const float2 historyUv = uv + velocity;
    reprojectionValid = (historyUv.x >= 0.0f && historyUv.x <= 1.0f &&
                         historyUv.y >= 0.0f && historyUv.y <= 1.0f) ? 1.0f : 0.0f;
    return min(uint2(saturate(historyUv) * float2(outDim)), outDim - 1u);
}

static float HistoryAcceptance(uint2 p, uint2 hp, uint2 outDim)
{
    const uint2 centerDepthPixel = MapToDepthPixel(p, outDim);
    const uint2 historyDepthPixel = MapToDepthPixel(hp, outDim);
    const float centerDepth = g_Depth.Load(int3(centerDepthPixel, 0));
    const float historyDepth = g_Depth.Load(int3(historyDepthPixel, 0));
    const float3 centerNormal = LoadNormal(centerDepthPixel);
    const float3 historyNormal = LoadNormal(historyDepthPixel);
    const float2 velocity = g_Velocity.Load(int3(centerDepthPixel, 0));

    const float depthOk = exp2(-abs(centerDepth - historyDepth) * 160.0f);
    const float normalOk = saturate((dot(centerNormal, historyNormal) - 0.78f) / 0.22f);
    const float speedPixels = length(velocity * float2(outDim));
    const float speedOk = saturate(1.0f - max(speedPixels - 8.0f, 0.0f) / 24.0f);
    return depthOk * normalOk * speedOk;
}

static float SharedTemporalAcceptance(uint2 p, uint2 outDim)
{
    uint maskW;
    uint maskH;
    g_TemporalMask.GetDimensions(maskW, maskH);
    const float2 uv = (float2(p) + 0.5f) / max(float2(outDim), 1.0f);
    const uint2 mp = min(uint2(uv * float2(maskW, maskH)), uint2(maskW - 1u, maskH - 1u));
    return g_TemporalMask.Load(int3(mp, 0)).x;
}

static void StoreShadow(uint3 id, bool useHistory, float alpha)
{
    uint w;
    uint h;
    g_ShadowOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    const uint2 p = id.xy;
    const uint2 outDim = uint2(w, h);
    const float current = SpatialShadow(p, outDim);
    if (!useHistory)
    {
        g_ShadowOut[p] = current;
        return;
    }

    float reprojectionValid = 0.0f;
    const uint2 hp = ReprojectHistoryPixel(p, outDim, reprojectionValid);
    const float history = g_ShadowHistory.Load(int3(hp, 0));
    const float historyWeight = saturate(1.0f - alpha) *
        reprojectionValid *
        HistoryAcceptance(p, hp, outDim) *
        SharedTemporalAcceptance(p, outDim);
    g_ShadowOut[p] = saturate(lerp(current, history, historyWeight));
}

static void StoreReflection(uint3 id, bool useHistory, float alpha)
{
    uint w;
    uint h;
    g_ReflectionOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    const uint2 p = id.xy;
    const uint2 outDim = uint2(w, h);
    const float4 current = SpatialReflectionColor(p, outDim);
    if (!useHistory)
    {
        g_ReflectionOut[p] = current;
        return;
    }

    float reprojectionValid = 0.0f;
    const uint2 hp = ReprojectHistoryPixel(p, outDim, reprojectionValid);
    const float4 history = g_ReflectionHistory.Load(int3(hp, 0));
    const float historyWeight = saturate(1.0f - alpha) *
        reprojectionValid *
        HistoryAcceptance(p, hp, outDim) *
        SharedTemporalAcceptance(p, outDim);
    g_ReflectionOut[p] = max(lerp(current, history, historyWeight), 0.0f);
}

static void StoreGI(uint3 id, bool useHistory, float alpha)
{
    uint w;
    uint h;
    g_GIOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    const uint2 p = id.xy;
    const uint2 outDim = uint2(w, h);
    const float4 current = SpatialGIColor(p, outDim);
    if (!useHistory)
    {
        g_GIOut[p] = current;
        return;
    }

    float reprojectionValid = 0.0f;
    const uint2 hp = ReprojectHistoryPixel(p, outDim, reprojectionValid);
    const float4 history = g_GIHistory.Load(int3(hp, 0));
    const float historyWeight = saturate(1.0f - alpha) *
        reprojectionValid *
        HistoryAcceptance(p, hp, outDim) *
        SharedTemporalAcceptance(p, outDim);
    g_GIOut[p] = max(lerp(current, history, historyWeight), 0.0f);
}

[numthreads(8, 8, 1)]
void ShadowSeedCS(uint3 id : SV_DispatchThreadID)
{
    StoreShadow(id, false, 1.0f);
}

[numthreads(8, 8, 1)]
void ShadowTemporalCS(uint3 id : SV_DispatchThreadID)
{
    StoreShadow(id, true, 0.20f);
}

[numthreads(8, 8, 1)]
void ReflectionSeedCS(uint3 id : SV_DispatchThreadID)
{
    StoreReflection(id, false, 1.0f);
}

[numthreads(8, 8, 1)]
void ReflectionTemporalCS(uint3 id : SV_DispatchThreadID)
{
    StoreReflection(id, true, 0.28f);
}

[numthreads(8, 8, 1)]
void GISeedCS(uint3 id : SV_DispatchThreadID)
{
    StoreGI(id, false, 1.0f);
}

[numthreads(8, 8, 1)]
void GITemporalCS(uint3 id : SV_DispatchThreadID)
{
    StoreGI(id, true, 0.12f);
}
