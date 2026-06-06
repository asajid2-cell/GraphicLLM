// Candidate full-scene composite producer for the V3 shader stack.
// This pass consumes concrete V3 lighting split resources and writes a named
// HDR scene-color target for downstream CinematicPostV3 review.

Texture2D<float4> g_DirectLighting : register(t0);
Texture2D<float4> g_IndirectLighting : register(t1);
Texture2D<float4> g_ShadowVisibility : register(t2);
Texture2D<float4> g_LegacyHDRFallback : register(t3);
Texture2D<float4> g_LocalReflectionRadiance : register(t4);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSMain(VSOutput input) : SV_Target0 {
    float3 direct = max(g_DirectLighting.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float3 indirect = max(g_IndirectLighting.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float shadow = saturate(g_ShadowVisibility.Sample(g_LinearClamp, input.texCoord).r);
    float3 fallback = max(g_LegacyHDRFallback.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);
    float3 reflection = max(g_LocalReflectionRadiance.Sample(g_LinearClamp, input.texCoord).rgb, 0.0f.xxx);

    // V3 direct lighting is already shadowed; shadow is still sampled here as
    // an explicit ownership input and to keep a bounded rescue path for early
    // split-lighting bring-up.
    float3 composite = direct + indirect;
    float reflectionLuma = Luma(reflection);
    float compositeLuma = Luma(composite);
    float reflectionConfidence = saturate(reflectionLuma / max(compositeLuma + 0.25f, 0.25f));
    composite += reflection * (0.10f * reflectionConfidence);

    float splitLuma = Luma(composite);
    float fallbackLuma = Luma(fallback);
    if (splitLuma < 0.002f && fallbackLuma > 0.002f) {
        composite = fallback * lerp(0.10f, 0.18f, shadow);
    }

    composite = min(composite, 16.0f.xxx);
    return float4(composite, 1.0f);
}
