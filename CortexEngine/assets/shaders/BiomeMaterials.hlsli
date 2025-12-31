// BiomeMaterials.hlsli
// Biome material structures and helper functions for terrain rendering.
// Used by MaterialResolve.hlsl and other terrain shaders.

#ifndef BIOME_MATERIALS_HLSLI
#define BIOME_MATERIALS_HLSLI

// Maximum number of biome types supported
#define MAX_BIOMES 16
#define MAX_HEIGHT_LAYERS 4

// Per-biome material data (matches BiomeMaterialGPU in C++)
struct BiomeMaterial {
    float4 baseColor;           // Primary terrain color
    float4 slopeColor;          // Color on steep slopes
    float roughness;            // PBR roughness
    float metallic;             // PBR metallic
    float2 _pad0;
    float4 heightLayerMin;      // Min heights for 4 layers
    float4 heightLayerMax;      // Max heights for 4 layers
    float4 heightLayerColor[4]; // Colors for 4 height layers
};

// Constant buffer containing all biome materials
cbuffer BiomeMaterialsCB : register(b4) {
    BiomeMaterial g_Biomes[MAX_BIOMES];
    uint g_BiomeCount;
    float3 _padBiome;
};

// Decode biome vertex color data
// R = biome0 index (0-15)
// G = biome1 index (0-15)
// B = blend weight (0-1)
// A = flags (reserved)
struct BiomeVertexData {
    uint biome0;
    uint biome1;
    float blendWeight;
    uint flags;
};

BiomeVertexData DecodeBlendData(float4 vertexColor) {
    BiomeVertexData data;
    data.biome0 = uint(vertexColor.r * 255.0 + 0.5) % MAX_BIOMES;
    data.biome1 = uint(vertexColor.g * 255.0 + 0.5) % MAX_BIOMES;
    data.blendWeight = vertexColor.b;
    data.flags = uint(vertexColor.a * 255.0 + 0.5);
    return data;
}

// Smooth interpolation helper
float Smoothstep(float edge0, float edge1, float x) {
    float t = saturate((x - edge0) / (edge1 - edge0 + 0.0001));
    return t * t * (3.0 - 2.0 * t);
}

// Compute height-based color for a single biome
float4 ComputeHeightColor(BiomeMaterial mat, float height) {
    float4 result = mat.baseColor;

    // Blend through height layers
    [unroll]
    for (int i = 0; i < MAX_HEIGHT_LAYERS; i++) {
        float minH = mat.heightLayerMin[i];
        float maxH = mat.heightLayerMax[i];

        // Only apply layer if it has valid range
        if (maxH > minH) {
            float t = Smoothstep(minH, maxH, height);
            result = lerp(result, mat.heightLayerColor[i], t);
        }
    }

    return result;
}

// Compute slope factor from world normal
// Returns 0 for flat surfaces, 1 for vertical surfaces
float ComputeSlopeFactor(float3 worldNormal) {
    return 1.0 - abs(worldNormal.y);
}

// Sample biome material at a world position
// Takes into account height, slope, and biome blending
struct BiomeSurfaceData {
    float4 albedo;
    float roughness;
    float metallic;
};

BiomeSurfaceData SampleBiomeMaterial(
    float3 worldPos,
    float3 worldNormal,
    float4 biomeVertexColor
) {
    BiomeSurfaceData result;

    // Decode biome blend data from vertex color
    BiomeVertexData biomeData = DecodeBlendData(biomeVertexColor);

    // Get materials for both biomes
    BiomeMaterial mat0 = g_Biomes[biomeData.biome0];
    BiomeMaterial mat1 = g_Biomes[biomeData.biome1];

    float height = worldPos.y;
    float slope = ComputeSlopeFactor(worldNormal);

    // Compute height-based colors for both biomes
    float4 color0 = ComputeHeightColor(mat0, height);
    float4 color1 = ComputeHeightColor(mat1, height);

    // Apply slope-based blending (steep surfaces get slope color)
    float slopeBlend0 = Smoothstep(0.5, 0.8, slope);
    float slopeBlend1 = Smoothstep(0.5, 0.8, slope);
    color0 = lerp(color0, mat0.slopeColor, slopeBlend0);
    color1 = lerp(color1, mat1.slopeColor, slopeBlend1);

    // Blend between biomes based on vertex color weight
    result.albedo = lerp(color0, color1, biomeData.blendWeight);

    // Blend roughness and metallic
    result.roughness = lerp(mat0.roughness, mat1.roughness, biomeData.blendWeight);
    result.metallic = lerp(mat0.metallic, mat1.metallic, biomeData.blendWeight);

    return result;
}

// Simplified version that just returns albedo color
// Useful for debugging or simple terrain rendering
float4 SampleBiomeAlbedo(
    float3 worldPos,
    float3 worldNormal,
    float4 biomeVertexColor
) {
    BiomeSurfaceData data = SampleBiomeMaterial(worldPos, worldNormal, biomeVertexColor);
    return data.albedo;
}

// Check if vertex color indicates terrain/biome data
// (Non-zero biome indices or blend weight suggests biome-enabled mesh)
bool IsBiomeTerrain(float4 vertexColor) {
    // If R or G channel has a non-zero value, it's likely biome data
    // Regular meshes typically have white or single-color vertex colors
    float sum = vertexColor.r + vertexColor.g;
    return sum > 0.001;
}

// Fallback color for when biome system is not in use
float4 GetDefaultTerrainColor(float3 worldNormal, float height) {
    // Simple grass-to-rock-to-snow gradient
    float4 grassColor = float4(0.3, 0.5, 0.2, 1.0);
    float4 rockColor = float4(0.5, 0.5, 0.5, 1.0);
    float4 snowColor = float4(0.95, 0.95, 1.0, 1.0);

    float slope = ComputeSlopeFactor(worldNormal);

    // Height-based blending
    float4 color = grassColor;
    color = lerp(color, rockColor, Smoothstep(50.0, 100.0, height));
    color = lerp(color, snowColor, Smoothstep(100.0, 150.0, height));

    // Slope-based rock on steep surfaces
    color = lerp(color, rockColor, Smoothstep(0.5, 0.8, slope));

    return color;
}

#endif // BIOME_MATERIALS_HLSLI
