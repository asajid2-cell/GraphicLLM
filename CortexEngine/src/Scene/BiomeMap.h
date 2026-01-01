#pragma once

// BiomeMap.h
// Biome map generator using Voronoi cells and climate noise.
// Generates biome assignments from world coordinates for terrain generation.

#include "BiomeTypes.h"
#include "TerrainNoise.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace Cortex::Scene {

// BiomeMapParams is defined in BiomeTypes.h (included above)

// BiomeMap - generates biome assignments from world coordinates
class BiomeMap {
public:
    BiomeMap();
    ~BiomeMap() = default;

    // Initialize with parameters
    void Initialize(const BiomeMapParams& params);

    // Load biome configurations from vector
    void SetBiomeConfigs(std::vector<BiomeConfig> configs);

    // Load biome configurations from JSON file
    bool LoadFromJSON(const std::string& path);

    // Sample biome at world position (thread-safe, no mutation)
    [[nodiscard]] BiomeSample Sample(float worldX, float worldZ) const;

    // Sample biome with 4-way blending support
    [[nodiscard]] BiomeSample4 Sample4(float worldX, float worldZ) const;

    // Sample with noise-modulated blending at boundaries
    [[nodiscard]] BiomeSample4 Sample4WithNoise(float worldX, float worldZ, float noiseScale = 0.1f, float noiseStrength = 0.15f) const;

    // Sample with height-based biome override (snowline)
    [[nodiscard]] BiomeSample4 Sample4WithHeightOverride(float worldX, float worldZ, float height) const;

    // Sample biome with full detail (includes height/offset calculations)
    [[nodiscard]] BiomeSample SampleDetailed(float worldX, float worldZ, float baseHeight) const;

    // Get biome config by type
    [[nodiscard]] const BiomeConfig& GetConfig(BiomeType type) const;

    // Get biome config by index
    [[nodiscard]] const BiomeConfig& GetConfigByIndex(size_t index) const;

    // Get all biome configs
    [[nodiscard]] const std::vector<BiomeConfig>& GetAllConfigs() const { return m_configs; }

    // Get parameters
    [[nodiscard]] const BiomeMapParams& GetParams() const { return m_params; }

    // Terrain height modifiers based on biome at position
    [[nodiscard]] float GetHeightScale(float worldX, float worldZ) const;
    [[nodiscard]] float GetHeightOffset(float worldX, float worldZ) const;

    // Get blended height modifiers (accounts for biome boundaries)
    [[nodiscard]] float GetBlendedHeightScale(float worldX, float worldZ) const;
    [[nodiscard]] float GetBlendedHeightOffset(float worldX, float worldZ) const;

    // Vegetation density at position
    [[nodiscard]] float GetVegetationDensity(float worldX, float worldZ) const;
    [[nodiscard]] float GetTreeDensity(float worldX, float worldZ) const;
    [[nodiscard]] float GetRockDensity(float worldX, float worldZ) const;

    // Get blended biome color at position (for terrain vertex coloring)
    [[nodiscard]] glm::vec3 GetBlendedColor(float worldX, float worldZ) const;

    // Get height and slope-aware blended color at position
    // height: terrain height at this position
    // slope: terrain slope (0 = flat, 1 = vertical)
    [[nodiscard]] glm::vec3 GetHeightLayeredColor(float worldX, float worldZ, float height, float slope) const;

    // Check if biome configs are loaded
    [[nodiscard]] bool IsInitialized() const { return !m_configs.empty(); }

private:
    BiomeMapParams m_params;
    std::vector<BiomeConfig> m_configs;

    // Default biome config for fallback
    BiomeConfig m_defaultConfig;

    // Lookup table for biome type to config index
    std::unordered_map<BiomeType, size_t> m_typeToIndex;

    // Climate-to-biome selection (Whittaker diagram style)
    // Uses temperature (x-axis) and moisture (y-axis) to determine biome
    [[nodiscard]] BiomeType SelectBiomeFromClimate(float temperature, float moisture) const;

    // Noise sampling helpers (thread-safe, const)
    [[nodiscard]] float SampleTemperature(float worldX, float worldZ) const;
    [[nodiscard]] float SampleMoisture(float worldX, float worldZ) const;

    // Voronoi-based cell distance for biome boundaries
    // Returns distance to nearest cell edge, and outputs cell center coordinates
    [[nodiscard]] float VoronoiDistance(float worldX, float worldZ, float& cellX, float& cellZ) const;

    // Hash function for Voronoi cell randomization
    [[nodiscard]] float Hash2D(float x, float z) const;

    // FBM noise helper
    [[nodiscard]] float FBMNoise(float x, float z, float freq, uint32_t octaves, float lacunarity, float gain) const;

    // Single octave Perlin-like noise
    [[nodiscard]] float Noise2D(float x, float z) const;

    // Smoothstep for blending
    [[nodiscard]] static float Smoothstep(float edge0, float edge1, float x);

    // Sample height layer color for a single biome (with interpolation between layers)
    [[nodiscard]] glm::vec3 SampleHeightLayerColor(const BiomeConfig& config, float height, float slope) const;

    // Find up to 4 nearest biomes and their weights at a position
    void FindNearestBiomes(float worldX, float worldZ,
                           BiomeType outBiomes[4], float outWeights[4], int& outCount) const;

    // Apply noise modulation to blend weights
    void ApplyNoiseToWeights(float worldX, float worldZ, float weights[4],
                             float noiseScale, float noiseStrength) const;

    // Apply height-based biome override (snowline effect)
    void ApplyHeightOverride(float height, float worldX, float worldZ,
                             BiomeType biomes[4], float weights[4], int& activeCount) const;

    // Snowline parameters (could be moved to config)
    static constexpr float SNOWLINE_START = 120.0f;
    static constexpr float SNOWLINE_FULL = 160.0f;
    static constexpr BiomeType SNOW_BIOME = BiomeType::Tundra;
};

} // namespace Cortex::Scene
