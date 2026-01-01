// TerrainDomain.hlsl
// Domain shader for terrain tessellation.
// Interpolates vertex attributes at tessellated positions and applies height displacement.

#include "BiomeMaterials.hlsli"

// Shared constants with hull shader
cbuffer TessellationConstants : register(b5) {
    float4x4 g_ViewProj;
    float4 g_CameraPosition;
    float4 g_TessParams;
    float4 g_ScreenParams;
    float4 g_LODParams;           // x = tessStartDist, y = tessEndDist, z = heightScale, w = geomorphFactor
};

cbuffer ObjectConstants : register(b0) {
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
    float g_DepthBiasNdc;
    float3 _pad0;
};

// Heightmap for displacement
Texture2D<float> g_HeightMap : register(t10);
SamplerState g_HeightSampler : register(s1);

// Input from hull shader (control point)
struct HullOutput {
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
    float3 coarsePos : COARSE_POS;
};

// Patch constant data
struct PatchConstants {
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

// Output to pixel shader
struct DomainOutput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;
};

// Sample heightmap with bilinear filtering
float SampleHeight(float2 uv) {
    return g_HeightMap.SampleLevel(g_HeightSampler, uv, 0).r;
}

// Compute normal from heightmap using central differences
float3 ComputeHeightmapNormal(float2 uv, float texelSize, float heightScale) {
    float hL = SampleHeight(uv + float2(-texelSize, 0.0));
    float hR = SampleHeight(uv + float2(texelSize, 0.0));
    float hD = SampleHeight(uv + float2(0.0, -texelSize));
    float hU = SampleHeight(uv + float2(0.0, texelSize));

    float3 normal;
    normal.x = (hL - hR) * heightScale;
    normal.y = 2.0 * texelSize;  // Spacing between samples
    normal.z = (hD - hU) * heightScale;

    return normalize(normal);
}

// Compute geomorph blend factor based on distance
float ComputeGeomorphFactor(float3 worldPos) {
    float dist = distance(worldPos, g_CameraPosition.xyz);
    float startDist = g_LODParams.x;
    float endDist = g_LODParams.y;

    // Smooth transition: 0 at close range (use fine detail), 1 at far (blend to coarse)
    return saturate((dist - startDist) / (endDist - startDist));
}

[domain("tri")]
DomainOutput DSMain(
    PatchConstants patchConstants,
    float3 baryCoords : SV_DomainLocation,
    const OutputPatch<HullOutput, 3> patch)
{
    DomainOutput output;

    // Interpolate vertex attributes using barycentric coordinates
    float3 worldPos = patch[0].worldPos * baryCoords.x +
                      patch[1].worldPos * baryCoords.y +
                      patch[2].worldPos * baryCoords.z;

    float3 normal = normalize(
        patch[0].normal * baryCoords.x +
        patch[1].normal * baryCoords.y +
        patch[2].normal * baryCoords.z);

    float4 tangent;
    tangent.xyz = normalize(
        patch[0].tangent.xyz * baryCoords.x +
        patch[1].tangent.xyz * baryCoords.y +
        patch[2].tangent.xyz * baryCoords.z);
    tangent.w = patch[0].tangent.w;  // Handedness

    float2 texCoord = patch[0].texCoord * baryCoords.x +
                      patch[1].texCoord * baryCoords.y +
                      patch[2].texCoord * baryCoords.z;

    float4 color = patch[0].color * baryCoords.x +
                   patch[1].color * baryCoords.y +
                   patch[2].color * baryCoords.z;

    // Interpolate coarse position for geomorphing
    float3 coarsePos = patch[0].coarsePos * baryCoords.x +
                       patch[1].coarsePos * baryCoords.y +
                       patch[2].coarsePos * baryCoords.z;

    // Sample heightmap for additional displacement
    float heightScale = g_LODParams.z;
    float heightSample = SampleHeight(texCoord);
    float displacement = heightSample * heightScale;

    // Apply displacement along normal
    float3 displacedPos = worldPos + normal * displacement;

    // Geomorphing: blend between fine (tessellated) and coarse positions
    // This prevents popping when LOD changes
    float geomorphFactor = ComputeGeomorphFactor(displacedPos);
    float geomorphStrength = g_LODParams.w;  // 0 = no geomorph, 1 = full geomorph

    // Only apply geomorphing if enabled
    if (geomorphStrength > 0.001) {
        // Coarse position also needs displacement
        float coarseHeight = SampleHeight(texCoord);  // Could use lower mip for coarse
        float3 displacedCoarse = coarsePos + normal * (coarseHeight * heightScale);

        // Blend between fine and coarse based on distance
        displacedPos = lerp(displacedPos, displacedCoarse, geomorphFactor * geomorphStrength);
    }

    // Compute displaced normal from heightmap (for lighting)
    float texelSize = 1.0 / 1024.0;  // Assume 1024x1024 heightmap, should be passed as parameter
    float3 heightNormal = ComputeHeightmapNormal(texCoord, texelSize, heightScale);

    // Blend vertex normal with heightmap normal
    // Use heightmap normal more at close range where tessellation provides detail
    float normalBlend = 1.0 - geomorphFactor;  // More heightmap normal up close
    normal = normalize(lerp(normal, heightNormal, normalBlend * 0.5));

    // Transform to clip space
    output.position = mul(g_ViewProj, float4(displacedPos, 1.0));
    output.worldPos = displacedPos;
    output.normal = normal;
    output.tangent = tangent;
    output.texCoord = texCoord;
    output.color = color;

    return output;
}
