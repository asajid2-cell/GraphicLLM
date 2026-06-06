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
Texture2D<float> g_Depth : register(t4);
Texture2D<float4> g_NormalRoughness : register(t5);
Texture2D<float2> g_Velocity : register(t6);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 historyCurr : SV_Target0;
    float4 historyValidity : SV_Target1;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

static float3 LoadNormal(uint2 p) {
    float3 n = g_NormalRoughness.Load(int3(p, 0)).xyz * 2.0f - 1.0f;
    return normalize(n + 1e-5f);
}

PSOutput PSMain(VSOutput input) {
    uint width;
    uint height;
    g_ReflectionRadiance.GetDimensions(width, height);
    float2 dim = max(float2(width, height), 1.0f);
    uint2 p = min(uint2(input.position.xy), uint2(width - 1u, height - 1u));
    float2 velocity = g_Velocity.Load(int3(p, 0));
    float2 historyUv = input.texCoord + velocity + g_TAAParams.xy;
    bool inBounds =
        historyUv.x >= 0.0f && historyUv.x <= 1.0f &&
        historyUv.y >= 0.0f && historyUv.y <= 1.0f;
    uint2 hp = min(uint2(saturate(historyUv) * dim), uint2(width - 1u, height - 1u));

    float4 radiance = g_ReflectionRadiance.Sample(g_LinearClamp, input.texCoord);
    float4 sourceId = g_ReflectionSourceId.Sample(g_LinearClamp, input.texCoord);
    float4 temporalDelta = g_ReflectionTemporalDelta.Sample(g_LinearClamp, input.texCoord);
    float4 historyPrev = g_ReflectionHistoryPrev.Sample(g_LinearClamp, historyUv);

    float centerDepth = g_Depth.Load(int3(p, 0));
    float historyDepth = g_Depth.Load(int3(hp, 0));
    float3 centerNormal = LoadNormal(p);
    float3 historyNormal = LoadNormal(hp);
    float speedPixels = length(velocity * dim);

    float depthAcceptance = exp2(-abs(centerDepth - historyDepth) * 160.0f);
    float normalAcceptance = saturate((dot(centerNormal, historyNormal) - 0.78f) / 0.22f);
    float motionAcceptance = saturate(1.0f - max(speedPixels - 4.0f, 0.0f) / 56.0f);
    float boundsAcceptance = inBounds ? 1.0f : 0.0f;
    float reprojectionAcceptance =
        saturate(depthAcceptance * normalAcceptance * motionAcceptance * boundsAcceptance);

    float confidence = saturate(radiance.a);
    float active = step(0.001f, confidence + Luma(max(radiance.rgb, 0.0f.xxx)));
    float sourceClass = saturate(sourceId.r);
    float prevConfidence = saturate(historyPrev.a);
    float previousHistoryAvailable = step(0.001f, prevConfidence + Luma(max(historyPrev.rgb, 0.0f.xxx)));
    float historyRequiredButMissing = saturate(temporalDelta.b);
    float forcedUnavailable = saturate(temporalDelta.g);
    float historyReusable = previousHistoryAvailable * reprojectionAcceptance;
    float reprojectionRejected = 1.0f - reprojectionAcceptance;

    PSOutput output;
    output.historyCurr = float4(max(radiance.rgb, 0.0f.xxx), confidence);
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
    return output;
}
