// TerrainVertex.hlsl
// Vertex shader for terrain tessellation pipeline.
// Outputs control points for hull shader, includes geomorph data.

cbuffer ObjectConstants : register(b0) {
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
    float g_DepthBiasNdc;
    float3 _pad0;
};

// Input vertex format (matches C++ Vertex struct)
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;           // Biome blend data
    float3 coarsePos : COARSE_POS;  // Coarse LOD position for geomorphing (optional, may be zero)
};

// Output to hull shader
struct VSOutput {
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 coarsePos : COARSE_POS;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    // Transform position to world space
    float4 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;

    // Transform normal to world space (using normal matrix for proper handling of non-uniform scale)
    output.normal = normalize(mul((float3x3)g_NormalMatrix, input.normal));

    // Transform tangent to world space
    output.tangent.xyz = normalize(mul((float3x3)g_ModelMatrix, input.tangent.xyz));
    output.tangent.w = input.tangent.w;  // Preserve handedness

    // Pass through texture coordinates and color
    output.texCoord = input.texCoord;
    output.color = input.color;

    // Transform coarse position to world space (for geomorphing)
    // If coarsePos is zero (not provided), use regular position
    float3 coarse = input.coarsePos;
    if (dot(coarse, coarse) < 0.0001) {
        coarse = input.position;  // Fallback to regular position
    }
    float4 worldCoarse = mul(g_ModelMatrix, float4(coarse, 1.0));
    output.coarsePos = worldCoarse.xyz;

    return output;
}

// Standard vertex shader (non-tessellation path)
// Used when tessellation is disabled
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

cbuffer FrameConstants : register(b1) {
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    // ... rest of frame constants not needed for basic VS
};

PSInput VSMainNoTess(VSInput input) {
    PSInput output;

    // Transform position to world space
    float4 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0));
    output.worldPos = worldPos.xyz;

    // Transform to clip space
    output.position = mul(g_ViewProjectionMatrix, worldPos);

    // Transform normal and tangent
    output.normal = normalize(mul((float3x3)g_NormalMatrix, input.normal));
    output.tangent.xyz = normalize(mul((float3x3)g_ModelMatrix, input.tangent.xyz));
    output.tangent.w = input.tangent.w;

    // Pass through texture coordinates and color
    output.texCoord = input.texCoord;
    output.color = input.color;

    return output;
}
