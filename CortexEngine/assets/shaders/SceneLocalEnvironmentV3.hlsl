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
    // x = fog start distance, y = scene-local payload ready,
    // z = texture richness, w = payload shader influence.
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
    // x/y = DOF focus/aperture for post, z = SceneLocalEnvironmentV3 profile
    // mode, w = local background ownership strength.
    float4   g_CinematicDofParams;
    float4   g_CinematicStabilityParams;
    float4   g_CinematicLookParams;
    float4   g_CinematicExposureParams;
    // x = scene-local probe diffuse scale, y = scene-local probe specular scale,
    // z = scene-local probe radiance enabled (>0.5),
    // w = ReflectionV3 source override.
    float4   g_LocalProbeParams;
};

Texture2D<float4> g_SceneLocalPayloadAlbedo : register(t0);
Texture2D<float4> g_SceneLocalPayloadNormal : register(t1);
Texture2D<float4> g_SceneLocalIrradianceProxy : register(t2);
Texture2D<float4> g_SceneLocalSpecularProxy : register(t3);
Texture2D<float4> g_SceneLocalVisibleBackgroundProxy : register(t4);
SamplerState g_LinearWrapSampler : register(s0);

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
    const float profileMode = round(max(g_CinematicDofParams.z, 0.0f));
    const float localBackgroundStrength = saturate(g_CinematicDofParams.w);
    const float isGallery = 1.0f - step(0.5f, abs(profileMode - 1.0f));
    const float isRoom = 1.0f - step(0.5f, abs(profileMode - 2.0f));
    const float isStage = 1.0f - step(0.5f, abs(profileMode - 3.0f));
    const float isExterior = 1.0f - step(0.5f, abs(profileMode - 4.0f));
    const float payloadReady = step(0.5f, g_FogExtraParams.y);
    const float payloadTextureRichness = saturate(g_FogExtraParams.z);
    const float payloadInfluence = saturate(g_FogExtraParams.w) * payloadReady;
    const float2 payloadUv = input.texCoord * float2(2.0f, 1.35f);
    const float3 payloadAlbedoSample =
        max(g_SceneLocalPayloadAlbedo.SampleLevel(g_LinearWrapSampler, payloadUv, 0.0f).rgb, 0.0f.xxx);
    const float3 payloadNormalSample =
        max(g_SceneLocalPayloadNormal.SampleLevel(g_LinearWrapSampler, payloadUv, 0.0f).rgb, 0.0f.xxx);
    const float3 irradianceProxySample =
        max(g_SceneLocalIrradianceProxy.SampleLevel(g_LinearWrapSampler, payloadUv * 0.45f, 0.0f).rgb, 0.0f.xxx);
    const float3 specularProxySample =
        max(g_SceneLocalSpecularProxy.SampleLevel(g_LinearWrapSampler, payloadUv * 0.65f, 0.0f).rgb, 0.0f.xxx);
    const float3 visibleProxySample =
        max(g_SceneLocalVisibleBackgroundProxy.SampleLevel(g_LinearWrapSampler, payloadUv * 0.35f, 0.0f).rgb, 0.0f.xxx);
    const float payloadAlbedoSignal = step(0.002f, Luma(payloadAlbedoSample));
    const float irradianceProxySignal = step(0.002f, Luma(irradianceProxySample));
    const float specularProxySignal = step(0.002f, Luma(specularProxySample));
    const float visibleProxySignal = step(0.002f, Luma(visibleProxySample));
    const float payloadNormalDetail =
        payloadAlbedoSignal * saturate(length(payloadNormalSample.rg * 2.0f - 1.0f) * 0.45f);
    const float payloadResourceInfluence =
        saturate(payloadInfluence * payloadAlbedoSignal * (0.45f + 0.30f * payloadNormalDetail));
    const float3 payloadSampleTint = SafeNormalizeColor(payloadAlbedoSample + 0.015f.xxx);
    const float3 irradianceProxyTint = SafeNormalizeColor(irradianceProxySample + 0.015f.xxx);
    const float3 specularProxyTint = SafeNormalizeColor(specularProxySample + 0.015f.xxx);
    const float3 visibleProxyTint = SafeNormalizeColor(visibleProxySample + 0.015f.xxx);

    const float3 skyCool = float3(0.20f, 0.31f, 0.48f);
    const float3 galleryNeutral = float3(0.18f, 0.175f, 0.16f);
    const float3 roomWarm = float3(0.22f, 0.155f, 0.105f);
    const float3 stageMagenta = float3(0.24f, 0.045f, 0.16f);
    const float3 stageCyan = float3(0.025f, 0.19f, 0.27f);
    const float3 exteriorSky = float3(0.24f, 0.34f, 0.50f);
    float3 localPalette = galleryNeutral;
    localPalette = lerp(localPalette, roomWarm, isRoom);
    localPalette = lerp(localPalette, lerp(stageMagenta, stageCyan, input.texCoord.x), isStage);
    localPalette = lerp(localPalette, exteriorSky, isExterior);
    const float3 payloadRadianceTint =
        lerp(lerp(localPalette * 1.08f,
                  ambientTint * 0.64f + localPalette * 0.58f,
                  payloadTextureRichness),
             lerp(payloadSampleTint, irradianceProxyTint, irradianceProxySignal),
             saturate(payloadResourceInfluence + irradianceProxySignal * payloadInfluence * 0.25f));
    const float3 payloadVisibleTint =
        lerp(lerp(localPalette, payloadRadianceTint, saturate(0.45f + 0.35f * payloadTextureRichness)),
             lerp(payloadSampleTint, visibleProxyTint, visibleProxySignal),
             saturate(payloadResourceInfluence * 0.55f + visibleProxySignal * payloadInfluence * 0.35f));
    const float3 payloadSpecularTint =
        lerp(payloadRadianceTint, specularProxyTint, specularProxySignal * payloadInfluence);
    const float payloadLocalWeight = saturate(payloadInfluence * (0.55f + 0.30f * payloadTextureRichness));

    const float externalBackgroundWeight =
        saturate((1.0f - localBackgroundStrength) * (backgroundExposure + 0.35f * iblEnabled));
    const float3 visibleColorBase = lerp(localPalette, skyCool, externalBackgroundWeight);
    const float3 visibleColor = lerp(visibleColorBase, payloadVisibleTint, payloadLocalWeight);
    const float3 visibleGradient = visibleColor * lerp(0.62f, 1.24f, uvHorizon);
    const float reflectionLocalWeight = saturate(localBackgroundStrength + localProbeEnabled * 0.35f + payloadInfluence * 0.30f);
    const float3 profileReflection = lerp(localPalette * 0.32f, ambientTint * 0.58f, saturate(specularIBL + probeSpecular));
    const float3 reflectionPayload = lerp(profileReflection, payloadSpecularTint * 0.70f, payloadLocalWeight);
    const float3 reflectionColor = lerp(ambientTint * 0.18f, reflectionPayload, reflectionLocalWeight);
    const float stageHaze = isStage * saturate(0.35f + 0.45f * fogEnabled + 0.25f * lightCountSignal);
    const float3 fogColor = lerp(ambientTint * 0.22f, visibleColor, saturate(fogEnabled + backgroundExposure + stageHaze));

    PSOutput output;
    output.sceneLocalEnvironment = float4(
        saturate(0.12f + 0.24f * iblEnabled + 0.38f * localProbeEnabled + 0.18f * localBackgroundStrength),
        saturate(lightCountSignal + diffuseIBL),
        saturate(fogEnabled + backgroundExposure + localBackgroundStrength * 0.25f + stageHaze),
        1.0f);
    output.ambientLighting = float4(
        lerp(lerp(ambientTint, localPalette, saturate(localBackgroundStrength * 0.55f)),
             payloadRadianceTint,
             payloadLocalWeight * 0.55f) *
            saturate(0.15f + ambientLuma + diffuseIBL + probeDiffuse + 0.10f * sunIntensity),
        saturate(diffuseIBL + probeDiffuse + lightCountSignal + payloadInfluence * 0.35f));
    output.visibleBackground = float4(visibleGradient, saturate(backgroundExposure + iblEnabled * 0.35f + payloadInfluence * 0.45f));
    output.reflectionBackground = float4(reflectionColor, saturate(specularIBL + probeSpecular + localProbeEnabled * 0.25f + payloadInfluence * 0.40f));
    output.atmosphere = float4(
        fogColor * saturate(0.20f + g_FogParams.x * 20.0f + fogEnabled * 0.55f + stageHaze * 0.45f),
        saturate(fogEnabled + g_ColorGrade.z * 0.25f + stageHaze));
    return output;
}
