// BiomeMap.cpp
// Implementation of biome map generation using Voronoi cells and climate noise.

#include "BiomeMap.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Cortex::Scene {

BiomeMap::BiomeMap() {
    // Initialize default config
    m_defaultConfig.type = BiomeType::Plains;
    m_defaultConfig.name = "Default";
    m_defaultConfig.baseColor = glm::vec4(0.3f, 0.5f, 0.2f, 1.0f);
}

void BiomeMap::Initialize(const BiomeMapParams& params) {
    m_params = params;
}

void BiomeMap::SetBiomeConfigs(std::vector<BiomeConfig> configs) {
    m_configs = std::move(configs);

    // Build type-to-index lookup
    m_typeToIndex.clear();
    for (size_t i = 0; i < m_configs.size(); ++i) {
        m_typeToIndex[m_configs[i].type] = i;
    }
}

bool BiomeMap::LoadFromJSON(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json json;
        file >> json;

        // Load biome map parameters if present
        if (json.contains("biomeMapParams")) {
            auto& params = json["biomeMapParams"];
            if (params.contains("seed")) m_params.seed = params["seed"];
            if (params.contains("cellSize")) m_params.cellSize = params["cellSize"];
            if (params.contains("blendRadius")) m_params.blendRadius = params["blendRadius"];
            if (params.contains("temperatureFreq")) m_params.temperatureFreq = params["temperatureFreq"];
            if (params.contains("moistureFreq")) m_params.moistureFreq = params["moistureFreq"];
        }

        // Load biome configs
        if (!json.contains("biomes") || !json["biomes"].is_array()) {
            return false;
        }

        std::vector<BiomeConfig> configs;
        for (auto& biomeJson : json["biomes"]) {
            BiomeConfig config;

            // Type
            if (biomeJson.contains("type")) {
                config.type = StringToBiomeType(biomeJson["type"]);
            }
            if (biomeJson.contains("name")) {
                config.name = biomeJson["name"];
            }

            // Terrain modifiers
            if (biomeJson.contains("heightScale")) config.heightScale = biomeJson["heightScale"];
            if (biomeJson.contains("heightOffset")) config.heightOffset = biomeJson["heightOffset"];
            if (biomeJson.contains("slopeInfluence")) config.slopeInfluence = biomeJson["slopeInfluence"];

            // Material properties
            if (biomeJson.contains("baseColor")) {
                auto& c = biomeJson["baseColor"];
                config.baseColor = glm::vec4(c[0], c[1], c[2], c[3]);
            }
            if (biomeJson.contains("slopeColor")) {
                auto& c = biomeJson["slopeColor"];
                config.slopeColor = glm::vec4(c[0], c[1], c[2], c[3]);
            }
            if (biomeJson.contains("roughness")) config.roughness = biomeJson["roughness"];
            if (biomeJson.contains("metallic")) config.metallic = biomeJson["metallic"];
            if (biomeJson.contains("normalScale")) config.normalScale = biomeJson["normalScale"];

            // Height layers
            if (biomeJson.contains("heightLayers")) {
                for (auto& layerJson : biomeJson["heightLayers"]) {
                    BiomeHeightLayer layer;
                    if (layerJson.contains("minHeight")) layer.minHeight = layerJson["minHeight"];
                    if (layerJson.contains("maxHeight")) layer.maxHeight = layerJson["maxHeight"];
                    // Handle both "min"/"max" and "minHeight"/"maxHeight"
                    if (layerJson.contains("min")) layer.minHeight = layerJson["min"];
                    if (layerJson.contains("max")) layer.maxHeight = layerJson["max"];
                    if (layerJson.contains("color")) {
                        auto& c = layerJson["color"];
                        layer.color = glm::vec4(c[0], c[1], c[2], c[3]);
                    }
                    config.heightLayers.push_back(layer);
                }
            }

            // Vegetation density
            if (biomeJson.contains("vegetationDensity")) config.vegetationDensity = biomeJson["vegetationDensity"];
            if (biomeJson.contains("treeDensity")) config.treeDensity = biomeJson["treeDensity"];
            if (biomeJson.contains("rockDensity")) config.rockDensity = biomeJson["rockDensity"];
            if (biomeJson.contains("grassDensity")) config.grassDensity = biomeJson["grassDensity"];

            // Prop types
            if (biomeJson.contains("propTypes")) {
                for (auto& prop : biomeJson["propTypes"]) {
                    config.propTypes.push_back(prop);
                }
            }

            configs.push_back(config);
        }

        SetBiomeConfigs(std::move(configs));
        return true;
    }
    catch (...) {
        return false;
    }
}

