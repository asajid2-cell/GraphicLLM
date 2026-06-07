// Full-scene reflection resolver producer for the V3 shader stack.
#include "SurfaceClassification.hlsli"
//
// This resolver owns ReflectionV3 source admission. It starts with scene-local
// reflection radiance and a scene-local environment fallback; SSR/RT source
// inputs can join the same policy contract without changing downstream
// composite/debug resources.

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
    // w = ReflectionV3 source override:
    //     0 auto, 1 force scene-local, 2 force SSR, 3 force RT,
    //     4 force environment, 255 force none.
    float4   g_LocalProbeParams;
};

Texture2D<float4> g_LocalReflectionRadiance : register(t0);
Texture2D<float4> g_SSRReflection : register(t1);
Texture2D<float4> g_RTReflection : register(t2);
Texture2D<float4> g_HistoryPrevSourceId : register(t3);
Texture2D<float4> g_HistoryValidity : register(t4);
Texture2D<float4> g_HistoryRejection : register(t5);
Texture2D<float4> g_NormalRoughness : register(t6);
Texture2D<float4> g_EmissiveMetallic : register(t7);
Texture2D<float4> g_MaterialExt2 : register(t8);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 radiance : SV_Target0;
    float4 confidence : SV_Target1;
    float4 sourceId : SV_Target2;
    float4 rejectedSourceMask : SV_Target3;
    float4 temporalDelta : SV_Target4;
    float4 ssrSourceSignal : SV_Target5;
    float4 rtSourceSignal : SV_Target6;
    float4 sourceSuppression : SV_Target7;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

