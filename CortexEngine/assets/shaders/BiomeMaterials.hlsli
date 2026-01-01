// BiomeMaterials.hlsli
// Biome material structures and helper functions for terrain rendering.
// Used by MaterialResolve.hlsl and other terrain shaders.
// Supports 4-way biome blending with noise-modulated transitions.

#ifndef BIOME_MATERIALS_HLSLI
#define BIOME_MATERIALS_HLSLI

// Maximum number of biome types supported
#define MAX_BIOMES 16
#define MAX_HEIGHT_LAYERS 4
#define MAX_BLEND_BIOMES 4

// Blending quality levels
#define BLEND_QUALITY_LOW 0      // 2-way simple blend
#define BLEND_QUALITY_MEDIUM 1   // 4-way blend
#define BLEND_QUALITY_HIGH 2     // 4-way + noise
#define BLEND_QUALITY_ULTRA 3    // 4-way + noise + height override

// Default blend quality (can be overridden by quality preset)
#ifndef BIOME_BLEND_QUALITY
#define BIOME_BLEND_QUALITY BLEND_QUALITY_HIGH
#endif

// Snowline parameters (world-space heights)
#define SNOWLINE_START 120.0
#define SNOWLINE_FULL 160.0
#define TUNDRA_BIOME_INDEX 8  // Index for Tundra in biome array

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

// ============================================================================
// NOISE FUNCTIONS FOR BOUNDARY BLENDING
// ============================================================================

// Hash function for pseudo-random values
float Hash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

