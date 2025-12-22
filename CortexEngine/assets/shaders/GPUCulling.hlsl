// GPUCulling.hlsl
// GPU frustum culling + indirect command compaction

struct GPUInstanceData {
    float4x4 modelMatrix;
    float4 boundingSphere;  // xyz = center (object space), w = radius
    float4 prevCenterWS;    // xyz = previous frame center (world space)
    uint meshIndex;
    uint materialIndex;
    uint flags;
    uint cullingId;
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
    uint4 g_OcclusionParams0; // x=forceVisible, y=hzbEnabled, z=hzbMipCount, w=streakThreshold
    uint4 g_OcclusionParams1; // x=hzbWidth, y=hzbHeight, z=historySize, w=debugEnabled
    float4 g_OcclusionParams2; // x=invW, y=invH, z=proj00, w=proj11
    float4 g_OcclusionParams3; // x=near, y=far, z=epsilon, w=cameraMotionWS
    float4x4 g_HZBViewMatrix;
    float4x4 g_HZBViewProjMatrix;
    float4 g_HZBCameraPos;
};

StructuredBuffer<GPUInstanceData> g_Instances : register(t0);
StructuredBuffer<IndirectCommand> g_AllCommands : register(t1);
ByteAddressBuffer g_OcclusionHistoryIn : register(t3);
Texture2D<float> g_HZB : register(t2);
RWStructuredBuffer<IndirectCommand> g_VisibleCommands : register(u0);
RWByteAddressBuffer g_CommandCount : register(u1);
RWByteAddressBuffer g_OcclusionHistoryOut : register(u2);
RWByteAddressBuffer g_Debug : register(u3);

