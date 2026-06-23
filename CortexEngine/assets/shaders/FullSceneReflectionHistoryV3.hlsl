// ReflectionV3 history/stability pass.
//
// This pass owns current/previous history and a reprojection validity mask.
// It intentionally does not loosen source admission policy; later resolver
// slices can consume this validity once packet evidence proves it is stable.

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
    float4   g_TAAParams;
};

Texture2D<float4> g_ReflectionRadiance : register(t0);
Texture2D<float4> g_ReflectionSourceId : register(t1);
Texture2D<float4> g_ReflectionTemporalDelta : register(t2);
Texture2D<float4> g_ReflectionHistoryPrev : register(t3);
Texture2D<float4> g_ReflectionHistoryPrevSourceId : register(t4);
Texture2D<float> g_Depth : register(t5);
Texture2D<float4> g_NormalRoughness : register(t6);
Texture2D<float2> g_Velocity : register(t7);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 historyCurr : SV_Target0;
    float4 historyValidity : SV_Target1;
    float4 historyRejection : SV_Target2;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

static float3 LoadNormal(uint2 p) {
    float3 n = g_NormalRoughness.Load(int3(p, 0)).xyz * 2.0f - 1.0f;
    return normalize(n + 1e-5f);
}

static float3 SampleNormal(float2 uv) {
    float3 n = g_NormalRoughness.Sample(g_LinearClamp, uv).xyz * 2.0f - 1.0f;
    return normalize(n + 1e-5f);
}

static float3 ClampRadianceLuma(float3 radiance, float maxLuma) {
    float luma = max(Luma(radiance), 1.0e-4f);
    return radiance * min(1.0f, maxLuma / luma);
}

static void CurrentRadianceNeighborhoodStats(uint2 p,
                                             uint2 maxPixel,
                                             out float3 minRadiance,
                                             out float3 maxRadiance,
                                             out float3 meanRadiance) {
    float3 center = max(g_ReflectionRadiance.Load(int3(p, 0)).rgb, 0.0f.xxx);
    minRadiance = center;
    maxRadiance = center;
    meanRadiance = 0.0f.xxx;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            int2 qInt = clamp(int2(p) + int2(x, y), int2(0, 0), int2(maxPixel));
            float3 sampleRadiance = max(g_ReflectionRadiance.Load(int3(uint2(qInt), 0)).rgb, 0.0f.xxx);
            minRadiance = min(minRadiance, sampleRadiance);
            maxRadiance = max(maxRadiance, sampleRadiance);
            meanRadiance += sampleRadiance;
        }
    }

    meanRadiance *= (1.0f / 9.0f);
}

