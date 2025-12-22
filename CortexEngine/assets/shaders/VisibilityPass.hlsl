// VisibilityPass.hlsl
// Phase 1 of visibility buffer rendering: Rasterize primitive IDs.
// Output: R32G32_UINT with (primitiveID, instanceID).

// Root signature:
// b0: View-projection matrix + current mesh index
// t0: Instance data SRV

cbuffer PerFrameData : register(b0) {
    float4x4 g_ViewProj;
    uint g_CurrentMeshIndex;  // Current mesh being drawn
    uint g_MaterialCount;
    uint2 _pad;
};

// Instance data structure (matches VBInstanceData in C++)
struct VBInstanceData {
    float4x4 worldMatrix;
    float4x4 prevWorldMatrix;
    float4x4 normalMatrix; // inverse-transpose (world-space normal transform)
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint3 _pad;
};

StructuredBuffer<VBInstanceData> g_Instances : register(t0);

struct VBMaterialConstants {
    float4 albedo;
    float metallic;
    float roughness;
    float ao;
    float _pad0;
    uint4 textureIndices; // bindless indices: albedo, normal, metallic, roughness
    float alphaCutoff;
    uint alphaMode; // 0=opaque, 1=mask, 2=blend
    uint doubleSided;
    uint _pad1;
};

StructuredBuffer<VBMaterialConstants> g_Materials : register(t1);
SamplerState g_Sampler : register(s0);

static const uint INVALID_BINDLESS_INDEX = 0xFFFFFFFFu;

struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    uint instanceID : SV_InstanceID;
    uint vertexID : SV_VertexID;
};

struct PSInput {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    nointerpolation uint instanceID : INSTANCE_ID;
};

struct PSOutput {
    uint2 visibilityData : SV_Target0;  // x = primitiveID, y = instanceID
};

PSInput VSMain(VSInput input) {
    PSInput output;

    // Fetch instance data
    VBInstanceData instance = g_Instances[input.instanceID];

    // Transform to world space then clip space
    float4 worldPos = mul(instance.worldMatrix, float4(input.position, 1.0));
    output.position = mul(g_ViewProj, worldPos);
    output.texCoord = input.texCoord;

    // Pass through instance ID for pixel shader (no interpolation!)
    output.instanceID = input.instanceID;

    return output;
}

PSOutput PSMain(PSInput input, uint primitiveID : SV_PrimitiveID) {
    PSOutput output;

    // Output visibility buffer payload (primitiveID within this draw + instanceID).
    output.visibilityData = uint2(primitiveID, input.instanceID);

    return output;
}

// Alpha-tested variant: samples baseColor alpha and discards below cutoff.
PSOutput PSMainAlphaTest(PSInput input, uint primitiveID : SV_PrimitiveID) {
    PSOutput output;

    VBInstanceData instance = g_Instances[input.instanceID];

    float alpha = 1.0f;
    float cutoff = 0.5f;
    if (g_MaterialCount > 0 && instance.materialIndex < g_MaterialCount) {
        VBMaterialConstants mat = g_Materials[instance.materialIndex];
        alpha = mat.albedo.a;
        cutoff = mat.alphaCutoff;

        uint albedoIndex = mat.textureIndices.x;
        if (albedoIndex != INVALID_BINDLESS_INDEX) {
            Texture2D<float4> albedoTex = ResourceDescriptorHeap[albedoIndex];
            float4 tex = albedoTex.Sample(g_Sampler, input.texCoord);
            alpha *= tex.a;
        }
    }

    clip(alpha - cutoff);
    output.visibilityData = uint2(primitiveID, input.instanceID);
    return output;
}