BiomeSample BiomeMap::Sample(float worldX, float worldZ) const {
    BiomeSample sample;

    // Sample climate values
    float temperature = SampleTemperature(worldX, worldZ);
    float moisture = SampleMoisture(worldX, worldZ);

    sample.temperature = temperature;
    sample.moisture = moisture;

    // Get Voronoi cell info for blending
    float cellX, cellZ;
    float distToEdge = VoronoiDistance(worldX, worldZ, cellX, cellZ);

    // Primary biome from climate at cell center
    float cellTemp = SampleTemperature(cellX, cellZ);
    float cellMoist = SampleMoisture(cellX, cellZ);
    sample.primary = SelectBiomeFromClimate(cellTemp, cellMoist);

    // Find secondary biome by checking neighboring cells
    // We look at the climate in the direction away from cell center
    float dirX = worldX - cellX;
    float dirZ = worldZ - cellZ;
    float dirLen = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (dirLen > 0.001f) {
        dirX /= dirLen;
        dirZ /= dirLen;
    }

    // Sample neighbor cell (in direction away from current cell center)
    float neighborX = worldX + dirX * m_params.cellSize * 0.5f;
    float neighborZ = worldZ + dirZ * m_params.cellSize * 0.5f;
    float neighborTemp = SampleTemperature(neighborX, neighborZ);
    float neighborMoist = SampleMoisture(neighborX, neighborZ);
    sample.secondary = SelectBiomeFromClimate(neighborTemp, neighborMoist);

    // If same biome, no blending needed
    if (sample.primary == sample.secondary) {
        sample.blendWeight = 0.0f;
    } else {
        // Calculate blend weight based on distance to edge
        // Closer to edge = more blending with secondary
        sample.blendWeight = 1.0f - Smoothstep(0.0f, m_params.blendRadius, distToEdge);
    }

    return sample;
}

BiomeSample BiomeMap::SampleDetailed(float worldX, float worldZ, float baseHeight) const {
    BiomeSample sample = Sample(worldX, worldZ);

    // Could add height-based biome overrides here
    // e.g., force snow biome at very high elevations

    return sample;
}

const BiomeConfig& BiomeMap::GetConfig(BiomeType type) const {
    auto it = m_typeToIndex.find(type);
    if (it != m_typeToIndex.end() && it->second < m_configs.size()) {
        return m_configs[it->second];
    }
    return m_defaultConfig;
}

const BiomeConfig& BiomeMap::GetConfigByIndex(size_t index) const {
    if (index < m_configs.size()) {
        return m_configs[index];
    }
    return m_defaultConfig;
}

float BiomeMap::GetHeightScale(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    return GetConfig(sample.primary).heightScale;
}

float BiomeMap::GetHeightOffset(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    return GetConfig(sample.primary).heightOffset;
}

float BiomeMap::GetBlendedHeightScale(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.heightScale, secondaryConfig.heightScale, sample.blendWeight);
}

float BiomeMap::GetBlendedHeightOffset(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.heightOffset, secondaryConfig.heightOffset, sample.blendWeight);
}

float BiomeMap::GetVegetationDensity(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.vegetationDensity, secondaryConfig.vegetationDensity, sample.blendWeight);
}

float BiomeMap::GetTreeDensity(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.treeDensity, secondaryConfig.treeDensity, sample.blendWeight);
}

float BiomeMap::GetRockDensity(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.rockDensity, secondaryConfig.rockDensity, sample.blendWeight);
}

glm::vec3 BiomeMap::GetBlendedColor(float worldX, float worldZ) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);
    return glm::mix(primaryConfig.baseColor, secondaryConfig.baseColor, sample.blendWeight);
}

