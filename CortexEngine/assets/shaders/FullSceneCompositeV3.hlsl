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
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PSOutput {
    float4 hdrSceneColor : SV_Target0;
    float4 energyClampPolicy : SV_Target1;
    float4 overbrightDiagnostics : SV_Target2;
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

    // V3 direct lighting is already shadowed; shadow is still sampled here as
    // an explicit ownership input and to keep a bounded rescue path for early
    // split-lighting bring-up.
    float3 composite = direct + indirect;
    composite += reflection * (0.10f * reflectionConfidence);

    float splitLuma = Luma(composite);
    if (splitLuma < 0.002f) {
        float albedoLuma = Luma(albedo);
        float3 materialFill = albedo * 0.035f;
        float3 sceneLocalFloor = float3(0.018f, 0.020f, 0.024f);
        composite = max(composite, max(materialFill, sceneLocalFloor * (1.0f - saturate(albedoLuma * 4.0f))));
        splitLuma = Luma(composite);
    }

    float fallbackLuma = Luma(fallback);
    float legacyFallbackUsed = 0.0f;
    if (splitLuma < 0.002f && fallbackLuma > 0.002f) {
        composite = fallback * lerp(0.10f, 0.18f, shadow);
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
    return output;
}
