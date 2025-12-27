// DebugBlitDepth.hlsl
// Fullscreen visualization of the hardware depth buffer (R32_FLOAT SRV).

Texture2D<float> g_DepthTexture : register(t0);
SamplerState g_Sampler : register(s0); // Unused (kept for shared root sig)

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    output.texCoord = texCoord;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);
    float depth = g_DepthTexture.Load(int3(pixelCoord, 0));

    // Visualize geometry as brighter (near = bright).
    float v = saturate(1.0f - depth);
    v = pow(v, 0.2f);
    return float4(v.xxx, 1.0f);
}

