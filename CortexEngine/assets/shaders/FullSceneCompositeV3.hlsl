// Candidate full-scene composite producer for the V3 shader stack.
// This pass consumes concrete V3 lighting split resources and writes a named
// HDR scene-color target for downstream CinematicPostV3 review.

Texture2D<float4> g_DirectLighting : register(t0);
Texture2D<float4> g_IndirectLighting : register(t1);
Texture2D<float4> g_ShadowVisibility : register(t2);
Texture2D<float4> g_LegacyHDRFallback : register(t3);
Texture2D<float4> g_ReflectionRadiance : register(t4);
Texture2D<float4> g_ReflectionConfidence : register(t5);
Texture2D<float4> g_MaterialAlbedo : register(t6);
Texture2D<float4> g_SceneLocalEnvironment : register(t7);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 hdrSceneColor : SV_Target0;
    float4 energyClampPolicy : SV_Target1;
    float4 overbrightDiagnostics : SV_Target2;
    float4 compositeContributionMap : SV_Target3;
    float4 legacyRescueUsage : SV_Target4;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

PSOutput PSMain(VSOutput input) {
    float3 direct = max(g_DirectLighting.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float3 indirect = max(g_IndirectLighting.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float shadow = saturate(g_ShadowVisibility.Sample(g_LinearClamp, input.texCoord).r);
    float3 fallback = max(g_LegacyHDRFallback.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float4 reflectionSample = g_ReflectionRadiance.Sample(g_LinearClamp, input.texCoord);
    float3 reflection = max(reflectionSample.rgb, 0.0f.xxx);
    float reflectionConfidence = saturate(max(reflectionSample.a, g_ReflectionConfidence.Sample(g_LinearClamp, input.texCoord).r));
    float3 albedo = saturate(g_MaterialAlbedo.Sample(g_LinearClamp, input.texCoord).rgb);
    float4 sceneEnvironment = saturate(g_SceneLocalEnvironment.Sample(g_LinearClamp, input.texCoord));

    // V3 direct lighting is already shadowed; shadow is still sampled here as
    // an explicit ownership input and to keep a bounded rescue path for early
    // split-lighting bring-up.
    float3 composite = direct + indirect;
    float compositeLumaBeforeReflection = Luma(composite);
    float reflectionLuma = Luma(reflection);
    float confidenceGate = smoothstep(0.12f, 0.82f, reflectionConfidence);
    float hotReflection = smoothstep(0.85f, 5.0f, reflectionLuma);
    float relativeReflection =
        reflectionLuma / max(compositeLumaBeforeReflection + 0.08f, 0.08f);
    float overReflective = smoothstep(0.70f, 2.60f, relativeReflection);
    float lumaRolloff = lerp(1.0f, 0.30f, hotReflection);
    float confidenceRolloff = lerp(0.42f, 1.0f, confidenceGate);
    float plasticGuard = lerp(1.0f, 0.46f, overReflective * (1.0f - confidenceGate * 0.35f));
    float reflectionWeight = 0.10f * confidenceGate * lumaRolloff * confidenceRolloff * plasticGuard;
    float3 reflectionContribution = reflection * reflectionWeight;
    composite += reflectionContribution;

    float3 materialContribution = 0.0f.xxx;
    float3 environmentContribution = 0.0f.xxx;

    float splitLuma = Luma(composite);
    if (splitLuma < 0.002f) {
        float albedoLuma = Luma(albedo);
        float3 materialFill = albedo * 0.035f;
        float3 sceneLocalFloor = lerp(float3(0.018f, 0.020f, 0.024f),
                                      float3(0.028f, 0.034f, 0.042f),
                                      saturate(sceneEnvironment.g + sceneEnvironment.b));
        materialContribution = materialFill;
        environmentContribution = sceneLocalFloor * (1.0f - saturate(albedoLuma * 4.0f));
        composite = max(composite, max(materialContribution, environmentContribution));
        splitLuma = Luma(composite);
    }

    float fallbackLuma = Luma(fallback);
    float legacyFallbackUsed = 0.0f;
    float legacyRescueWeight = 0.0f;
    if (splitLuma < 0.002f && fallbackLuma > 0.002f) {
        legacyRescueWeight = lerp(0.10f, 0.18f, shadow);
        composite = fallback * legacyRescueWeight;
        legacyFallbackUsed = 1.0f;
    }

    float3 unclampedComposite = composite;
    composite = min(unclampedComposite, 16.0f.xxx);

    float unclampedLuma = Luma(unclampedComposite);
    float clampedLuma = Luma(composite);
    float clampMask = any(unclampedComposite > 16.0f.xxx) ? 1.0f : 0.0f;
    float clampRatio = saturate((unclampedLuma - clampedLuma) / max(unclampedLuma, 1.0e-4f));
    float overbrightMask = saturate((unclampedLuma - 1.0f) / 4.0f);
    float underlitMask = 1.0f - smoothstep(0.002f, 0.05f, unclampedLuma);

    PSOutput output;
    output.hdrSceneColor = float4(composite, 1.0f);
    output.energyClampPolicy = float4(saturate(unclampedLuma / 16.0f), clampMask, clampRatio, legacyFallbackUsed);
    output.overbrightDiagnostics = float4(overbrightMask, underlitMask, legacyFallbackUsed, reflectionConfidence);
    output.compositeContributionMap = float4(
        saturate(Luma(direct) / max(unclampedLuma, 1.0e-4f)),
        saturate(Luma(indirect + environmentContribution + materialContribution) / max(unclampedLuma, 1.0e-4f)),
        saturate(Luma(reflectionContribution) / max(unclampedLuma, 1.0e-4f)),
        legacyFallbackUsed);
    output.legacyRescueUsage = float4(
        legacyFallbackUsed,
        saturate(fallbackLuma / 16.0f),
        legacyRescueWeight,
        saturate(splitLuma / 16.0f));
    return output;
}
