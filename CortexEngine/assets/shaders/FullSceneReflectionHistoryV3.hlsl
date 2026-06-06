// ReflectionV3 history/stability seed pass.
//
// This slice establishes current/previous history ownership without changing
// source admission policy. Later slices can add velocity/depth/normal
// reprojection and source-ID hysteresis on top of this resource contract.

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

PSOutput PSMain(VSOutput input) {
    float4 radiance = g_ReflectionRadiance.Sample(g_LinearClamp, input.texCoord);
    float4 sourceId = g_ReflectionSourceId.Sample(g_LinearClamp, input.texCoord);
    float4 temporalDelta = g_ReflectionTemporalDelta.Sample(g_LinearClamp, input.texCoord);
    float4 historyPrev = g_ReflectionHistoryPrev.Sample(g_LinearClamp, input.texCoord);

    float confidence = saturate(radiance.a);
    float active = step(0.001f, confidence + Luma(max(radiance.rgb, 0.0f.xxx)));
    float sourceClass = saturate(sourceId.r);
    float prevConfidence = saturate(historyPrev.a);
    float previousHistoryAvailable = step(0.001f, prevConfidence + Luma(max(historyPrev.rgb, 0.0f.xxx)));
    float historyRequiredButMissing = saturate(temporalDelta.b);
    float forcedUnavailable = saturate(temporalDelta.g);

    PSOutput output;
    output.historyCurr = float4(max(radiance.rgb, 0.0f.xxx), confidence);
    output.historyValidity = float4(active, sourceClass, previousHistoryAvailable, max(forcedUnavailable, historyRequiredButMissing));
    return output;
}