PSOutput PSMain(VSOutput input) {
    int2 pixelCoord = int2(input.position.xy);
    // These source buffers are pixel-aligned render targets. Use exact loads so
    // source IDs, confidence, and suppression masks do not shimmer from linear
    // filtering as the camera jitters or the mouse moves.
    float4 local = g_LocalReflectionRadiance.Load(int3(pixelCoord, 0));
    float3 localRadiance = max(local.rgb, 0.0f.xxx);
    float localConfidence = saturate(local.a);
    float localActive = step(0.001f, localConfidence + Luma(localRadiance));

    float4 normalRoughness = g_NormalRoughness.Load(int3(pixelCoord, 0));
    float4 emissiveMetallic = g_EmissiveMetallic.Load(int3(pixelCoord, 0));
    float4 materialExt2 = g_MaterialExt2.Load(int3(pixelCoord, 0));
    float roughness = saturate(normalRoughness.w);
    float metallic = saturate(emissiveMetallic.a);
    uint surfaceClass = DecodeSurfaceClass(materialExt2.r);
    uint sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a);
    bool waterLike = surfaceClass == SURFACE_CLASS_WATER ||
                     sceneMaterialClass == SCENE_MATERIAL_WATER;
    bool glassLike = surfaceClass == SURFACE_CLASS_GLASS ||
                     sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE;
    bool mirrorLike = surfaceClass == SURFACE_CLASS_MIRROR ||
                      sceneMaterialClass == SCENE_MATERIAL_MIRROR;
    bool conductorLike = surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
                         sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL ||
                         sceneMaterialClass == SCENE_MATERIAL_POLISHED_METAL;
    bool wetLike = sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE;
    float smoothness = saturate(1.0f - roughness);
    float roughReflection = smoothstep(0.45f, 0.92f, roughness);
    float glossyMaterial = saturate(smoothness * (0.62f + 0.38f * metallic));
    float classSourceFloor =
        mirrorLike ? 1.00f :
        waterLike ? 0.92f :
        glassLike ? 0.84f :
        conductorLike ? 0.76f :
        wetLike ? 0.70f :
        0.0f;
    float ssrMaterialWeight = max(saturate(0.24f + 0.76f * glossyMaterial), classSourceFloor);
    float rtMaterialWeight = max(saturate(0.32f + 0.68f * glossyMaterial), classSourceFloor);

    float4 ssr = g_SSRReflection.Load(int3(pixelCoord, 0));
    float3 ssrRadiance = max(ssr.rgb, 0.0f.xxx);
    float ssrRawConfidence = saturate(ssr.a);
    float ssrConfidence = smoothstep(0.55f, 0.86f, ssrRawConfidence);
    float ssrLuma = Luma(ssrRadiance);
    ssrConfidence *= step(0.001f, ssrLuma);
    ssrConfidence *= ssrMaterialWeight;
    float ssrRawActive = step(0.001f, ssrRawConfidence * ssrLuma);
    float ssrForcedConfidence = max(ssrConfidence, saturate(ssrRawConfidence));
    float ssrActive = step(0.001f, ssrConfidence);

    float4 rt = g_RTReflection.Load(int3(pixelCoord, 0));
    float3 rtRadiance = max(rt.rgb, 0.0f.xxx);
    float rtRawConfidence = saturate(rt.a);
    float rtLuma = Luma(rtRadiance);
    float rtConfidence = smoothstep(0.08f, 0.35f, max(rtRawConfidence, saturate(rtLuma)));
    rtConfidence *= step(0.001f, rtRawConfidence + rtLuma);
    rtConfidence *= rtMaterialWeight;
    float rtRawActive = step(0.001f, (rtRawConfidence + rtLuma));
    float rtActive = step(0.001f, rtConfidence);

    float envEnabled = max(step(0.5f, g_EnvParams.z), step(0.5f, g_LocalProbeParams.z));
    float envScale = max(max(g_EnvParams.x, g_EnvParams.y), g_LocalProbeParams.y);
    float3 envRadiance = max(g_AmbientColor.rgb, 0.0f.xxx) * max(envScale, 0.08f) * envEnabled;
    float envConfidence = saturate(envEnabled * (0.18f + 0.32f * saturate(envScale)));
    envConfidence = saturate(envConfidence + roughReflection * (0.04f + 0.08f * (1.0f - metallic)));
    localConfidence = saturate(localConfidence + roughReflection * (0.04f + 0.06f * (1.0f - metallic)));
    localActive = step(0.001f, localConfidence + Luma(localRadiance));
    float envActive = step(0.001f, envConfidence + Luma(envRadiance));

    uint sourceOverride = (uint)round(max(g_LocalProbeParams.w, 0.0f));
    bool forceLocal = sourceOverride == 1u;
    bool forceSSR = sourceOverride == 2u;
    bool forceRT = sourceOverride == 3u;
    bool forceEnvironment = sourceOverride == 4u;
    bool forceNone = sourceOverride >= 255u;

    float4 historyPrevSourceId = g_HistoryPrevSourceId.Load(int3(pixelCoord, 0));
    float4 historyValidity = g_HistoryValidity.Load(int3(pixelCoord, 0));
    float4 historyRejection = g_HistoryRejection.Load(int3(pixelCoord, 0));
    float previousSourceClass = saturate(max(historyPrevSourceId.r, historyValidity.g));
    float previousSourceAvailable = step(0.001f, previousSourceClass);
    float historyReusable = saturate(historyValidity.b);
    float historyDebt = saturate(max(historyValidity.a,
                                     max(historyRejection.g, max(historyRejection.b, historyRejection.a))));
    float priorSwitchDebt = saturate(max(historyRejection.r, previousSourceAvailable * (1.0f - historyReusable)));
    float ssrSourceSwitch = previousSourceAvailable * step(0.08f, abs(0.50f - previousSourceClass));
    float rtSourceSwitch = previousSourceAvailable * step(0.08f, abs(0.75f - previousSourceClass));
    float ssrHistoryPenalty =
        ssrSourceSwitch * saturate(0.06f + 0.20f * (1.0f - historyReusable) + 0.16f * historyDebt + 0.14f * priorSwitchDebt);
    float rtHistoryPenalty =
        rtSourceSwitch * saturate(0.04f + 0.16f * (1.0f - historyReusable) + 0.12f * historyDebt + 0.10f * priorSwitchDebt);
    float ssrAdmissionConfidence = saturate(ssrConfidence - ssrHistoryPenalty);
    float rtAdmissionConfidence = saturate(rtConfidence - rtHistoryPenalty);
    float ssrAutoThreshold = max(localConfidence + 0.18f, 0.72f);
    float rtAutoThreshold = max(localConfidence + 0.16f, 0.62f);
    bool autoPolicy = !forceLocal && !forceSSR && !forceRT && !forceEnvironment;
    bool ssrBaselineEligible = autoPolicy && ssrActive > 0.0f && ssrConfidence >= ssrAutoThreshold;
    bool rtBaselineEligible = autoPolicy && rtActive > 0.0f && rtConfidence >= rtAutoThreshold;
    bool ssrHistoryEligible = autoPolicy && ssrActive > 0.0f && ssrAdmissionConfidence >= ssrAutoThreshold;
    bool rtHistoryEligible = autoPolicy && rtActive > 0.0f && rtAdmissionConfidence >= rtAutoThreshold;

    bool chooseSSR = !forceNone && ((forceSSR && ssrRawActive > 0.0f) || ssrHistoryEligible);
    bool chooseRT = !forceNone && !chooseSSR &&
                    ((forceRT && rtRawActive > 0.0f) ||
                     rtHistoryEligible);
    bool chooseLocal = !forceNone && !chooseSSR && !chooseRT &&
                       ((forceLocal && localActive > 0.0f) ||
                        (!forceLocal && !forceSSR && !forceRT && !forceEnvironment && localActive > 0.0f));
    bool chooseEnvironment = !forceNone && !chooseLocal &&
                             !chooseSSR && !chooseRT &&
                             ((forceEnvironment && envActive > 0.0f) ||
                              (!forceLocal && !forceSSR && !forceRT && envActive > 0.0f));

    float sourceCode = chooseSSR ? 2.0f : (chooseRT ? 3.0f : (chooseLocal ? 1.0f : (chooseEnvironment ? 4.0f : 0.0f)));
    float3 radiance = chooseSSR ? ssrRadiance : (chooseRT ? rtRadiance : (chooseLocal ? localRadiance : (chooseEnvironment ? envRadiance : 0.0f.xxx)));
    float confidence = chooseSSR ? (forceSSR ? ssrForcedConfidence : ssrAdmissionConfidence) :
                       (chooseRT ? (forceRT ? max(rtConfidence, rtRawConfidence) : rtAdmissionConfidence) :
                        (chooseLocal ? localConfidence : (chooseEnvironment ? envConfidence : 0.0f)));
    float active = step(0.001f, confidence + Luma(radiance));

    PSOutput output;
    output.radiance = float4(radiance, confidence);
    output.confidence = float4(confidence.xxx, 1.0f);

    // Encoded source ID: R = source class normalized by 4
    // (0 none, 0.25 scene-local radiance, 0.5 SSR, 0.75 RT,
    //  1 scene-local environment),
    // G = confidence, B = active override normalized for policy debugging.
    float overrideSignal = sourceOverride >= 255u ? 1.0f : saturate((float)sourceOverride / 4.0f);
    output.sourceId = float4(sourceCode * 0.25f, confidence, overrideSignal, 1.0f);

    // Rejection mask: R = scene-local radiance rejected/missing,
    // G = SSR rejected/missing, B = RT/environment rejected or missing,
    // A = auto SSR/RT suppressed by source-history hysteresis.
    float localAvailability = saturate(localConfidence + Luma(localRadiance));
    float ssrAvailability = saturate(max(ssrConfidence, ssrRawConfidence) * ssrRawActive);
    float rtAvailability = saturate(max(rtConfidence, max(rtRawConfidence, saturate(rtLuma))) * rtRawActive);
    float envAvailability = saturate(envConfidence + Luma(envRadiance));
    float localRejected = chooseLocal ? 0.0f : saturate(1.0f - localAvailability);
    float ssrRejected = chooseSSR ? 0.0f : saturate(1.0f - ssrAvailability);
    float rtRejected = chooseRT ? 0.0f : saturate(1.0f - rtAvailability);
    float environmentRejected = chooseEnvironment ? 0.0f : saturate(1.0f - envAvailability);
    float materialSuppressedSource = max(
        ssrRawActive > 0.0f ? saturate(ssrRawConfidence - ssrConfidence) : 0.0f,
        rtRawActive > 0.0f ? saturate(max(rtRawConfidence, saturate(rtLuma)) - rtConfidence) : 0.0f);
    float historySuppressedSource = max(
        ssrBaselineEligible ? saturate(ssrConfidence - ssrAdmissionConfidence) : 0.0f,
        rtBaselineEligible ? saturate(rtConfidence - rtAdmissionConfidence) : 0.0f);
    output.rejectedSourceMask = float4(localRejected,
                                       ssrRejected,
                                       max(rtRejected, environmentRejected),
                                       max(historySuppressedSource, materialSuppressedSource));
    output.sourceSuppression = float4(historySuppressedSource,
                                      materialSuppressedSource,
                                      roughness,
                                      metallic);

    // Stable scene-local sources do not require history. Forced policies that
    // cannot be satisfied are visible in G so packets can prove the override
    // was rejected instead of silently falling through.
    float forcedButUnavailable =
        (forceLocal && localActive <= 0.0f) ||
        (forceSSR && ssrRawActive <= 0.0f) ||
        (forceRT && rtRawActive <= 0.0f) ||
        (forceEnvironment && envActive <= 0.0f) ||
        forceNone
            ? 1.0f
            : 0.0f;
    float historyRequiredButMissing = (chooseSSR || chooseRT) ? (1.0f - step(0.5f, g_TAAParams.w)) : 0.0f;
    float inactiveContinuous = saturate(1.0f - saturate(confidence + Luma(radiance)));
    output.temporalDelta = float4(inactiveContinuous, forcedButUnavailable, historyRequiredButMissing, 1.0f);

    // SSR source diagnostic: R = raw luma, G = raw SSR alpha/weight,
    // B = admitted confidence after resolver shaping / forced raw admission,
    // A = forced-SSR rejected.
    output.ssrSourceSignal = float4(saturate(ssrLuma), ssrRawConfidence,
                                    forceSSR ? ssrForcedConfidence : ssrAdmissionConfidence,
                                    forceSSR && ssrRawActive <= 0.0f ? 1.0f : 0.0f);

    // RT source diagnostic: R = raw RT luma, G = raw RT alpha/weight,
    // B = admitted confidence after resolver shaping / forced raw admission,
    // A = forced-RT rejected.
    output.rtSourceSignal = float4(saturate(rtLuma), rtRawConfidence,
                                   forceRT ? max(rtConfidence, rtRawConfidence) : rtAdmissionConfidence,
                                   forceRT && rtRawActive <= 0.0f ? 1.0f : 0.0f);
    return output;
}