PSOutput PSMain(VSOutput input) {
    uint width;
    uint height;
    g_ReflectionRadiance.GetDimensions(width, height);
    float2 dim = max(float2(width, height), 1.0f);
    uint2 maxPixel = uint2(width - 1u, height - 1u);
    uint2 p = min(uint2(input.position.xy), maxPixel);
    float2 velocity = g_Velocity.Load(int3(p, 0));
    float2 historyUv = input.texCoord + velocity + g_TAAParams.xy;
    bool inBounds =
        historyUv.x >= 0.0f && historyUv.x <= 1.0f &&
        historyUv.y >= 0.0f && historyUv.y <= 1.0f;
    uint2 hp = min(uint2(saturate(historyUv) * dim), uint2(width - 1u, height - 1u));

    // Current-frame reflection resources are pixel-aligned. Load them exactly
    // so source class, confidence, and temporal debt are not blended across
    // neighboring pixels during mouse jitter.
    float4 radiance = g_ReflectionRadiance.Load(int3(p, 0));
    float4 sourceId = g_ReflectionSourceId.Load(int3(p, 0));
    float4 temporalDelta = g_ReflectionTemporalDelta.Load(int3(p, 0));
    float4 historyPrev = g_ReflectionHistoryPrev.Sample(g_LinearClamp, historyUv);
    float4 historyPrevSourceId = g_ReflectionHistoryPrevSourceId.Sample(g_LinearClamp, historyUv);

    float centerDepth = g_Depth.Load(int3(p, 0));
    float historyDepth = g_Depth.Sample(g_LinearClamp, saturate(historyUv));
    float3 centerNormal = LoadNormal(p);
    float3 historyNormal = SampleNormal(saturate(historyUv));
    float speedPixels = length(velocity * dim);

    float depthAcceptance = exp2(-abs(centerDepth - historyDepth) * 96.0f);
    float normalAcceptance = saturate((dot(centerNormal, historyNormal) - 0.62f) / 0.38f);
    float motionAcceptance = saturate(1.0f - max(speedPixels - 4.0f, 0.0f) / 56.0f);
    float boundsAcceptance = inBounds ? 1.0f : 0.0f;
    float disocclusionRejection = 1.0f - saturate(depthAcceptance * normalAcceptance * boundsAcceptance);
    float highMotionRejection = 1.0f - motionAcceptance;
    float reprojectionAcceptance =
        saturate(depthAcceptance * normalAcceptance * motionAcceptance * boundsAcceptance);

    float confidence = saturate(radiance.a);
    float currentActivity = confidence;
    float active = currentActivity;
    float sourceClass = saturate(sourceId.r);
    float prevConfidence = saturate(historyPrev.a);
    float previousHistoryStrength = prevConfidence;
    float previousHistoryAvailable = smoothstep(0.001f, 0.025f, previousHistoryStrength);
    float historySupport = saturate(max(confidence, previousHistoryStrength));
    float historyRequiredButMissing = saturate(temporalDelta.b);
    float forcedUnavailable = saturate(temporalDelta.g);
    float historyReusable = previousHistoryStrength * reprojectionAcceptance;
    float reprojectionRejected = historySupport * (1.0f - reprojectionAcceptance);
    float previousSourceClass = saturate(historyPrevSourceId.r);
    float previousSourceAvailable = smoothstep(0.001f, 0.025f, saturate(historyPrevSourceId.a + previousSourceClass));
    float sourceSwitch = previousSourceAvailable * previousHistoryStrength *
        smoothstep(0.04f, 0.16f, abs(sourceClass - previousSourceClass));
    disocclusionRejection *= historySupport;
    highMotionRejection *= historySupport;
    float outOfBoundsRejection = historySupport * (1.0f - boundsAcceptance);

    float3 currentRadiance = max(radiance.rgb, 0.0f.xxx);
    float3 historyRadiance = max(historyPrev.rgb, 0.0f.xxx);
    float3 neighborhoodMin;
    float3 neighborhoodMax;
    float3 neighborhoodMean;
    CurrentRadianceNeighborhoodStats(p, maxPixel, neighborhoodMin, neighborhoodMax, neighborhoodMean);

    float neighborhoodMeanLuma = max(Luma(neighborhoodMean), 1.0e-4f);
    float neighborhoodMinLuma = min(Luma(neighborhoodMin), neighborhoodMeanLuma);
    float currentLuma = Luma(currentRadiance);
    float currentOutlierLimit = neighborhoodMeanLuma * lerp(1.25f, 1.80f, confidence) + 0.030f;
    currentOutlierLimit = min(currentOutlierLimit, neighborhoodMinLuma + lerp(0.18f, 0.45f, confidence));
    float currentFirefly = smoothstep(currentOutlierLimit * 0.90f, currentOutlierLimit * 1.35f, currentLuma);
    currentFirefly *= lerp(1.0f, 0.62f, confidence);
    currentRadiance = lerp(
        currentRadiance,
        ClampRadianceLuma(currentRadiance, currentOutlierLimit),
        currentFirefly);

    float3 clampedHistoryRadiance = clamp(historyRadiance, neighborhoodMin, neighborhoodMax);
    float historyOutlierLimit = neighborhoodMeanLuma * lerp(1.30f, 1.95f, prevConfidence) + 0.035f;
    historyOutlierLimit = min(historyOutlierLimit, neighborhoodMinLuma + lerp(0.16f, 0.42f, prevConfidence));
    clampedHistoryRadiance = ClampRadianceLuma(clampedHistoryRadiance, historyOutlierLimit);

    float resetGuard = saturate((1.0f - sourceSwitch) * (1.0f - forcedUnavailable) * (1.0f - historyRequiredButMissing));
    float historyBlend = saturate(historyReusable * resetGuard) * 0.88f * (1.0f - 0.82f * currentFirefly);
    float3 accumulatedRadiance = lerp(currentRadiance, clampedHistoryRadiance, historyBlend);
    float filteredConfidence = confidence * (1.0f - 0.72f * currentFirefly);
    float accumulatedConfidence = saturate(max(filteredConfidence, historyReusable));

    PSOutput output;
    output.historyCurr = float4(accumulatedRadiance, accumulatedConfidence);
    // x = current reflection active.
    // y = source class from ReflectionV3.
    // z = reprojected previous history reusable this frame.
    // w = rejection/debt strength; nonzero means source history should not be
    // blindly trusted by later reflection source-fusion passes.
    output.historyValidity = float4(
        active,
        sourceClass,
        historyReusable,
        max(max(forcedUnavailable, historyRequiredButMissing), reprojectionRejected));
    // x = source switch between current source ID and reprojected previous source ID.
    // y = depth/normal/bounds disocclusion rejection.
    // z = high-motion rejection.
    // w = out-of-bounds or forced/history debt.
    output.historyRejection = float4(
        sourceSwitch,
        disocclusionRejection,
        highMotionRejection,
        max(outOfBoundsRejection, max(forcedUnavailable, historyRequiredButMissing)));
    return output;
}