static uint CeilDivPow2(uint value, uint mip) {
    const uint denom = (1u << mip);
    return (value + denom - 1u) >> mip;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint instanceIdx = DTid.x;
    if (instanceIdx >= g_InstanceCount) {
        return;
    }

    const uint debugEnabled = g_OcclusionParams1.w;
    if (debugEnabled != 0) {
        uint unused;
        g_Debug.InterlockedAdd(0, 1, unused); // tested
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

    // Inflate bounds based on object motion to avoid false occlusion/popping
    // when instances move while the HZB is from the previous frame.
    const float motion = length(centerWS - instance.prevCenterWS.xyz);
    const float cameraMotionWS = max(g_OcclusionParams3.w, 0.0f);
    worldRadius += (motion + cameraMotionWS);

    const uint forceVisible = g_OcclusionParams0.x;
    const uint hzbEnabled = g_OcclusionParams0.y;
    const uint hzbMipCount = g_OcclusionParams0.z;
    const uint streakThreshold = max(1u, g_OcclusionParams0.w);

    const uint hzbWidth = g_OcclusionParams1.x;
    const uint hzbHeight = g_OcclusionParams1.y;

    const float2 hzbInvSize = g_OcclusionParams2.xy;
    const float2 hzbProjScale = g_OcclusionParams2.zw;
    const float hzbEpsilon = g_OcclusionParams3.z;

    bool visible = (forceVisible != 0);
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
    if (debugEnabled != 0 && !visible) {
        uint unused;
        g_Debug.InterlockedAdd(4, 1, unused); // frustum culled
    }

    // Occlusion culling against the previous frame's HZB (optional).
    // Uses a small streak-based hysteresis to reduce popping and false positives.
    uint prevStreak = 0;
    uint newStreak = 0;
    const uint historySize = g_OcclusionParams1.z;
    const uint historyIdx = instance.cullingId;
    const bool historyValid = (historyIdx < historySize);
    if (hzbEnabled != 0 && historyValid) {
        prevStreak = g_OcclusionHistoryIn.Load(historyIdx * 4u);
    }

    bool occluded = false;
    float dbgNearDepth = 0.0f;
    float dbgHzbDepth = 0.0f;
    uint dbgMip = 0xFFFFFFFFu;
    if (visible && hzbEnabled != 0 && historyValid && hzbMipCount > 0 && hzbWidth > 0 && hzbHeight > 0) {
        const float4 clipCenter = mul(g_HZBViewProjMatrix, float4(centerWS, 1.0f));
        if (abs(clipCenter.w) > 1e-6f) {
            float3 ndcCenter = clipCenter.xyz / clipCenter.w;
            float2 uv;
            uv.x = ndcCenter.x * 0.5f + 0.5f;
            uv.y = 0.5f - ndcCenter.y * 0.5f;

            // Conservative guard: if projection lands outside, skip occlusion.
            if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f) {
                const float viewZ = clipCenter.w;
                if (viewZ > 1e-4f) {
                    const float2 radiusNdc = (worldRadius * hzbProjScale) / viewZ;
                    const float2 radiusPx = abs(radiusNdc) * float2(0.5f * (float)hzbWidth, 0.5f * (float)hzbHeight);
                    const float r = max(1.0f, max(radiusPx.x, radiusPx.y));
                    uint mip = min(hzbMipCount - 1u, (uint)max(0.0f, floor(log2(r))));

                    // Under motion, bias toward more detailed mips to reduce false occlusion
                    // from coarse min-depth tiles that are temporally misaligned.
                    if (motion + cameraMotionWS > 0.001f && mip > 0u) {
                        mip -= 1u;
                    }

                    const uint mipW = max(1u, CeilDivPow2(hzbWidth, mip));
                    const uint mipH = max(1u, CeilDivPow2(hzbHeight, mip));
                    dbgMip = mip;

                    // Sample 4 corners of the projected footprint (conservative):
                    // take the maximum depth among nearest-depth samples so any
                    // "hole" keeps the object visible.
                    const float2 radiusUV = abs(radiusNdc) * 0.5f;
                    const float2 uvMin = saturate(uv - radiusUV);
                    const float2 uvMax = saturate(uv + radiusUV);

                    uint2 coord00 = min((uint2)(uvMin * float2((float)mipW, (float)mipH)), uint2(mipW - 1u, mipH - 1u));
                    uint2 coord10 = min((uint2)(float2(uvMax.x, uvMin.y) * float2((float)mipW, (float)mipH)), uint2(mipW - 1u, mipH - 1u));
                    uint2 coord01 = min((uint2)(float2(uvMin.x, uvMax.y) * float2((float)mipW, (float)mipH)), uint2(mipW - 1u, mipH - 1u));
                    uint2 coord11 = min((uint2)(uvMax * float2((float)mipW, (float)mipH)), uint2(mipW - 1u, mipH - 1u));

                    float hzbDepth = g_HZB.Load(int3((int2)coord00, (int)mip));
                    hzbDepth = max(hzbDepth, g_HZB.Load(int3((int2)coord10, (int)mip)));
                    hzbDepth = max(hzbDepth, g_HZB.Load(int3((int2)coord01, (int)mip)));
                    hzbDepth = max(hzbDepth, g_HZB.Load(int3((int2)coord11, (int)mip)));
                    dbgHzbDepth = hzbDepth;

                    // Compute the sphere's nearest point to the HZB camera in world space.
                    float3 toCenter = centerWS - g_HZBCameraPos.xyz;
                    float lenSq = dot(toCenter, toCenter);
                    float3 dir = (lenSq > 1e-8f) ? (toCenter * rsqrt(lenSq)) : float3(0.0f, 0.0f, 1.0f);
                    float3 nearWS = centerWS - dir * worldRadius;

                    const float4 clipNear = mul(g_HZBViewProjMatrix, float4(nearWS, 1.0f));
                    if (abs(clipNear.w) > 1e-6f) {
                        const float nearDepth = saturate(clipNear.z / clipNear.w);
                        dbgNearDepth = nearDepth;
                        occluded = (nearDepth > (hzbDepth + hzbEpsilon));
                    }
                }
            }
        }
    }

    // Hysteresis: require N consecutive occluded frames before rejecting.
    if (hzbEnabled != 0 && historyValid) {
        if (visible && occluded) {
            newStreak = min(prevStreak + 1u, 255u);
            if (newStreak < streakThreshold) {
                occluded = false;
            }
        } else {
            newStreak = 0u;
        }
        g_OcclusionHistoryOut.Store(historyIdx * 4u, newStreak);
    }

    if (debugEnabled != 0) {
        if (visible && occluded) {
            uint unused;
            g_Debug.InterlockedAdd(8, 1, unused); // occluded
        } else if (visible) {
            uint unused;
            g_Debug.InterlockedAdd(12, 1, unused); // visible
        }

        if (instanceIdx == 0) {
            uint flags = 0;
            flags |= visible ? 1u : 0u;
            flags |= occluded ? 2u : 0u;
            flags |= (hzbEnabled != 0) ? 4u : 0u;
            g_Debug.Store(16, asuint(dbgNearDepth));
            g_Debug.Store(20, asuint(dbgHzbDepth));
            g_Debug.Store(24, dbgMip);
            g_Debug.Store(28, flags);
        }
    }

    if (visible && !occluded) {
        uint visibleIdx;
        g_CommandCount.InterlockedAdd(0, 1, visibleIdx);
        g_VisibleCommands[visibleIdx] = g_AllCommands[instanceIdx];
    }
}
