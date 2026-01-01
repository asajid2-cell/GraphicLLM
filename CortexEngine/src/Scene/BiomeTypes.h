#pragma once

// BiomeTypes.h
// Biome configuration types for procedural terrain generation.
// Defines biome enums, configurations, and sample results.

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Cortex::Scene {

// Biome type enumeration
enum class BiomeType : uint8_t {
    Plains = 0,
    Mountains = 1,
    Desert = 2,
    Forest = 3,
    Tundra = 4,
    Swamp = 5,
    Beach = 6,
    Volcanic = 7,
    Ocean = 8,
    COUNT
};

// Convert BiomeType to string for debugging/serialization
inline const char* BiomeTypeToString(BiomeType type) {
    switch (type) {
        case BiomeType::Plains:    return "Plains";
        case BiomeType::Mountains: return "Mountains";
        case BiomeType::Desert:    return "Desert";
        case BiomeType::Forest:    return "Forest";
        case BiomeType::Tundra:    return "Tundra";
        case BiomeType::Swamp:     return "Swamp";
        case BiomeType::Beach:     return "Beach";
        case BiomeType::Volcanic:  return "Volcanic";
        case BiomeType::Ocean:     return "Ocean";
        default:                   return "Unknown";
    }
}

// Convert string to BiomeType for deserialization
inline BiomeType StringToBiomeType(const std::string& str) {
    if (str == "Plains")    return BiomeType::Plains;
    if (str == "Mountains") return BiomeType::Mountains;
    if (str == "Desert")    return BiomeType::Desert;
    if (str == "Forest")    return BiomeType::Forest;
    if (str == "Tundra")    return BiomeType::Tundra;
    if (str == "Swamp")     return BiomeType::Swamp;
    if (str == "Beach")     return BiomeType::Beach;
    if (str == "Volcanic")  return BiomeType::Volcanic;
    if (str == "Ocean")     return BiomeType::Ocean;
    return BiomeType::Plains; // Default fallback
}

// Height-based color layer for terrain materials
struct BiomeHeightLayer {
    float minHeight = 0.0f;
    float maxHeight = 100.0f;
    glm::vec4 color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
};

// Per-biome configuration loaded from JSON
struct BiomeConfig {
    BiomeType type = BiomeType::Plains;
    std::string name = "Plains";

    // Terrain shape modifiers
    float heightScale = 1.0f;         // Multiplier for base terrain noise
    float heightOffset = 0.0f;        // Added to final height
    float slopeInfluence = 0.5f;      // How much slope affects material selection

    // Material properties
    glm::vec4 baseColor = glm::vec4(0.3f, 0.5f, 0.2f, 1.0f);   // Primary terrain color
    glm::vec4 slopeColor = glm::vec4(0.4f, 0.35f, 0.25f, 1.0f); // Color on steep slopes
    float roughness = 0.8f;
    float metallic = 0.0f;
    float normalScale = 1.0f;

    // Height-based color layers (e.g., grass -> rock -> snow)
    // Up to 4 layers for GPU efficiency
    std::vector<BiomeHeightLayer> heightLayers;

    // Vegetation/prop density (0 = none, 1 = max)
    float vegetationDensity = 0.5f;
    float treeDensity = 0.0f;
    float rockDensity = 0.0f;
    float grassDensity = 0.0f;

    // Props to spawn in this biome (references prop config IDs)
    std::vector<std::string> propTypes;
};

// Maximum biomes that can blend at a single vertex
constexpr int MAX_BLEND_BIOMES = 4;

// Biome map sample result at a world position
struct BiomeSample {
    BiomeType primary = BiomeType::Plains;     // Dominant biome at this location
    BiomeType secondary = BiomeType::Plains;   // Secondary biome for boundary blending
    float blendWeight = 0.0f;                  // 0 = all primary, 1 = all secondary
    float temperature = 0.5f;                  // 0-1 climate value for variation
    float moisture = 0.5f;                     // 0-1 humidity value for vegetation

    // Get interpolated height scale based on blend
    float GetBlendedHeightScale(const BiomeConfig& primaryCfg, const BiomeConfig& secondaryCfg) const {
        return glm::mix(primaryCfg.heightScale, secondaryCfg.heightScale, blendWeight);
    }

    // Get interpolated height offset based on blend
    float GetBlendedHeightOffset(const BiomeConfig& primaryCfg, const BiomeConfig& secondaryCfg) const {
        return glm::mix(primaryCfg.heightOffset, secondaryCfg.heightOffset, blendWeight);
    }
};

// Extended biome sample with 4-way blending support
struct BiomeSample4 {
    BiomeType biomes[MAX_BLEND_BIOMES] = { BiomeType::Plains, BiomeType::Plains, BiomeType::Plains, BiomeType::Plains };
    float weights[MAX_BLEND_BIOMES] = { 1.0f, 0.0f, 0.0f, 0.0f };  // Sum to 1.0
    int activeCount = 1;               // Number of active biomes (1-4)
    float temperature = 0.5f;
    float moisture = 0.5f;

    // Get primary biome (highest weight)
    BiomeType GetPrimary() const { return biomes[0]; }

    // Normalize weights to sum to 1.0
    void NormalizeWeights() {
        float sum = weights[0] + weights[1] + weights[2] + weights[3];
        if (sum > 0.001f) {
            for (int i = 0; i < MAX_BLEND_BIOMES; ++i) {
                weights[i] /= sum;
            }
        }
    }

    // Convert to legacy 2-way sample
    BiomeSample ToLegacy() const {
        BiomeSample legacy;
        legacy.primary = biomes[0];
        legacy.secondary = (activeCount > 1) ? biomes[1] : biomes[0];
        legacy.blendWeight = (activeCount > 1) ? weights[1] : 0.0f;
        legacy.temperature = temperature;
        legacy.moisture = moisture;
        return legacy;
    }

    // Create from legacy 2-way sample
    static BiomeSample4 FromLegacy(const BiomeSample& legacy) {
        BiomeSample4 sample4;
        sample4.biomes[0] = legacy.primary;
        sample4.biomes[1] = legacy.secondary;
        sample4.weights[0] = 1.0f - legacy.blendWeight;
        sample4.weights[1] = legacy.blendWeight;
        sample4.weights[2] = 0.0f;
        sample4.weights[3] = 0.0f;
        sample4.activeCount = (legacy.blendWeight > 0.01f) ? 2 : 1;
        sample4.temperature = legacy.temperature;
        sample4.moisture = legacy.moisture;
        return sample4;
    }
};

// Biome map generation parameters
struct BiomeMapParams {
    uint32_t seed = 1337;

    // Voronoi cell size (larger = bigger biome regions)
    float cellSize = 512.0f;

    // Blend distance at biome boundaries (soft transitions)
    float blendRadius = 64.0f;

    // Temperature/moisture noise frequencies (smaller = larger climate zones)
    float temperatureFreq = 0.001f;
    float moistureFreq = 0.0008f;

    // Temperature/moisture noise octaves
    uint32_t climateOctaves = 4;
    float climateLacunarity = 2.0f;
    float climateGain = 0.5f;
};

// Per-vertex biome data packed for GPU upload (legacy 2-way format)
// Stored in vertex color channels for splatmap approach
struct BiomeVertexData {
    uint8_t biome0 = 0;        // Primary biome index
    uint8_t biome1 = 0;        // Secondary biome index (for blending)
    uint8_t blendWeight = 0;   // 0-255 blend factor (0 = all biome0, 255 = all biome1)
    uint8_t flags = 0;         // Reserved for future use

    // Pack into a 32-bit integer for GPU
    uint32_t Pack() const {
        return static_cast<uint32_t>(biome0) |
               (static_cast<uint32_t>(biome1) << 8) |
               (static_cast<uint32_t>(blendWeight) << 16) |
               (static_cast<uint32_t>(flags) << 24);
    }

    // Unpack from 32-bit integer
    static BiomeVertexData Unpack(uint32_t packed) {
        BiomeVertexData data;
        data.biome0 = static_cast<uint8_t>(packed & 0xFF);
        data.biome1 = static_cast<uint8_t>((packed >> 8) & 0xFF);
        data.blendWeight = static_cast<uint8_t>((packed >> 16) & 0xFF);
        data.flags = static_cast<uint8_t>((packed >> 24) & 0xFF);
        return data;
    }

    // Convert to glm::vec4 for vertex color (normalized floats)
    glm::vec4 ToVec4() const {
        return glm::vec4(
            static_cast<float>(biome0) / 255.0f,
            static_cast<float>(biome1) / 255.0f,
            static_cast<float>(blendWeight) / 255.0f,
            static_cast<float>(flags) / 255.0f
        );
    }

    // Create from biome sample
    static BiomeVertexData FromSample(const BiomeSample& sample) {
        BiomeVertexData data;
        data.biome0 = static_cast<uint8_t>(sample.primary);
        data.biome1 = static_cast<uint8_t>(sample.secondary);
        data.blendWeight = static_cast<uint8_t>(sample.blendWeight * 255.0f);
        data.flags = 0;
        return data;
    }
};

// Extended 4-way biome vertex data for advanced blending
// Uses two vertex color attributes: weights (COLOR0) and indices (COLOR1)
struct BiomeVertexData4 {
    uint8_t biomeIndices[4] = { 0, 0, 0, 0 };  // Biome indices (0-15)
    uint8_t weights[4] = { 255, 0, 0, 0 };     // Blend weights (0-255, sum to 255)

    // Convert weights to float4 for COLOR0 (normalized)
    glm::vec4 GetWeightsVec4() const {
        return glm::vec4(
            static_cast<float>(weights[0]) / 255.0f,
            static_cast<float>(weights[1]) / 255.0f,
            static_cast<float>(weights[2]) / 255.0f,
            static_cast<float>(weights[3]) / 255.0f
        );
    }

    // Convert indices to float4 for COLOR1 (packed as 0-1, decode as 0-15)
    glm::vec4 GetIndicesVec4() const {
        return glm::vec4(
            static_cast<float>(biomeIndices[0]) / 15.0f,
            static_cast<float>(biomeIndices[1]) / 15.0f,
            static_cast<float>(biomeIndices[2]) / 15.0f,
            static_cast<float>(biomeIndices[3]) / 15.0f
        );
    }

    // Get active biome count (non-zero weights)
    int GetActiveCount() const {
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            if (weights[i] > 2) ++count;  // Threshold to ignore near-zero
        }
        return count > 0 ? count : 1;
    }

    // Create from BiomeSample4
    static BiomeVertexData4 FromSample4(const BiomeSample4& sample) {
        BiomeVertexData4 data;
        for (int i = 0; i < MAX_BLEND_BIOMES; ++i) {
            data.biomeIndices[i] = static_cast<uint8_t>(sample.biomes[i]);
            data.weights[i] = static_cast<uint8_t>(sample.weights[i] * 255.0f);
        }
        return data;
    }

    // Convert to legacy 2-way format (uses top 2 weights)
    BiomeVertexData ToLegacy() const {
        BiomeVertexData legacy;
        legacy.biome0 = biomeIndices[0];
        legacy.biome1 = biomeIndices[1];
        // Blend weight is the ratio of weight1 to (weight0 + weight1)
        int sum01 = weights[0] + weights[1];
        legacy.blendWeight = (sum01 > 0) ? static_cast<uint8_t>((weights[1] * 255) / sum01) : 0;
        legacy.flags = 0;
        return legacy;
    }
};

// GPU-side biome material data (matches HLSL cbuffer layout)
// This is uploaded as a constant buffer for the terrain shader
// CRITICAL: Layout must exactly match BiomeMaterial in BiomeMaterials.hlsli!
struct alignas(16) BiomeMaterialGPU {
    glm::vec4 baseColor;           // 16 bytes @ offset 0
    glm::vec4 slopeColor;          // 16 bytes @ offset 16
    float roughness;               // 4 bytes @ offset 32
    float metallic;                // 4 bytes @ offset 36
    float _pad0[2];                // 8 bytes @ offset 40 (matches HLSL float2 _pad0)
    float heightLayerMin[4];       // 16 bytes @ offset 48 (16-byte aligned for HLSL float4)
    float heightLayerMax[4];       // 16 bytes @ offset 64
    glm::vec4 heightLayerColor[4]; // 64 bytes @ offset 80
    // Total: 144 bytes per biome
};

// Constant buffer containing all biome materials
struct alignas(16) BiomeMaterialsCBuffer {
    BiomeMaterialGPU biomes[16];   // Max 16 biome types
    uint32_t biomeCount;
    float padding[3];
};

} // namespace Cortex::Scene
