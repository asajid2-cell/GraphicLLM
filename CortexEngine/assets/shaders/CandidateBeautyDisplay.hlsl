// Fullscreen display path for the opt-in FullSceneCandidateBeautyV3 LDR output.
// The candidate producer stays offscreen; this shader only presents it for review.

Texture2D<float4> g_CandidateBeauty : register(t0);
SamplerState g_LinearClamp : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

float4 PSMain(VSOutput input) : SV_Target0 {
    return g_CandidateBeauty.Sample(g_LinearClamp, input.texCoord);
}
