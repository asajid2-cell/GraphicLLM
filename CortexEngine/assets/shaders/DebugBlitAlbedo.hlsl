// DebugBlitAlbedo.hlsl
// Simple fullscreen blit to visualize G-buffer albedo

Texture2D<float4> g_AlbedoTexture : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// Fullscreen triangle vertex shader (no vertex buffer needed)
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Generate fullscreen triangle
    // Vertex 0: (-1, -1) -> UV (0, 1)
    // Vertex 1: (-1,  3) -> UV (0, -1)
    // Vertex 2: ( 3, -1) -> UV (2, 1)
    float2 texCoord = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(texCoord * float2(2, -2) + float2(-1, 1), 0, 1);
    output.texCoord = texCoord;

    return output;
}

// Pixel shader: Sample albedo and output
float4 PSMain(VSOutput input) : SV_Target0 {
    return g_AlbedoTexture.Sample(g_Sampler, input.texCoord);
}
