// Full-scene reflection resolver producer for the V3 shader stack.
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
    //     0 auto, 1 force scene-local, 2 force SSR,
    //     4 force environment, 255 force none.
    float4   g_LocalProbeParams;
};

Texture2D<float4> g_LocalReflectionRadiance : register(t0);
Texture2D<float4> g_SSRReflection : register(t1);
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
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

PSOutput PSMain(VSOutput input) {
    float4 local = g_LocalReflectionRadiance.Sample(g_LinearClamp, input.texCoord);
    float3 localRadiance = max(local.rgb, 0.0f.xxx);
    float localConfidence = saturate(local.a);
    float localActive = step(0.001f, localConfidence + Luma(localRadiance));

    float4 ssr = g_SSRReflection.Sample(g_LinearClamp, input.texCoord);
    float3 ssrRadiance = max(ssr.rgb, 0.0f.xxx);
    float ssrRawConfidence = saturate(ssr.a);
    float ssrConfidence = smoothstep(0.55f, 0.86f, ssrRawConfidence);
    float ssrLuma = Luma(ssrRadiance);
    ssrConfidence *= step(0.001f, ssrLuma);
    float ssrRawActive = step(0.001f, ssrRawConfidence * ssrLuma);
    float ssrForcedConfidence = max(ssrConfidence, saturate(ssrRawConfidence));
    float ssrActive = step(0.001f, ssrConfidence);

    float envEnabled = max(step(0.5f, g_EnvParams.z), step(0.5f, g_LocalProbeParams.z));
    float envScale = max(max(g_EnvParams.x, g_EnvParams.y), g_LocalProbeParams.y);
    float3 envRadiance = max(g_AmbientColor.rgb, 0.0f.xxx) * max(envScale, 0.08f) * envEnabled;
    float envConfidence = saturate(envEnabled * (0.18f + 0.32f * saturate(envScale)));
    float envActive = step(0.001f, envConfidence + Luma(envRadiance));

    uint sourceOverride = (uint)round(max(g_LocalProbeParams.w, 0.0f));
    bool forceLocal = sourceOverride == 1u;
    bool forceSSR = sourceOverride == 2u;
    bool forceEnvironment = sourceOverride == 4u;
    bool forceNone = sourceOverride >= 255u;

    bool chooseSSR = !forceNone && ((forceSSR && ssrRawActive > 0.0f) ||
                     (!forceLocal && !forceSSR && !forceEnvironment &&
                      ssrActive > 0.0f && ssrConfidence >= max(localConfidence + 0.18f, 0.72f)));
    bool chooseLocal = !forceNone && !chooseSSR &&
                       ((forceLocal && localActive > 0.0f) ||
                        (!forceLocal && !forceSSR && !forceEnvironment && localActive > 0.0f));
    bool chooseEnvironment = !forceNone && !chooseLocal &&
                             !chooseSSR &&
                             ((forceEnvironment && envActive > 0.0f) ||
                              (!forceLocal && !forceSSR && envActive > 0.0f));

    float sourceCode = chooseSSR ? 2.0f : (chooseLocal ? 1.0f : (chooseEnvironment ? 4.0f : 0.0f));
    float3 radiance = chooseSSR ? ssrRadiance : (chooseLocal ? localRadiance : (chooseEnvironment ? envRadiance : 0.0f.xxx));
    float confidence = chooseSSR ? (forceSSR ? ssrForcedConfidence : ssrConfidence) : (chooseLocal ? localConfidence : (chooseEnvironment ? envConfidence : 0.0f));
    float active = step(0.001f, confidence + Luma(radiance));

    PSOutput output;
    output.radiance = float4(radiance, confidence);
    output.confidence = float4(confidence.xxx, 1.0f);

    // Encoded source ID: R = source class normalized by 4
    // (0 none, 0.25 scene-local radiance, 0.5 SSR,
    //  1 scene-local environment),
    // G = confidence, B = active override normalized for policy debugging.
    float overrideSignal = sourceOverride >= 255u ? 1.0f : saturate((float)sourceOverride / 4.0f);
    output.sourceId = float4(sourceCode * 0.25f, confidence, overrideSignal, 1.0f);

    // Rejection mask: R = scene-local radiance rejected/missing,
    // G = SSR rejected/missing, B = environment fallback rejected/missing.
    float localRejected = chooseLocal ? 0.0f : (1.0f - localActive);
    float ssrRejected = chooseSSR ? 0.0f : (1.0f - ssrActive);
    float environmentRejected = chooseEnvironment ? 0.0f : (1.0f - envActive);
    output.rejectedSourceMask = float4(localRejected, ssrRejected, environmentRejected, 1.0f);

    // Stable scene-local sources do not require history. Forced policies that
    // cannot be satisfied are visible in G so packets can prove the override
    // was rejected instead of silently falling through.
    float forcedButUnavailable =
        (forceLocal && localActive <= 0.0f) ||
        (forceSSR && ssrRawActive <= 0.0f) ||
        (forceEnvironment && envActive <= 0.0f) ||
        forceNone
            ? 1.0f
            : 0.0f;
    float historyRequiredButMissing = chooseSSR ? (1.0f - step(0.5f, g_TAAParams.w)) : 0.0f;
    output.temporalDelta = float4(1.0f - active, forcedButUnavailable, historyRequiredButMissing, 1.0f);

    // SSR source diagnostic: R = raw luma, G = raw SSR alpha/weight,
    // B = admitted confidence after resolver shaping / forced raw admission,
    // A = forced-SSR rejected.
    output.ssrSourceSignal = float4(saturate(ssrLuma), ssrRawConfidence,
                                    forceSSR ? ssrForcedConfidence : ssrConfidence,
                                    forceSSR && ssrRawActive <= 0.0f ? 1.0f : 0.0f);
    return output;
}
