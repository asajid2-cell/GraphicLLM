// VBMotionVectors.hlsl
// Visibility-buffer-driven per-object motion vectors.
// Outputs per-pixel screen-space velocity in UV units (prevUV - currUV).

cbuffer MotionConstants : register(b0)
{
    uint  g_Width;
    uint  g_Height;
    float g_RcpWidth;
    float g_RcpHeight;
    uint  g_MeshCount;
    uint3 _pad0;
};

// FrameConstants layout (subset copied from MotionVectors.hlsl for consistent offsets).
cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    float4   g_TimeAndExposure;
    float4   g_AmbientColor;
    uint4    g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    float4   g_AOParams;
    float4   g_BloomParams;
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
};

// Instance data structure (matches VBInstanceData in C++)
struct VBInstanceData
{
    float4x4 worldMatrix;
    float4x4 prevWorldMatrix;
    float4x4 normalMatrix;
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint _padAlign[3];      // explicit padding to match C++ alignment (16-byte boundary for float4)
    float4 boundingSphere;  // xyz = center (object space), w = radius
    float4 prevCenterWS;    // xyz = previous frame center (world space)
    uint cullingId;         // packed gen<<16|slot
    uint flags;
    float depthBiasNdc;
    uint _pad0;
};

struct VBMeshTableEntry
{
    uint vertexBufferIndex;
    uint indexBufferIndex;
    uint vertexStrideBytes;
    uint indexFormat; // 0 = R32_UINT, 1 = R16_UINT
};

struct Vertex
{
    float3 position;
    float3 normal;
    float4 tangent;
    float2 texCoord;
};

Texture2D<uint2> g_VisibilityBuffer : register(t0);
StructuredBuffer<VBInstanceData> g_Instances : register(t1);
StructuredBuffer<VBMeshTableEntry> g_MeshTable : register(t3);

RWTexture2D<float2> g_VelocityOut : register(u0);

Vertex LoadVertex(ByteAddressBuffer vertexBuffer, uint vertexIndex, uint vertexStrideBytes)
{
    uint offset = vertexIndex * vertexStrideBytes;
    Vertex v;
    v.position = asfloat(vertexBuffer.Load3(offset + 0));
    v.normal = asfloat(vertexBuffer.Load3(offset + 12));
    v.tangent = asfloat(vertexBuffer.Load4(offset + 24));
    v.texCoord = asfloat(vertexBuffer.Load2(offset + 40));
    return v;
}

uint LoadIndex16(ByteAddressBuffer indexBuffer, uint byteOffset)
{
    uint word = indexBuffer.Load(byteOffset & ~3u);
    return ((byteOffset & 2u) != 0u) ? ((word >> 16) & 0xFFFFu) : (word & 0xFFFFu);
}

uint LoadIndex(ByteAddressBuffer indexBuffer, uint byteOffset, uint indexFormat)
{
    return (indexFormat == 1u) ? LoadIndex16(indexBuffer, byteOffset) : indexBuffer.Load(byteOffset);
}

uint3 LoadTriangleIndices(ByteAddressBuffer indexBuffer, uint triangleID, uint firstIndex, uint baseVertex, uint indexFormat)
{
    uint indexStrideBytes = (indexFormat == 1u) ? 2u : 4u;
    uint indexOffset = (firstIndex + triangleID * 3u) * indexStrideBytes;

    uint3 indices;
    indices.x = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 0u, indexFormat) + baseVertex;
    indices.y = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 1u, indexFormat) + baseVertex;
    indices.z = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 2u, indexFormat) + baseVertex;
    return indices;
}

