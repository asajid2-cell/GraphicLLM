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

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    float4   g_TimeAndExposure;
    float4   g_AmbientColor;
    uint4    g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    float4   g_FogExtraParams;
    float4   g_AOParams;
    float4   g_BloomParams;
};

static float DecodeReflectionDenoiseAlpha()
{
    uint postFxFlags = (uint)(g_BloomParams.w + 0.5f);
    return max((float)((postFxFlags >> 16u) & 255u) * (1.0f / 255.0f), 0.02f);
}

static float Luma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

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

static float LoadRoughness(uint2 depthPixel)
{
    return saturate(g_NormalRoughness.Load(int3(depthPixel, 0)).w);
}

static float EdgeWeight(uint2 centerDepthPixel, uint2 sampleDepthPixel)
{
    const float centerDepth = g_Depth.Load(int3(centerDepthPixel, 0));
    const float sampleDepth = g_Depth.Load(int3(sampleDepthPixel, 0));
    const float3 centerNormal = LoadNormal(centerDepthPixel);
    const float3 sampleNormal = LoadNormal(sampleDepthPixel);
    const float centerRoughness = LoadRoughness(centerDepthPixel);

    const float depthWeight = exp2(-abs(centerDepth - sampleDepth) * lerp(224.0f, 72.0f, centerRoughness));
    const float normalFloor = lerp(0.82f, 0.48f, centerRoughness);
    const float normalWeight = saturate((dot(centerNormal, sampleNormal) - normalFloor) / max(1.0f - normalFloor, 1.0e-3f));
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
    const float centerRoughness = LoadRoughness(centerDepthPixel);

    float4 center = max(g_ReflectionCurrent.Load(int3(p, 0)), 0.0f);
    float4 sum = center;
    float3 moment2 = center.rgb * center.rgb;
    float weightSum = 1.0f;
    const float radius = lerp(1.0f, 4.25f, smoothstep(0.18f, 0.78f, centerRoughness));

    [unroll]
    for (int y = -4; y <= 4; ++y)
    {
        [unroll]
        for (int x = -4; x <= 4; ++x)
        {
            if (x == 0 && y == 0) continue;
            if (max(abs(x), abs(y)) > radius + 0.25f) continue;
            const uint2 q = uint2(clamp(int2(p) + int2(x, y), int2(0, 0), int2(int(w) - 1, int(h) - 1)));
            const uint2 sampleDepthPixel = MapToDepthPixel(q, outDim);
            float spatial = exp2(-dot(float2((float)x, (float)y), float2((float)x, (float)y)) /
                                 max(radius * radius, 0.5f));
            const float weight = EdgeWeight(centerDepthPixel, sampleDepthPixel) * spatial;
            float4 sampleColor = max(g_ReflectionCurrent.Load(int3(q, 0)), 0.0f);
            const float roughClamp = smoothstep(0.32f, 0.78f, centerRoughness);
            const float sampleLimit = lerp(64.0f, 2.6f, roughClamp) * (Luma(center.rgb) + 0.06f);
            const float sampleLuma = Luma(sampleColor.rgb);
            sampleColor.rgb *= min(1.0f, sampleLimit / max(sampleLuma, 1.0e-4f));
            sum += sampleColor * weight;
            moment2 += sampleColor.rgb * sampleColor.rgb * weight;
            weightSum += weight;
        }
    }

    float4 mean = sum / max(weightSum, 1e-4f);
    float3 variance = max(moment2 / max(weightSum, 1e-4f) - mean.rgb * mean.rgb, 0.0f.xxx);
    float sigmaScale = lerp(1.05f, 1.10f, centerRoughness);
    float3 sigma = sqrt(variance + 1.0e-5f.xxx) * sigmaScale;
    float3 clampedCenter = clamp(center.rgb, mean.rgb - sigma, mean.rgb + sigma);
    float preserveSharp = 1.0f - smoothstep(0.18f, 0.42f, centerRoughness);
    mean.rgb = lerp(mean.rgb, clampedCenter, preserveSharp);
    return max(mean, 0.0f);
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
    const float lumaDelta = abs(dot(current.rgb - history.rgb, float3(0.2126f, 0.7152f, 0.0722f)));
    const float roughness = LoadRoughness(MapToDepthPixel(p, outDim));
    const float roughHistory = smoothstep(0.34f, 0.78f, roughness);
    const float varianceAccept = max(saturate(1.0f - lumaDelta / lerp(0.35f, 1.65f, roughness)),
                                     roughHistory * 0.62f);
    const float temporalAcceptance =
        reprojectionValid *
        HistoryAcceptance(p, hp, outDim) *
        lerp(SharedTemporalAcceptance(p, outDim), 1.0f, roughHistory * 0.35f) *
        varianceAccept;
    const float historyWeight = max(saturate(1.0f - alpha) * temporalAcceptance,
                                    roughHistory * 0.82f * temporalAcceptance);
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
    StoreReflection(id, true, DecodeReflectionDenoiseAlpha());
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
