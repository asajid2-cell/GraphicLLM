// Full-scene reflection resolver producer for the V3 shader stack.
//
// This first resolver slice wraps the scene-local reflection radiance buffer
// into named V3 resources: radiance, confidence, source ID, rejected-source
// mask, and temporal delta. Later slices can replace the source selection with
// SSR/RT/environment arbitration without changing downstream contracts.

Texture2D<float4> g_LocalReflectionRadiance : register(t0);
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
};

static float Luma(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

PSOutput PSMain(VSOutput input) {
    float4 local = g_LocalReflectionRadiance.Sample(g_LinearClamp, input.texCoord);
    float3 radiance = max(local.rgb, 0.0f.xxx);
    float confidence = saturate(local.a);
    float active = step(0.001f, confidence + Luma(radiance));

    PSOutput output;
    output.radiance = float4(radiance, confidence);
    output.confidence = float4(confidence.xxx, 1.0f);

    // Encoded source ID: 0 = none, 1 = scene-local radiance.
    output.sourceId = float4(active, confidence, 0.0f, 1.0f);

    // Channels reserve rejection classes. This slice can only prove when no
    // scene-local source was available; SSR/RT/environment rejected bits become
    // meaningful when the resolver owns those sources.
    float noSceneLocalSource = 1.0f - active;
    output.rejectedSourceMask = float4(noSceneLocalSource, 0.0f, 0.0f, 1.0f);

    // The first resolver has no temporal history owner, so the stable
    // scene-local path emits zero delta where confidence exists and one in the
    // no-source channel. This makes missing reflection ownership visible
    // without claiming history filtering.
    output.temporalDelta = float4(noSceneLocalSource, 0.0f, 0.0f, 1.0f);
    return output;
}
