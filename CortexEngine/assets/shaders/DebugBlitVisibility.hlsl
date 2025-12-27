// DebugBlitVisibility.hlsl
// Fullscreen visualization of the visibility buffer (triangleID, instanceID).

Texture2D<uint2> g_VisibilityTexture : register(t0);
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

static uint HashUint(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static float3 ColorFromID(uint id) {
    uint h = HashUint(id);
    float r = (float)((h >> 0) & 255u) / 255.0f;
    float g = (float)((h >> 8) & 255u) / 255.0f;
    float b = (float)((h >> 16) & 255u) / 255.0f;
    return float3(r, g, b);
}

float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);
    uint2 vis = g_VisibilityTexture.Load(int3(pixelCoord, 0));

    const uint tri = vis.x;
    const uint inst = vis.y;

    if (tri == 0xFFFFFFFFu && inst == 0xFFFFFFFFu) {
        return float4(0, 0, 0, 1);
    }

    float3 c = ColorFromID(inst);
    // Small modulation so triangle boundaries show up a bit.
    float t = (float)((tri % 17u) + 1u) / 18.0f;
    return float4(c * t, 1.0f);
}

