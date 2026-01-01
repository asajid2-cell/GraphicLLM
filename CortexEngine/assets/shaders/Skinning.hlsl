// Skinning.hlsl
// GPU skinning for skeletal animation.
// Supports linear blend skinning and dual quaternion skinning.
// Reference: "Real-Time Rendering" - Chapter 4 (Transforms)

#include "Common.hlsli"

// Maximum bones per skeleton
#define MAX_BONES 256

// Maximum bone influences per vertex
#define MAX_BONES_PER_VERTEX 4

// Skinning constant buffer
cbuffer SkinningCB : register(b3) {
    uint g_BoneCount;
    uint g_UseDualQuaternion;   // 0 = linear blend, 1 = dual quaternion
    uint g_SkinningPadding[2];
};

// Bone matrices (inverse bind * world)
StructuredBuffer<float4x4> g_BoneMatrices : register(t10);

// Dual quaternion skinning data
struct DualQuaternion {
    float4 real;    // Rotation quaternion
    float4 dual;    // Translation dual part
};

StructuredBuffer<DualQuaternion> g_BoneDualQuaternions : register(t11);

// Skinned vertex input
struct SkinnedVertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD0;
    uint4 boneIndices : BLENDINDICES;   // Bone indices (max 4)
    float4 boneWeights : BLENDWEIGHT;   // Bone weights (must sum to 1)
};

// Skinned vertex output
struct SkinnedVertexOutput {
    float3 position;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

// Linear blend skinning (LBS)
SkinnedVertexOutput SkinVertexLinear(SkinnedVertexInput input) {
    SkinnedVertexOutput output;

    float4 skinnedPos = float4(0, 0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);
    float3 skinnedTangent = float3(0, 0, 0);

    float4 originalPos = float4(input.position, 1.0);
    float3 originalNormal = input.normal;
    float3 originalTangent = input.tangent.xyz;

    // Accumulate weighted bone transforms
    [unroll]
    for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
        uint boneIdx = input.boneIndices[i];
        float weight = input.boneWeights[i];

        if (weight > 0.0 && boneIdx < g_BoneCount) {
            float4x4 boneMatrix = g_BoneMatrices[boneIdx];

            skinnedPos += mul(boneMatrix, originalPos) * weight;
            skinnedNormal += mul((float3x3)boneMatrix, originalNormal) * weight;
            skinnedTangent += mul((float3x3)boneMatrix, originalTangent) * weight;
        }
    }

    output.position = skinnedPos.xyz;
    output.normal = normalize(skinnedNormal);
    output.tangent = normalize(skinnedTangent);
    output.bitangent = cross(output.normal, output.tangent) * input.tangent.w;

    return output;
}

// Quaternion multiplication
float4 QuatMul(float4 q1, float4 q2) {
    return float4(
        q1.w * q2.xyz + q2.w * q1.xyz + cross(q1.xyz, q2.xyz),
        q1.w * q2.w - dot(q1.xyz, q2.xyz)
    );
}

// Quaternion conjugate
float4 QuatConj(float4 q) {
    return float4(-q.xyz, q.w);
}

// Rotate vector by quaternion
float3 QuatRotate(float4 q, float3 v) {
    float4 qv = float4(v, 0);
    float4 result = QuatMul(QuatMul(q, qv), QuatConj(q));
    return result.xyz;
}

// Dual quaternion multiplication
DualQuaternion DQMul(DualQuaternion a, DualQuaternion b) {
    DualQuaternion result;
    result.real = QuatMul(a.real, b.real);
    result.dual = QuatMul(a.real, b.dual) + QuatMul(a.dual, b.real);
    return result;
}

// Normalize dual quaternion
DualQuaternion DQNormalize(DualQuaternion dq) {
    float len = length(dq.real);
    DualQuaternion result;
    result.real = dq.real / len;
    result.dual = dq.dual / len;
    return result;
}

// Blend dual quaternions
DualQuaternion DQBlend(DualQuaternion dqs[MAX_BONES_PER_VERTEX], float weights[MAX_BONES_PER_VERTEX]) {
    DualQuaternion result;
    result.real = float4(0, 0, 0, 0);
    result.dual = float4(0, 0, 0, 0);

    // Ensure shortest path (flip sign if necessary)
    float4 baseReal = dqs[0].real;

    [unroll]
    for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
        float sign = (dot(baseReal, dqs[i].real) < 0.0) ? -1.0 : 1.0;
        result.real += dqs[i].real * weights[i] * sign;
        result.dual += dqs[i].dual * weights[i] * sign;
    }

    return DQNormalize(result);
}

// Transform point by dual quaternion
float3 DQTransformPoint(DualQuaternion dq, float3 p) {
    // Rotation
    float3 rotated = QuatRotate(dq.real, p);

    // Translation
    float4 t = QuatMul(dq.dual, QuatConj(dq.real)) * 2.0;

    return rotated + t.xyz;
}

// Transform vector by dual quaternion (rotation only)
float3 DQTransformVector(DualQuaternion dq, float3 v) {
    return QuatRotate(dq.real, v);
}