glm::vec3 BiomeMap::SampleHeightLayerColor(const BiomeConfig& config, float height, float slope) const {
    glm::vec3 baseLayerColor = glm::vec3(config.baseColor);

    // If no height layers defined, use base color
    if (config.heightLayers.empty()) {
        // Apply slope influence
        float slopeFactor = slope * config.slopeInfluence;
        return glm::mix(baseLayerColor, glm::vec3(config.slopeColor), slopeFactor);
    }

    // Find the layer(s) this height falls into and interpolate
    const auto& layers = config.heightLayers;
    glm::vec3 layerColor = baseLayerColor;

    // Check if below all layers
    if (height < layers[0].minHeight) {
        layerColor = glm::vec3(layers[0].color);
    }
    // Check if above all layers
    else if (height >= layers.back().maxHeight) {
        layerColor = glm::vec3(layers.back().color);
    }
    else {
        // Find the layer(s) and interpolate
        for (size_t i = 0; i < layers.size(); ++i) {
            const auto& layer = layers[i];

            if (height >= layer.minHeight && height < layer.maxHeight) {
                // Inside this layer
                layerColor = glm::vec3(layer.color);

                // Smooth transition at layer boundaries (blend with adjacent layers)
                float blendZone = (layer.maxHeight - layer.minHeight) * 0.2f; // 20% blend zone

                // Blend with previous layer at bottom
                if (i > 0 && height < layer.minHeight + blendZone) {
                    float t = (height - layer.minHeight) / blendZone;
                    t = Smoothstep(0.0f, 1.0f, t);
                    layerColor = glm::mix(glm::vec3(layers[i - 1].color), layerColor, t);
                }
                // Blend with next layer at top
                else if (i < layers.size() - 1 && height > layer.maxHeight - blendZone) {
                    float t = (height - (layer.maxHeight - blendZone)) / blendZone;
                    t = Smoothstep(0.0f, 1.0f, t);
                    layerColor = glm::mix(layerColor, glm::vec3(layers[i + 1].color), t);
                }
                break;
            }
        }
    }

    // Apply slope influence - blend toward slope color on steep terrain
    float slopeFactor = slope * config.slopeInfluence;
    return glm::mix(layerColor, glm::vec3(config.slopeColor), slopeFactor);
}

glm::vec3 BiomeMap::GetHeightLayeredColor(float worldX, float worldZ, float height, float slope) const {
    BiomeSample sample = Sample(worldX, worldZ);
    const auto& primaryConfig = GetConfig(sample.primary);
    const auto& secondaryConfig = GetConfig(sample.secondary);

    // Get height-layered color for each biome
    glm::vec3 primaryColor = SampleHeightLayerColor(primaryConfig, height, slope);
    glm::vec3 secondaryColor = SampleHeightLayerColor(secondaryConfig, height, slope);

    // Blend between biomes
    return glm::mix(primaryColor, secondaryColor, sample.blendWeight);
}

BiomeType BiomeMap::SelectBiomeFromClimate(float temperature, float moisture) const {
    // Whittaker diagram style biome selection
    // Temperature: 0 = cold, 1 = hot
    // Moisture: 0 = dry, 1 = wet

    // Quantize to grid for discrete biome selection
    // 4x4 grid mapping
    int tempIdx = static_cast<int>(temperature * 3.99f);
    int moistIdx = static_cast<int>(moisture * 3.99f);
    tempIdx = std::clamp(tempIdx, 0, 3);
    moistIdx = std::clamp(moistIdx, 0, 3);

    // Climate-to-biome lookup table (Whittaker-style)
    // Rows = moisture (0=dry to 3=wet)
    // Cols = temperature (0=cold to 3=hot)
    static const BiomeType climateBiomes[4][4] = {
        // Cold       Cool        Warm        Hot
        { BiomeType::Tundra,    BiomeType::Mountains, BiomeType::Desert,   BiomeType::Desert    }, // Dry
        { BiomeType::Tundra,    BiomeType::Plains,    BiomeType::Plains,   BiomeType::Desert    }, // Low moisture
        { BiomeType::Forest,    BiomeType::Forest,    BiomeType::Plains,   BiomeType::Swamp     }, // Medium moisture
        { BiomeType::Forest,    BiomeType::Swamp,     BiomeType::Swamp,    BiomeType::Beach     }, // Wet
    };

    return climateBiomes[moistIdx][tempIdx];
}

