// GPUCulling.hlsl
// GPU frustum culling + indirect command compaction

struct GPUInstanceData {
    float4x4 modelMatrix;
    float4 boundingSphere;  // xyz = center (object space), w = radius
    uint meshIndex;
    uint materialIndex;
    uint flags;
    uint _pad;
};

struct IndirectCommand {
    uint2 objectCBV;
    uint2 materialCBV;
    uint2 vertexBuffer;
    uint vertexBufferSize;
    uint vertexBufferStride;
    uint2 indexBuffer;
    uint indexBufferSize;
    uint indexBufferFormat;
    uint indexCountPerInstance;
    uint instanceCount;
    uint startIndexLocation;
    int baseVertexLocation;
    uint startInstanceLocation;
    uint padding;
};

cbuffer CullConstants : register(b0) {
    float4x4 g_ViewProj;
    float4 g_FrustumPlanes[6];
    float3 g_CameraPos;
    uint g_InstanceCount;
    uint g_ForceVisible;
    uint3 g_Pad;
};

StructuredBuffer<GPUInstanceData> g_Instances : register(t0);
StructuredBuffer<IndirectCommand> g_AllCommands : register(t1);
RWStructuredBuffer<IndirectCommand> g_VisibleCommands : register(u0);
RWByteAddressBuffer g_CommandCount : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= g_InstanceCount) {
        return;
    }

    GPUInstanceData instance = g_Instances[instanceIdx];

    float3 centerOS = instance.boundingSphere.xyz;
    float radius = instance.boundingSphere.w;

    float3 centerWS = mul(instance.modelMatrix, float4(centerOS, 1.0)).xyz;

    float3 scaleX = float3(instance.modelMatrix[0][0], instance.modelMatrix[1][0], instance.modelMatrix[2][0]);
    float3 scaleY = float3(instance.modelMatrix[0][1], instance.modelMatrix[1][1], instance.modelMatrix[2][1]);
    float3 scaleZ = float3(instance.modelMatrix[0][2], instance.modelMatrix[1][2], instance.modelMatrix[2][2]);
    float maxScale = max(max(length(scaleX), length(scaleY)), length(scaleZ));
    float worldRadius = radius * maxScale;

    bool visible = (g_ForceVisible != 0);
    if (!visible) {
        visible = true;
        [unroll]
        for (int i = 0; i < 6; i++) {
            float dist = dot(g_FrustumPlanes[i].xyz, centerWS) + g_FrustumPlanes[i].w;
            if (dist < -worldRadius) {
                visible = false;
                break;
            }
        }
    }

    if (visible) {
        uint visibleIdx;
        g_CommandCount.InterlockedAdd(0, 1, visibleIdx);
        g_VisibleCommands[visibleIdx] = g_AllCommands[instanceIdx];
    }
}
