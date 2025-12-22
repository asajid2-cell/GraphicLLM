// ClusteredLightCulling.hlsl
// Builds per-cluster light lists for deferred shading.
//
// Output:
//  g_ClusterRanges[cluster] = { baseOffset, count }
//  g_ClusterLightIndices[baseOffset + i] = lightIndex
//
// Cluster layout: X/Y tiles in screen space, Z slices in view space (log distributed).

cbuffer ClusterConstants : register(b0)
{
    float4x4 g_ViewMatrix;
    float4   g_ProjectionParams; // x=proj11, y=proj22, z=nearZ, w=farZ
    uint4    g_ScreenAndCluster; // x=width, y=height, z=clusterCountX, w=clusterCountY
    uint4    g_ClusterParams;    // x=clusterCountZ, y=maxLightsPerCluster, z=lightCount, w=unused
};

struct Light
{
    // xyz: position (for point/spot/area), w: type (0 = directional, 1 = point, 2 = spot, 3 = rect area)
    float4 position_type;
    // xyz: direction (for dir/spot, normalized), w: inner cone cos (spot)
    float4 direction_cosInner;
    // rgb: color * intensity, w: range (for point/spot)
    float4 color_range;
    // x: outer cone cos (spot), y: shadow index (if used), z,w: reserved / area params
    float4 params;
};

StructuredBuffer<Light> g_Lights : register(t0);
RWStructuredBuffer<uint2> g_ClusterRanges : register(u0);
RWStructuredBuffer<uint> g_ClusterLightIndices : register(u1);

float PowSafe(float a, float b)
{
    return pow(max(a, 1e-6f), b);
}

float ComputeSliceNear(float nearZ, float farZ, uint slice, uint sliceCount)
{
    float t0 = (float)slice / (float)sliceCount;
    return nearZ * PowSafe(farZ / nearZ, t0);
}

float ComputeSliceFar(float nearZ, float farZ, uint slice, uint sliceCount)
{
    float t1 = (float)(slice + 1u) / (float)sliceCount;
    return nearZ * PowSafe(farZ / nearZ, t1);
}

bool SphereIntersectsAabb(float3 center, float radius, float3 aabbMin, float3 aabbMax)
{
    float3 closest = clamp(center, aabbMin, aabbMax);
    float3 d = center - closest;
    return dot(d, d) <= radius * radius;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint width = g_ScreenAndCluster.x;
    uint height = g_ScreenAndCluster.y;
    uint clusterCountX = g_ScreenAndCluster.z;
    uint clusterCountY = g_ScreenAndCluster.w;
    uint clusterCountZ = g_ClusterParams.x;
    uint maxLightsPerCluster = g_ClusterParams.y;
    uint lightCount = g_ClusterParams.z;

    uint clusterCountXY = clusterCountX * clusterCountY;
    uint clusterCountTotal = clusterCountXY * clusterCountZ;
    uint clusterIndex = dtid.x;
    if (clusterIndex >= clusterCountTotal)
        return;

    uint z = clusterIndex / clusterCountXY;
    uint rem = clusterIndex - z * clusterCountXY;
    uint y = rem / clusterCountX;
    uint x = rem - y * clusterCountX;

    float proj11 = g_ProjectionParams.x;
    float proj22 = g_ProjectionParams.y;
    float nearZ = g_ProjectionParams.z;
    float farZ = g_ProjectionParams.w;

    float zNearSlice = ComputeSliceNear(nearZ, farZ, z, clusterCountZ);
    float zFarSlice = ComputeSliceFar(nearZ, farZ, z, clusterCountZ);

    uint tileW = (width + clusterCountX - 1u) / clusterCountX;
    uint tileH = (height + clusterCountY - 1u) / clusterCountY;

    uint px0 = x * tileW;
    uint py0 = y * tileH;
    uint px1 = min(px0 + tileW, width);
    uint py1 = min(py0 + tileH, height);

    float ndcX0 = ((float)px0 / (float)width) * 2.0f - 1.0f;
    float ndcX1 = ((float)px1 / (float)width) * 2.0f - 1.0f;
    float ndcY0 = 1.0f - ((float)py0 / (float)height) * 2.0f;
    float ndcY1 = 1.0f - ((float)py1 / (float)height) * 2.0f;

    float x0Near = ndcX0 * zNearSlice / proj11;
    float x1Near = ndcX1 * zNearSlice / proj11;
    float x0Far = ndcX0 * zFarSlice / proj11;
    float x1Far = ndcX1 * zFarSlice / proj11;

    float y0Near = ndcY0 * zNearSlice / proj22;
    float y1Near = ndcY1 * zNearSlice / proj22;
    float y0Far = ndcY0 * zFarSlice / proj22;
    float y1Far = ndcY1 * zFarSlice / proj22;

    float3 aabbMin = float3(min(x0Near, x0Far), min(min(y0Near, y1Near), min(y0Far, y1Far)), zNearSlice);
    float3 aabbMax = float3(max(x1Near, x1Far), max(max(y0Near, y1Near), max(y0Far, y1Far)), zFarSlice);

    uint baseOffset = clusterIndex * maxLightsPerCluster;
    uint count = 0u;

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        Light light = g_Lights[i];
        float type = light.position_type.w;
        if (type < 0.5f) {
            // Directional lights are handled separately (sun).
            continue;
        }

        float range = light.color_range.w;
        if (range <= 0.0f) {
            continue;
        }

        float3 posVS = mul(g_ViewMatrix, float4(light.position_type.xyz, 1.0f)).xyz;
        if (SphereIntersectsAabb(posVS, range, aabbMin, aabbMax))
        {
            if (count < maxLightsPerCluster)
            {
                g_ClusterLightIndices[baseOffset + count] = i;
                ++count;
            }
        }
    }

    g_ClusterRanges[clusterIndex] = uint2(baseOffset, count);
}