// Compute screen-space barycentrics using edge functions.
// This matches the GPU rasterizer's approach for determining triangle coverage.
// Edge functions are more numerically stable for thin triangles.
float3 ComputeScreenSpaceBarycentrics(float2 p, float2 v0, float2 v1, float2 v2)
{
    // Use edge function (signed area) method - same as GPU rasterizers
    // Edge function E(p) = (p - v0) x (v1 - v0) where x is 2D cross product

    // Edge vectors
    float2 e01 = v1 - v0;  // Edge from v0 to v1
    float2 e12 = v2 - v1;  // Edge from v1 to v2
    float2 e20 = v0 - v2;  // Edge from v2 to v0

    // Vectors from each vertex to point p
    float2 d0 = p - v0;
    float2 d1 = p - v1;
    float2 d2 = p - v2;

    // Edge functions (2D cross products give signed areas)
    // These are proportional to the barycentric weights
    float w0 = e12.x * d1.y - e12.y * d1.x;  // Area opposite to v0
    float w1 = e20.x * d2.y - e20.y * d2.x;  // Area opposite to v1
    float w2 = e01.x * d0.y - e01.y * d0.x;  // Area opposite to v2

    // Total signed area (for normalization)
    float area = w0 + w1 + w2;

    // Handle degenerate triangles
    if (abs(area) < 1e-10f)
    {
        return float3(0.333333f, 0.333333f, 0.333334f);
    }

    // Normalize to get barycentrics that sum to 1
    float invArea = 1.0f / area;
    return float3(w0 * invArea, w1 * invArea, w2 * invArea);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixelCoord = dispatchThreadID.xy;
    if (pixelCoord.x >= g_Width || pixelCoord.y >= g_Height)
    {
        return;
    }

    uint2 visData = g_VisibilityBuffer[pixelCoord];
    uint triangleID = visData.x;
    uint instanceID = visData.y;

    if (triangleID == 0xFFFFFFFFu && instanceID == 0xFFFFFFFFu)
    {
        g_VelocityOut[pixelCoord] = float2(0.0f, 0.0f);
        return;
    }

    VBInstanceData instance = g_Instances[instanceID];
    if (g_MeshCount == 0u || instance.meshIndex >= g_MeshCount)
    {
        g_VelocityOut[pixelCoord] = float2(0.0f, 0.0f);
        return;
    }

    // Robustness guard: skip invalid triangle IDs.
    const uint first = instance.firstIndex;
    const uint count = instance.indexCount;
    const uint triFirst = first + triangleID * 3u;
    if (count < 3u || triFirst + 2u >= first + count)
    {
        g_VelocityOut[pixelCoord] = float2(0.0f, 0.0f);
        return;
    }

    VBMeshTableEntry mesh = g_MeshTable[instance.meshIndex];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[mesh.vertexBufferIndex];
    ByteAddressBuffer indexBuffer = ResourceDescriptorHeap[mesh.indexBufferIndex];

    uint3 indices = LoadTriangleIndices(indexBuffer, triangleID, instance.firstIndex, instance.baseVertex, mesh.indexFormat);
    Vertex v0 = LoadVertex(vertexBuffer, indices.x, mesh.vertexStrideBytes);
    Vertex v1 = LoadVertex(vertexBuffer, indices.y, mesh.vertexStrideBytes);
    Vertex v2 = LoadVertex(vertexBuffer, indices.z, mesh.vertexStrideBytes);

    // Jittered clip positions (match visibility pass rasterization) for barycentrics.
    float4 clip0 = mul(g_ViewProjectionMatrix, mul(instance.worldMatrix, float4(v0.position, 1.0f)));
    float4 clip1 = mul(g_ViewProjectionMatrix, mul(instance.worldMatrix, float4(v1.position, 1.0f)));
    float4 clip2 = mul(g_ViewProjectionMatrix, mul(instance.worldMatrix, float4(v2.position, 1.0f)));

    if (abs(clip0.w) < 1e-6f || abs(clip1.w) < 1e-6f || abs(clip2.w) < 1e-6f)
    {
        g_VelocityOut[pixelCoord] = float2(0.0f, 0.0f);
        return;
    }

    float3 ndc0 = clip0.xyz / clip0.w;
    float3 ndc1 = clip1.xyz / clip1.w;
    float3 ndc2 = clip2.xyz / clip2.w;

    float2 screen0 = float2(ndc0.x * 0.5f + 0.5f, 0.5f - ndc0.y * 0.5f);
    float2 screen1 = float2(ndc1.x * 0.5f + 0.5f, 0.5f - ndc1.y * 0.5f);
    float2 screen2 = float2(ndc2.x * 0.5f + 0.5f, 0.5f - ndc2.y * 0.5f);

    float2 pixelUV = (float2(pixelCoord) + 0.5f) * float2(g_RcpWidth, g_RcpHeight);
    float3 bary = ComputeScreenSpaceBarycentrics(pixelUV, screen0, screen1, screen2);

    // Perspective correction (same pattern as MaterialResolve).
    float3 baryPersp = bary / float3(clip0.w, clip1.w, clip2.w);
    float barySum = baryPersp.x + baryPersp.y + baryPersp.z;
    if (barySum > 1e-7f)
    {
        bary = baryPersp / barySum;
    }
    bary = saturate(bary);

    float3 posOS = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    float3 worldCurr = mul(instance.worldMatrix, float4(posOS, 1.0f)).xyz;
    float3 worldPrev = mul(instance.prevWorldMatrix, float4(posOS, 1.0f)).xyz;

    float4 currClip = mul(g_ViewProjectionNoJitter, float4(worldCurr, 1.0f));
    float4 prevClip = mul(g_PrevViewProjMatrix, float4(worldPrev, 1.0f));

    if (abs(currClip.w) < 1e-6f || abs(prevClip.w) < 1e-6f)
    {
        g_VelocityOut[pixelCoord] = float2(0.0f, 0.0f);
        return;
    }

    float2 currNdc = currClip.xy / currClip.w;
    float2 prevNdc = prevClip.xy / prevClip.w;

    float2 currUV;
    currUV.x = currNdc.x * 0.5f + 0.5f;
    currUV.y = 0.5f - currNdc.y * 0.5f;

    float2 prevUV;
    prevUV.x = prevNdc.x * 0.5f + 0.5f;
    prevUV.y = 0.5f - prevNdc.y * 0.5f;

    g_VelocityOut[pixelCoord] = prevUV - currUV;
}