float BiomeMap::SampleTemperature(float worldX, float worldZ) const {
    float noise = FBMNoise(
        worldX + m_params.seed * 1000.0f,
        worldZ + m_params.seed * 1000.0f,
        m_params.temperatureFreq,
        m_params.climateOctaves,
        m_params.climateLacunarity,
        m_params.climateGain
    );
    // Map from [-1, 1] to [0, 1]
    return noise * 0.5f + 0.5f;
}

float BiomeMap::SampleMoisture(float worldX, float worldZ) const {
    float noise = FBMNoise(
        worldX + m_params.seed * 2000.0f,
        worldZ + m_params.seed * 2000.0f,
        m_params.moistureFreq,
        m_params.climateOctaves,
        m_params.climateLacunarity,
        m_params.climateGain
    );
    // Map from [-1, 1] to [0, 1]
    return noise * 0.5f + 0.5f;
}

float BiomeMap::VoronoiDistance(float worldX, float worldZ, float& cellX, float& cellZ) const {
    // Scale to cell coordinates
    float scaledX = worldX / m_params.cellSize;
    float scaledZ = worldZ / m_params.cellSize;

    // Integer cell coordinates
    int cellIntX = static_cast<int>(std::floor(scaledX));
    int cellIntZ = static_cast<int>(std::floor(scaledZ));

    float minDist = 1e10f;
    float secondMinDist = 1e10f;
    float nearestCellX = 0.0f;
    float nearestCellZ = 0.0f;

    // Check 3x3 neighborhood
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cx = cellIntX + dx;
            int cz = cellIntZ + dz;

            // Random offset within cell (jittered grid)
            float jitterX = Hash2D(cx + m_params.seed, cz) * 0.8f + 0.1f;
            float jitterZ = Hash2D(cx, cz + m_params.seed) * 0.8f + 0.1f;

            float pointX = (cx + jitterX) * m_params.cellSize;
            float pointZ = (cz + jitterZ) * m_params.cellSize;

            float distSq = (worldX - pointX) * (worldX - pointX) + (worldZ - pointZ) * (worldZ - pointZ);

            if (distSq < minDist) {
                secondMinDist = minDist;
                minDist = distSq;
                nearestCellX = pointX;
                nearestCellZ = pointZ;
            } else if (distSq < secondMinDist) {
                secondMinDist = distSq;
            }
        }
    }

    cellX = nearestCellX;
    cellZ = nearestCellZ;

    // Distance to nearest cell edge is approximated by
    // (distance to second nearest - distance to nearest) / 2
    float dist1 = std::sqrt(minDist);
    float dist2 = std::sqrt(secondMinDist);
    return (dist2 - dist1) * 0.5f;
}

float BiomeMap::Hash2D(float x, float z) const {
    // Simple hash function for deterministic random values
    float n = std::sin(x * 12.9898f + z * 78.233f) * 43758.5453f;
    return n - std::floor(n);
}

float BiomeMap::FBMNoise(float x, float z, float freq, uint32_t octaves, float lacunarity, float gain) const {
    float amplitude = 1.0f;
    float frequency = freq;
    float value = 0.0f;
    float maxValue = 0.0f;

    for (uint32_t i = 0; i < octaves; ++i) {
        value += amplitude * Noise2D(x * frequency, z * frequency);
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

float BiomeMap::Noise2D(float x, float z) const {
    // Simple gradient noise implementation
    // Uses a combination of integer coords and fractional parts

    int ix = static_cast<int>(std::floor(x));
    int iz = static_cast<int>(std::floor(z));
    float fx = x - ix;
    float fz = z - iz;

    // Smoothstep for interpolation
    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fz * fz * (3.0f - 2.0f * fz);

    // Hash corners
    float n00 = Hash2D(static_cast<float>(ix), static_cast<float>(iz)) * 2.0f - 1.0f;
    float n10 = Hash2D(static_cast<float>(ix + 1), static_cast<float>(iz)) * 2.0f - 1.0f;
    float n01 = Hash2D(static_cast<float>(ix), static_cast<float>(iz + 1)) * 2.0f - 1.0f;
    float n11 = Hash2D(static_cast<float>(ix + 1), static_cast<float>(iz + 1)) * 2.0f - 1.0f;

    // Bilinear interpolation
    float nx0 = glm::mix(n00, n10, u);
    float nx1 = glm::mix(n01, n11, u);
    return glm::mix(nx0, nx1, v);
}

float BiomeMap::Smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace Cortex::Scene
