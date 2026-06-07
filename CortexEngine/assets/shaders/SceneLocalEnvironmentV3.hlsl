// Scene-local environment producer for the V3 shader stack.
// This pass separates visible background, lighting environment, reflection
// environment, and atmosphere into owned debug/contract resources.

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
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
    float4   g_SSRParams;
    float4   g_PostGradeParams;
    float4   g_RTReflectionParams;
    uint4    g_ScreenAndCluster;
    uint4    g_ClusterParams;
    uint4    g_ClusterSRVIndices;
    float4   g_ProjectionParams;
    float4   g_CinematicParams;
    float4   g_CinematicDofParams;
    float4   g_CinematicStabilityParams;
    float4   g_CinematicLookParams;
    float4   g_CinematicExposureParams;
    // x = scene-local probe diffuse scale, y = scene-local probe specular scale,
    // z = scene-local probe radiance enabled (>0.5),
    // w = ReflectionV3 source override.
    float4   g_LocalProbeParams;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 sceneLocalEnvironment : SV_Target0;
    float4 ambientLighting : SV_Target1;
    float4 visibleBackground : SV_Target2;
    float4 reflectionBackground : SV_Target3;
    float4 atmosphere : SV_Target4;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

static float3 SafeNormalizeColor(float3 color) {
    return color / max(max(max(color.r, color.g), color.b), 1.0e-3f);
}

PSOutput PSMain(VSOutput input) {
    const float iblEnabled = step(0.5f, g_EnvParams.z);
    const float localProbeEnabled = step(0.5f, g_LocalProbeParams.z);
    const float fogEnabled = step(0.5f, g_FogParams.w);
    const float exposure = max(g_TimeAndExposure.z, 0.0f);
    const float sunIntensity = g_LightCount.x > 0u ? Luma(max(g_Lights[0].color_range.rgb, 0.0f.xxx)) : 0.0f;
    const float lightCountSignal = saturate((float)g_LightCount.x / 16.0f);
    const float uvHorizon = saturate(1.0f - input.texCoord.y);

    const float3 ambientBase = max(g_AmbientColor.rgb, 0.0f.xxx);
    const float ambientLuma = Luma(ambientBase);
    const float3 ambientTint = SafeNormalizeColor(ambientBase + 0.03f.xxx);
    const float diffuseIBL = saturate(g_EnvParams.x / 3.0f) * iblEnabled;
    const float specularIBL = saturate(g_EnvParams.y / 3.0f) * iblEnabled;
    const float backgroundExposure = saturate(g_EnvParams.w / 4.0f);
    const float probeDiffuse = saturate(g_LocalProbeParams.x);
    const float probeSpecular = saturate(g_LocalProbeParams.y);

    const float3 skyCool = float3(0.20f, 0.31f, 0.48f);
    const float3 roomNeutral = float3(0.12f, 0.13f, 0.14f);
    const float3 visibleColor = lerp(roomNeutral, skyCool, saturate(backgroundExposure + 0.35f * iblEnabled));
    const float3 visibleGradient = visibleColor * lerp(0.65f, 1.20f, uvHorizon);
    const float3 reflectionColor = lerp(ambientTint * 0.18f, ambientTint * 0.55f, saturate(specularIBL + probeSpecular));
    const float3 fogColor = lerp(ambientTint * 0.22f, visibleColor, saturate(fogEnabled + backgroundExposure));

    PSOutput output;
    output.sceneLocalEnvironment = float4(
        saturate(0.15f + 0.30f * iblEnabled + 0.35f * localProbeEnabled),
        saturate(lightCountSignal + diffuseIBL),
        saturate(fogEnabled + backgroundExposure),
        1.0f);
    output.ambientLighting = float4(
        ambientTint * saturate(0.15f + ambientLuma + diffuseIBL + probeDiffuse + 0.10f * sunIntensity),
        saturate(diffuseIBL + probeDiffuse + lightCountSignal));
    output.visibleBackground = float4(visibleGradient, saturate(backgroundExposure + iblEnabled * 0.35f));
    output.reflectionBackground = float4(reflectionColor, saturate(specularIBL + probeSpecular + localProbeEnabled * 0.25f));
    output.atmosphere = float4(
        fogColor * saturate(0.20f + g_FogParams.x * 20.0f + fogEnabled * 0.55f),
        saturate(fogEnabled + g_ColorGrade.z * 0.25f));
    return output;
}