// Dual quaternion skinning (DQS)
SkinnedVertexOutput SkinVertexDualQuaternion(SkinnedVertexInput input) {
    SkinnedVertexOutput output;

    DualQuaternion dqs[MAX_BONES_PER_VERTEX];
    float weights[MAX_BONES_PER_VERTEX];

    // Gather dual quaternions
    [unroll]
    for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
        uint boneIdx = input.boneIndices[i];
        float weight = input.boneWeights[i];

        if (weight > 0.0 && boneIdx < g_BoneCount) {
            dqs[i] = g_BoneDualQuaternions[boneIdx];
            weights[i] = weight;
        } else {
            dqs[i].real = float4(0, 0, 0, 1);
            dqs[i].dual = float4(0, 0, 0, 0);
            weights[i] = 0.0;
        }
    }

    // Blend dual quaternions
    DualQuaternion blended = DQBlend(dqs, weights);

    // Transform position
    output.position = DQTransformPoint(blended, input.position);

    // Transform normal and tangent (rotation only)
    output.normal = normalize(DQTransformVector(blended, input.normal));
    output.tangent = normalize(DQTransformVector(blended, input.tangent.xyz));
    output.bitangent = cross(output.normal, output.tangent) * input.tangent.w;

    return output;
}

// Main skinning function (selects method based on flag)
SkinnedVertexOutput SkinVertex(SkinnedVertexInput input) {
    if (g_UseDualQuaternion > 0) {
        return SkinVertexDualQuaternion(input);
    } else {
        return SkinVertexLinear(input);
    }
}

// Compute shader for GPU skinning (optional - for vertex buffer skinning)
RWStructuredBuffer<float3> g_SkinnedPositions : register(u0);
RWStructuredBuffer<float3> g_SkinnedNormals : register(u1);
RWStructuredBuffer<float3> g_SkinnedTangents : register(u2);

StructuredBuffer<float3> g_SourcePositions : register(t0);
StructuredBuffer<float3> g_SourceNormals : register(t1);
StructuredBuffer<float4> g_SourceTangents : register(t2);
StructuredBuffer<uint4> g_SourceBoneIndices : register(t3);
StructuredBuffer<float4> g_SourceBoneWeights : register(t4);

cbuffer SkinningDispatchCB : register(b0) {
    uint g_VertexCount;
    uint g_DispatchPadding[3];
};

[numthreads(64, 1, 1)]
void CSSkinVertices(uint3 dispatchID : SV_DispatchThreadID) {
    uint vertexIdx = dispatchID.x;

    if (vertexIdx >= g_VertexCount) {
        return;
    }

    float3 position = g_SourcePositions[vertexIdx];
    float3 normal = g_SourceNormals[vertexIdx];
    float4 tangent = g_SourceTangents[vertexIdx];
    uint4 boneIndices = g_SourceBoneIndices[vertexIdx];
    float4 boneWeights = g_SourceBoneWeights[vertexIdx];

    float3 skinnedPos = float3(0, 0, 0);
    float3 skinnedNormal = float3(0, 0, 0);
    float3 skinnedTangent = float3(0, 0, 0);

    float4 originalPos = float4(position, 1.0);

    [unroll]
    for (int i = 0; i < 4; i++) {
        uint boneIdx;
        float weight;

        switch (i) {
            case 0: boneIdx = boneIndices.x; weight = boneWeights.x; break;
            case 1: boneIdx = boneIndices.y; weight = boneWeights.y; break;
            case 2: boneIdx = boneIndices.z; weight = boneWeights.z; break;
            case 3: boneIdx = boneIndices.w; weight = boneWeights.w; break;
        }

        if (weight > 0.0 && boneIdx < g_BoneCount) {
            float4x4 boneMatrix = g_BoneMatrices[boneIdx];

            skinnedPos += mul(boneMatrix, originalPos).xyz * weight;
            skinnedNormal += mul((float3x3)boneMatrix, normal) * weight;
            skinnedTangent += mul((float3x3)boneMatrix, tangent.xyz) * weight;
        }
    }

    g_SkinnedPositions[vertexIdx] = skinnedPos;
    g_SkinnedNormals[vertexIdx] = normalize(skinnedNormal);
    g_SkinnedTangents[vertexIdx] = normalize(skinnedTangent);
}

// Morphing/blend shapes support
struct MorphTarget {
    float3 positionDelta;
    float3 normalDelta;
    float3 tangentDelta;
};

StructuredBuffer<MorphTarget> g_MorphTargets : register(t5);
StructuredBuffer<float> g_MorphWeights : register(t6);

cbuffer MorphCB : register(b1) {
    uint g_MorphTargetCount;
    uint g_MorphVertexCount;
    uint g_MorphPadding[2];
};

[numthreads(64, 1, 1)]
void CSApplyMorphTargets(uint3 dispatchID : SV_DispatchThreadID) {
    uint vertexIdx = dispatchID.x;

    if (vertexIdx >= g_MorphVertexCount) {
        return;
    }

    float3 position = g_SourcePositions[vertexIdx];
    float3 normal = g_SourceNormals[vertexIdx];
    float3 tangent = g_SourceTangents[vertexIdx].xyz;

    // Apply morph targets
    for (uint i = 0; i < g_MorphTargetCount; i++) {
        float weight = g_MorphWeights[i];
        if (weight > 0.001) {
            uint morphIdx = i * g_MorphVertexCount + vertexIdx;
            MorphTarget morph = g_MorphTargets[morphIdx];

            position += morph.positionDelta * weight;
            normal += morph.normalDelta * weight;
            tangent += morph.tangentDelta * weight;
        }
    }

    g_SkinnedPositions[vertexIdx] = position;
    g_SkinnedNormals[vertexIdx] = normalize(normal);
    g_SkinnedTangents[vertexIdx] = normalize(tangent);
}
