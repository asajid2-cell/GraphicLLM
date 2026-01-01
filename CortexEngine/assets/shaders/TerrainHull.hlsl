// TerrainHull.hlsl
// Hull shader for terrain tessellation.
// Computes per-edge tessellation factors based on distance and screen-space edge length.

#include "BiomeMaterials.hlsli"

// Tessellation parameters from constant buffer
cbuffer TessellationConstants : register(b5) {
    float4x4 g_ViewProj;
    float4 g_CameraPosition;      // xyz = camera world pos, w = unused
    float4 g_TessParams;          // x = maxTessFactor, y = minTessFactor, z = targetEdgeLength (pixels), w = distanceFalloff
    float4 g_ScreenParams;        // x = width, y = height, z = 1/width, w = 1/height
    float4 g_LODParams;           // x = tessStartDist, y = tessEndDist, z = heightScale, w = unused
};

// Input control point (from vertex shader)
struct HullInput {
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;           // Biome blend data
    float3 coarsePos : COARSE_POS;  // Coarse LOD position for geomorphing
};

// Output control point
struct HullOutput {
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 coarsePos : COARSE_POS;
};

// Patch constant data (tessellation factors)
struct PatchConstants {
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

// Project world position to screen space and return screen position
float2 WorldToScreen(float3 worldPos) {
    float4 clipPos = mul(g_ViewProj, float4(worldPos, 1.0));
    float2 ndc = clipPos.xy / clipPos.w;
    float2 screenPos = (ndc * 0.5 + 0.5) * g_ScreenParams.xy;
    screenPos.y = g_ScreenParams.y - screenPos.y;  // Flip Y
    return screenPos;
}

// Compute screen-space edge length in pixels
float ComputeScreenEdgeLength(float3 p0, float3 p1) {
    float2 s0 = WorldToScreen(p0);
    float2 s1 = WorldToScreen(p1);
    return length(s1 - s0);
}

// Compute distance-based tessellation factor
float ComputeDistanceFactor(float3 edgeMidpoint) {
    float dist = distance(edgeMidpoint, g_CameraPosition.xyz);
    float startDist = g_LODParams.x;
    float endDist = g_LODParams.y;

    // Linear falloff from max to min tessellation
    float t = saturate((dist - startDist) / (endDist - startDist));
    return lerp(g_TessParams.x, g_TessParams.y, t);
}

// Compute screen-space based tessellation factor
float ComputeScreenSpaceFactor(float3 p0, float3 p1) {
    float screenLength = ComputeScreenEdgeLength(p0, p1);
    float targetLength = g_TessParams.z;

    // How many segments needed to achieve target edge length
    float factor = screenLength / targetLength;
    return clamp(factor, g_TessParams.y, g_TessParams.x);
}

// Compute final edge tessellation factor (blend of distance and screen-space)
float ComputeEdgeTessFactor(float3 p0, float3 p1) {
    float3 midpoint = (p0 + p1) * 0.5;

    // Distance-based factor
    float distFactor = ComputeDistanceFactor(midpoint);

    // Screen-space based factor
    float screenFactor = ComputeScreenSpaceFactor(p0, p1);

    // Blend: use distance as primary, screen-space as refinement
    float blendWeight = g_TessParams.w;  // 0 = all distance, 1 = all screen-space
    float factor = lerp(distFactor, screenFactor, blendWeight);

    return clamp(factor, g_TessParams.y, g_TessParams.x);
}

// Frustum culling check for a triangle
bool IsTriangleVisible(float3 p0, float3 p1, float3 p2) {
    // Simple sphere-based culling using triangle center
    float3 center = (p0 + p1 + p2) / 3.0;
    float radius = max(max(distance(center, p0), distance(center, p1)), distance(center, p2));

    // Transform center to clip space
    float4 clipCenter = mul(g_ViewProj, float4(center, 1.0));

    // Check if sphere is behind camera
    if (clipCenter.w + radius < 0.0) return false;

    // Check frustum planes (simplified)
    float3 absClip = abs(clipCenter.xyz);
    float threshold = clipCenter.w + radius;

    return absClip.x <= threshold && absClip.y <= threshold && clipCenter.z <= threshold;
}

// Patch constant function - computes tessellation factors for the patch
PatchConstants PatchConstantFunc(
    InputPatch<HullInput, 3> patch,
    uint patchID : SV_PrimitiveID)
{
    PatchConstants output;

    // Frustum culling
    if (!IsTriangleVisible(patch[0].worldPos, patch[1].worldPos, patch[2].worldPos)) {
        // Cull patch by setting tessellation factors to 0
        output.edges[0] = 0.0;
        output.edges[1] = 0.0;
        output.edges[2] = 0.0;
        output.inside = 0.0;
        return output;
    }

    // Compute per-edge tessellation factors
    // Edge 0: vertices 1-2, Edge 1: vertices 2-0, Edge 2: vertices 0-1
    output.edges[0] = ComputeEdgeTessFactor(patch[1].worldPos, patch[2].worldPos);
    output.edges[1] = ComputeEdgeTessFactor(patch[2].worldPos, patch[0].worldPos);
    output.edges[2] = ComputeEdgeTessFactor(patch[0].worldPos, patch[1].worldPos);

    // Inside factor is average of edge factors
    output.inside = (output.edges[0] + output.edges[1] + output.edges[2]) / 3.0;

    return output;
}

// Main hull shader - passes through control points
[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantFunc")]
[maxtessfactor(64.0)]
HullOutput HSMain(
    InputPatch<HullInput, 3> patch,
    uint controlPointID : SV_OutputControlPointID,
    uint patchID : SV_PrimitiveID)
{
    HullOutput output;

    // Pass through control point data
    output.worldPos = patch[controlPointID].worldPos;
    output.normal = patch[controlPointID].normal;
    output.tangent = patch[controlPointID].tangent;
    output.texCoord = patch[controlPointID].texCoord;
    output.color = patch[controlPointID].color;
    output.coarsePos = patch[controlPointID].coarsePos;

    return output;
}
