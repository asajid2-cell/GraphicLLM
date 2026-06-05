// DebugBlitVisibility.hlsl
// Fullscreen visualization of the visibility buffer and per-pixel identity.

Texture2D<uint2> g_VisibilityTexture : register(t0);
cbuffer DebugBlitConstants : register(b0) {
    uint g_DebugMode;      // 0=payload/instance, 1=material id, 2=stable object id, 3..6=material-table policy columns
    uint g_InstanceCount;
    uint g_MaterialCount;
    uint g_Padding;
};
struct VBInstanceData {
    float4x4 worldMatrix;
    float4x4 prevWorldMatrix;
    float4x4 normalMatrix;
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint _padAlign[3];
    float4 boundingSphere;
    float4 prevCenterWS;
    uint cullingId;
    uint flags;
    float depthBiasNdc;
    uint _pad0;
};
StructuredBuffer<VBInstanceData> g_Instances : register(t1);
struct VBMaterialConstants {
    float4 albedo;
    float metallic;
    float roughness;
    float ao;
    float _pad0;
    uint4 textureIndices;
    uint4 textureIndices2;
    float4 emissiveFactorStrength;
    float4 extraParams;
    float4 coatParams;
    float4 transmissionParams;
    float4 specularParams;
    uint4 textureIndices3;
    uint4 textureIndices4;
    float alphaCutoff;
    uint alphaMode;
    uint doubleSided;
    uint materialClass;
    uint4 policyParams;
};
StructuredBuffer<VBMaterialConstants> g_Materials : register(t2);
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

static float3 PolicyColor(uint id, uint mode) {
    if (mode == 3u) {
        static const float3 familyColors[16] = {
            float3(0.48, 0.48, 0.48),
            float3(0.70, 0.62, 0.52),
            float3(0.95, 0.92, 0.82),
            float3(0.62, 0.36, 0.16),
            float3(0.55, 0.58, 0.60),
            float3(0.95, 0.78, 0.38),
            float3(0.58, 0.86, 1.00),
            float3(0.55, 0.38, 0.78),
            float3(0.20, 0.55, 0.95),
            float3(0.10, 0.35, 0.75),
            float3(1.00, 0.16, 0.72),
            float3(0.05, 0.80, 1.00),
            float3(0.48, 0.46, 0.42),
            float3(0.04, 0.04, 0.05),
            float3(0.03, 0.42, 0.82),
            float3(0.92, 0.92, 0.98),
        };
        return familyColors[min(id, 15u)];
    }
    return ColorFromID(id + mode * 1024u);
}

float4 PSMain(VSOutput input) : SV_Target0 {
    uint2 pixelCoord = uint2(input.position.xy);
    uint2 vis = g_VisibilityTexture.Load(int3(pixelCoord, 0));

    const uint tri = vis.x;
    const uint inst = vis.y;

    if (tri == 0xFFFFFFFFu && inst == 0xFFFFFFFFu) {
        return float4(0, 0, 0, 1);
    }

    uint id = inst;
    if (g_DebugMode != 0u) {
        if (inst >= g_InstanceCount) {
            return float4(1, 0, 1, 1);
        }
        VBInstanceData instance = g_Instances[inst];
        if (g_DebugMode == 1u) {
            id = instance.materialIndex;
        } else if (g_DebugMode == 2u) {
            id = instance.cullingId;
        } else if (g_DebugMode >= 3u && g_DebugMode <= 6u) {
            if (instance.materialIndex >= g_MaterialCount) {
                return float4(1, 0, 1, 1);
            }
            VBMaterialConstants material = g_Materials[instance.materialIndex];
            if (g_DebugMode == 3u) {
                id = material.policyParams.x;
            } else if (g_DebugMode == 4u) {
                id = material.policyParams.y;
            } else if (g_DebugMode == 5u) {
                id = material.policyParams.z;
            } else {
                id = material.policyParams.w;
            }
            return float4(PolicyColor(id, g_DebugMode), 1.0f);
        }
    }

    float3 c = ColorFromID(id);
    // Small modulation so triangle boundaries show up a bit.
    float t = g_DebugMode == 0u ? (float)((tri % 17u) + 1u) / 18.0f : 1.0f;
    return float4(c * t, 1.0f);
}