// 2D value noise
float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    // Smooth interpolation
    float2 u = f * f * (3.0 - 2.0 * f);

    // Four corners
    float a = Hash(i);
    float b = Hash(i + float2(1.0, 0.0));
    float c = Hash(i + float2(0.0, 1.0));
    float d = Hash(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Fractal Brownian Motion (FBM) - multi-octave noise
float FBM(float2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    float totalAmplitude = 0.0;

    [unroll]
    for (int i = 0; i < octaves && i < 6; i++) {
        value += amplitude * ValueNoise(p * frequency);
        totalAmplitude += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value / totalAmplitude;
}

// Noise-modulated blend weight for natural transitions
float NoiseModulatedBlend(float baseWeight, float2 worldXZ, float noiseScale, float noiseStrength) {
    float noise = FBM(worldXZ * noiseScale, 4);
    noise = noise * 2.0 - 1.0;  // Remap to [-1, 1]

    // Apply noise to blend weight
    float modulated = baseWeight + noise * noiseStrength;
    return saturate(modulated);
}

// ============================================================================
// BIOME VERTEX DATA STRUCTURES
// ============================================================================

// Legacy 2-way blend format
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

// Extended 4-way blend format
// Supports up to 4 biomes blending at a vertex
struct BiomeVertexData4 {
    uint biomeIndices[4];   // Up to 4 biome indices
    float weights[4];       // Corresponding blend weights (sum to 1)
    uint activeCount;       // How many biomes are active (1-4)
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

// Decode 4-way blend data from two float4 vertex colors
// color0: RGBA = weights for biomes 0-3 (normalized)
// color1: RGBA = biome indices (packed as 0-15 values)
BiomeVertexData4 DecodeBlendData4(float4 weights, float4 indices) {
    BiomeVertexData4 data;

    // Extract biome indices from color1
    data.biomeIndices[0] = uint(indices.r * 15.0 + 0.5) % MAX_BIOMES;
    data.biomeIndices[1] = uint(indices.g * 15.0 + 0.5) % MAX_BIOMES;
    data.biomeIndices[2] = uint(indices.b * 15.0 + 0.5) % MAX_BIOMES;
    data.biomeIndices[3] = uint(indices.a * 15.0 + 0.5) % MAX_BIOMES;

    // Extract weights from color0
    data.weights[0] = weights.r;
    data.weights[1] = weights.g;
    data.weights[2] = weights.b;
    data.weights[3] = weights.a;

    // Normalize weights
    float totalWeight = data.weights[0] + data.weights[1] + data.weights[2] + data.weights[3];
    if (totalWeight > 0.001) {
        data.weights[0] /= totalWeight;
        data.weights[1] /= totalWeight;
        data.weights[2] /= totalWeight;
        data.weights[3] /= totalWeight;
    }

    // Count active biomes (weight > threshold)
    data.activeCount = 0;
    [unroll]
    for (int i = 0; i < 4; i++) {
        if (data.weights[i] > 0.01) data.activeCount++;
    }

    data.flags = 0;
    return data;
}

// Convert 2-way data to 4-way format for unified processing
BiomeVertexData4 Convert2To4Way(BiomeVertexData data2) {
    BiomeVertexData4 data4;

    data4.biomeIndices[0] = data2.biome0;
    data4.biomeIndices[1] = data2.biome1;
    data4.biomeIndices[2] = 0;
    data4.biomeIndices[3] = 0;

    data4.weights[0] = 1.0 - data2.blendWeight;
    data4.weights[1] = data2.blendWeight;
    data4.weights[2] = 0.0;
    data4.weights[3] = 0.0;

    data4.activeCount = (data2.blendWeight > 0.01) ? 2 : 1;
    data4.flags = data2.flags;

    return data4;
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

// ============================================================================
// HEIGHT-BASED BIOME OVERRIDE (SNOWLINE)
// ============================================================================

// Compute snowline blend factor
// Returns 0 below snowline, 1 at full snow height
float ComputeSnowlineBlend(float height) {
    return Smoothstep(SNOWLINE_START, SNOWLINE_FULL, height);
}

// Apply height-based biome override (e.g., snow above snowline)
void ApplyHeightOverride(inout BiomeVertexData4 biomeData, float height, float2 worldXZ) {
    float snowBlend = ComputeSnowlineBlend(height);

    if (snowBlend > 0.01) {
        #if BIOME_BLEND_QUALITY >= BLEND_QUALITY_HIGH
        // Add noise variation to snowline for natural look
        float noiseOffset = FBM(worldXZ * 0.05, 3) * 20.0;  // +/- 20 units variation
        snowBlend = ComputeSnowlineBlend(height - noiseOffset);
        #endif

        // Inject tundra/snow biome with appropriate weight
        // Find the slot with lowest weight to replace
        int minSlot = 0;
        float minWeight = biomeData.weights[0];
        [unroll]
        for (int i = 1; i < 4; i++) {
            if (biomeData.weights[i] < minWeight) {
                minWeight = biomeData.weights[i];
                minSlot = i;
            }
        }

        // Blend in the snow biome
        biomeData.biomeIndices[minSlot] = TUNDRA_BIOME_INDEX;
        biomeData.weights[minSlot] = snowBlend;

        // Renormalize weights
        float total = 0.0;
        [unroll]
        for (int j = 0; j < 4; j++) {
            total += biomeData.weights[j];
        }
        if (total > 0.001) {
            [unroll]
            for (int k = 0; k < 4; k++) {
                biomeData.weights[k] /= total;
            }
        }
    }
}

// ============================================================================
// BIOME SURFACE SAMPLING
// ============================================================================

// Sample biome material at a world position
// Takes into account height, slope, and biome blending
struct BiomeSurfaceData {
    float4 albedo;
    float roughness;
    float metallic;
};

// Compute material properties for a single biome at given position
BiomeSurfaceData ComputeSingleBiomeSurface(uint biomeIndex, float3 worldPos, float3 worldNormal) {
    BiomeSurfaceData result;
    BiomeMaterial mat = g_Biomes[biomeIndex];

    float height = worldPos.y;
    float slope = ComputeSlopeFactor(worldNormal);

    // Compute height-based color
    float4 color = ComputeHeightColor(mat, height);

    // Apply slope-based blending (steep surfaces get slope color)
    float slopeBlend = Smoothstep(0.5, 0.8, slope);
    color = lerp(color, mat.slopeColor, slopeBlend);

    result.albedo = color;
    result.roughness = mat.roughness;
    result.metallic = mat.metallic;

    return result;
}

// 4-way biome material sampling with optional noise and height override
BiomeSurfaceData SampleBiomeMaterial4(
    float3 worldPos,
    float3 worldNormal,
    BiomeVertexData4 biomeData
) {
    BiomeSurfaceData result;
    result.albedo = float4(0, 0, 0, 0);
    result.roughness = 0.0;
    result.metallic = 0.0;

    float2 worldXZ = worldPos.xz;
    float totalWeight = 0.0;  // Track accumulated weight separately

    #if BIOME_BLEND_QUALITY >= BLEND_QUALITY_ULTRA
    // Apply height-based override (snowline)
    ApplyHeightOverride(biomeData, worldPos.y, worldXZ);
    #endif

    // Process each active biome
    [unroll]
    for (int i = 0; i < 4; i++) {
        float weight = biomeData.weights[i];
        if (weight < 0.001) continue;

        #if BIOME_BLEND_QUALITY >= BLEND_QUALITY_HIGH
        // Apply noise modulation to blend weights for natural transitions
        // Use different noise offsets for each biome layer
        float2 noiseUV = worldXZ + float2(i * 17.3, i * 23.7);
        weight = NoiseModulatedBlend(weight, noiseUV, 0.1, 0.15);
        #endif

        uint biomeIdx = biomeData.biomeIndices[i];
        BiomeSurfaceData biomeSurface = ComputeSingleBiomeSurface(biomeIdx, worldPos, worldNormal);

        // Accumulate weighted contribution
        result.albedo.rgb += biomeSurface.albedo.rgb * weight;
        result.roughness += biomeSurface.roughness * weight;
        result.metallic += biomeSurface.metallic * weight;
        totalWeight += weight;  // Accumulate weight for normalization
    }

    // Normalize by accumulated weight after noise modulation
    if (totalWeight > 0.001) {
        if (abs(totalWeight - 1.0) > 0.01) {
            result.albedo.rgb /= totalWeight;
            result.roughness /= totalWeight;
            result.metallic /= totalWeight;
        }
    } else {
        // Fallback to default material if no weights
        result.albedo.rgb = float3(0.5, 0.5, 0.5);
        result.roughness = 0.8;
        result.metallic = 0.0;
    }
    result.albedo.a = 1.0;

    return result;
}

// Legacy 2-way sampling (backward compatible)
BiomeSurfaceData SampleBiomeMaterial(
    float3 worldPos,
    float3 worldNormal,
    float4 biomeVertexColor
) {
    #if BIOME_BLEND_QUALITY == BLEND_QUALITY_LOW
    // Simple 2-way blend for low quality
    BiomeSurfaceData result;

    BiomeVertexData biomeData = DecodeBlendData(biomeVertexColor);
    BiomeMaterial mat0 = g_Biomes[biomeData.biome0];
    BiomeMaterial mat1 = g_Biomes[biomeData.biome1];

    float height = worldPos.y;
    float slope = ComputeSlopeFactor(worldNormal);

    float4 color0 = ComputeHeightColor(mat0, height);
    float4 color1 = ComputeHeightColor(mat1, height);

    float slopeBlend = Smoothstep(0.5, 0.8, slope);
    color0 = lerp(color0, mat0.slopeColor, slopeBlend);
    color1 = lerp(color1, mat1.slopeColor, slopeBlend);

    result.albedo = lerp(color0, color1, biomeData.blendWeight);
    result.roughness = lerp(mat0.roughness, mat1.roughness, biomeData.blendWeight);
    result.metallic = lerp(mat0.metallic, mat1.metallic, biomeData.blendWeight);

    return result;
    #else
    // Convert to 4-way format and use enhanced sampling
    BiomeVertexData biomeData2 = DecodeBlendData(biomeVertexColor);
    BiomeVertexData4 biomeData4 = Convert2To4Way(biomeData2);
    return SampleBiomeMaterial4(worldPos, worldNormal, biomeData4);
    #endif
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
// The alpha channel contains flags: bit 0 (0x01) = biome terrain flag
// CPU encodes this as flags = 1.0f / 255.0f for biome terrain vertices
bool IsBiomeTerrain(float4 vertexColor) {
    // Decode flags from alpha channel
    uint flags = uint(vertexColor.a * 255.0 + 0.5);
    // Check bit 0 for biome terrain flag
    return (flags & 1) != 0;
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
