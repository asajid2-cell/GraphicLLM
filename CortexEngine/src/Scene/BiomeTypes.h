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

// Per-vertex biome data packed for GPU upload
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

// GPU-side biome material data (matches HLSL cbuffer layout)
// This is uploaded as a constant buffer for the terrain shader
struct alignas(16) BiomeMaterialGPU {
    glm::vec4 baseColor;           // 16 bytes
    glm::vec4 slopeColor;          // 16 bytes
    float roughness;               // 4 bytes
    float metallic;                // 4 bytes
    float heightLayerMin[4];       // 16 bytes (padded)
    float heightLayerMax[4];       // 16 bytes
    glm::vec4 heightLayerColor[4]; // 64 bytes
    float padding[2];              // 8 bytes for alignment
    // Total: 144 bytes per biome
};

// Constant buffer containing all biome materials
struct alignas(16) BiomeMaterialsCBuffer {
    BiomeMaterialGPU biomes[16];   // Max 16 biome types
    uint32_t biomeCount;
    float padding[3];
};

} // namespace Cortex::Scene
