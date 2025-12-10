// VisibilityPass.hlsl (v2 - mesh filtering)
// Phase 1 of visibility buffer rendering: Rasterize triangle IDs
// Output: R32G32_UINT with (triangleID + drawID, instanceID)

// Root signature:
// b0: View-projection matrix + current mesh index
// t0: Instance data SRV

cbuffer PerFrameData : register(b0) {
    float4x4 g_ViewProj;
    uint g_CurrentMeshIndex;  // Current mesh being drawn
    uint3 _pad;
};

// Instance data structure (matches VBInstanceData in C++)
struct VBInstanceData {
    float4x4 worldMatrix;
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint3 _pad;
};

StructuredBuffer<VBInstanceData> g_Instances : register(t0);

struct VSInput {
    float3 position : POSITION;
    uint instanceID : SV_InstanceID;
    uint vertexID : SV_VertexID;
};

struct PSInput {
    float4 position : SV_Position;
    nointerpolation uint instanceID : INSTANCE_ID;
};

struct PSOutput {
    uint2 visibilityData : SV_Target0;  // x = triangleID + drawID, y = instanceID
};

PSInput VSMain(VSInput input) {
    PSInput output;

    // Fetch instance data
    VBInstanceData instance = g_Instances[input.instanceID];

    // Filter out instances that don't use the current mesh
    // Move vertices far outside clip space if mesh doesn't match
    if (instance.meshIndex != g_CurrentMeshIndex) {
        output.position = float4(10000.0, 10000.0, 10000.0, 1.0);  // Far outside clip space
        output.instanceID = 0xFFFFFFFF;                             // Invalid instance ID
        return output;
    }

    // Transform to world space then clip space
    float4 worldPos = mul(instance.worldMatrix, float4(input.position, 1.0));
    output.position = mul(g_ViewProj, worldPos);

    // Pass through instance ID for pixel shader (no interpolation!)
    output.instanceID = input.instanceID;

    return output;
}

PSOutput PSMain(PSInput input, uint primitiveID : SV_PrimitiveID) {
    PSOutput output;

    // Pack triangle ID and draw ID (for now, drawID = 0)
    // Format: triangleID[23:0] | drawID[31:24]
    uint triangleID = primitiveID;
    uint drawID = 0;  // Single draw call for now
    uint packedTriangleAndDraw = (triangleID & 0x00FFFFFF) | ((drawID & 0xFF) << 24);

    // Output visibility buffer payload
    output.visibilityData = uint2(packedTriangleAndDraw, input.instanceID);

    return output;
}
