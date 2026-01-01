#pragma once

// VegetationSpawner.h
// Procedural vegetation spawning system.
// Uses biome data and academic sampling algorithms for natural distribution.
// Reference: Bridson's Poisson Disk, Blue Noise, Lloyd Relaxation

#include "../Scene/VegetationTypes.h"
#include "../Scene/BiomeTypes.h"
#include "../Utils/PoissonDisk.h"
#include "../Utils/BlueNoise.h"
#include "../Utils/LloydRelaxation.h"
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace Cortex::Scene {
    class BiomeMap;
}

namespace Cortex::Editor {

using namespace Cortex::Scene;

// Callback for terrain height/normal queries
using TerrainQueryFunc = std::function<bool(float x, float z, float& outHeight, glm::vec3& outNormal)>;

class VegetationSpawner {
public:
    VegetationSpawner();
    ~VegetationSpawner();

    // Initialize with biome map and terrain query function
    void Initialize(const BiomeMap* biomeMap, TerrainQueryFunc terrainQuery);

    // Set spawn parameters
    void SetParams(const VegetationSpawnParams& params);
    const VegetationSpawnParams& GetParams() const { return m_params; }

    // Prototype management
    void AddPrototype(const VegetationPrototype& prototype);
    void ClearPrototypes();
    const std::vector<VegetationPrototype>& GetPrototypes() const { return m_prototypes; }

    // Biome vegetation density
    void SetBiomeDensity(BiomeType biome, const BiomeVegetationDensity& density);
    const BiomeVegetationDensity* GetBiomeDensity(BiomeType biome) const;

    // Spawn vegetation for a chunk
    // Returns the spawned instances
    VegetationChunk SpawnChunk(int32_t chunkX, int32_t chunkZ, float chunkSize, int resolution);

    // Spawn vegetation for a region (world coordinates)
    std::vector<VegetationInstance> SpawnRegion(float minX, float minZ, float maxX, float maxZ);

    // Update LOD levels for all instances based on camera position
    void UpdateLODs(VegetationChunk& chunk, const glm::vec3& cameraPos);

    // Cull instances outside frustum
    void FrustumCull(VegetationChunk& chunk, const glm::mat4& viewProj);

    // Get statistics
    VegetationStats GetStats() const { return m_stats; }

    // Load/save configuration
    bool LoadConfig(const std::string& path);
    bool SaveConfig(const std::string& path) const;

private:
    const BiomeMap* m_biomeMap = nullptr;
    TerrainQueryFunc m_terrainQuery;
    VegetationSpawnParams m_params;
    std::vector<VegetationPrototype> m_prototypes;
    std::unordered_map<BiomeType, BiomeVegetationDensity> m_biomeDensities;

    VegetationStats m_stats;
    mutable std::mutex m_mutex;

    // RNG state per-chunk for deterministic spawning
    uint32_t m_rngState = 0;

    // Academic sampling algorithms
    Utils::PoissonDiskSampler m_poissonSampler;
    Utils::LloydRelaxation m_lloydRelaxation;

    // Legacy Poisson disk sampling (kept for compatibility)
    struct PoissonGrid {
        std::vector<int> cells;
        int gridWidth = 0;
        int gridHeight = 0;
        float cellSize = 0.0f;
    };

    // Spawn helpers
    void SpawnCategory(VegetationType type, float density,
                       const std::vector<std::pair<uint32_t, float>>& weights,
                       float minSpacing, float minX, float minZ, float maxX, float maxZ,
                       std::vector<VegetationInstance>& outInstances);

    bool TrySpawnInstance(VegetationType type, uint32_t prototypeIndex,
                          float x, float z, VegetationInstance& outInstance);

    // Academic sampling method dispatch
    std::vector<glm::vec2> SamplePoints(SamplingMethod method,
                                         float minX, float minZ, float maxX, float maxZ,
                                         float minDistance, VegetationType type);

    // Sampling implementations
    std::vector<glm::vec2> SampleRandom(float minX, float minZ, float maxX, float maxZ,
                                         float density);

    std::vector<glm::vec2> SampleBridsonPoisson(float minX, float minZ, float maxX, float maxZ,
                                                 float minDistance, VegetationType type);

    std::vector<glm::vec2> SampleBlueNoise(float minX, float minZ, float maxX, float maxZ,
                                            float density);

    std::vector<glm::vec2> SamplePoissonRelaxed(float minX, float minZ, float maxX, float maxZ,
                                                 float minDistance, VegetationType type);

    std::vector<glm::vec2> SampleStratified(float minX, float minZ, float maxX, float maxZ,
                                             float spacing);

    // Legacy Poisson disk sampling
    std::vector<glm::vec2> PoissonDiskSample(float minX, float minZ, float maxX, float maxZ,
                                              float minDistance, int maxAttempts);

    bool IsValidPoissonPoint(const glm::vec2& point, float minDistance,
                             const PoissonGrid& grid, const std::vector<glm::vec2>& points,
                             float minX, float minZ);

    // Random number generation
    float RandomFloat();
    float RandomFloat(float min, float max);
    int RandomInt(int min, int max);
    void SeedRNG(int32_t chunkX, int32_t chunkZ);

    // Prototype selection
    uint32_t SelectPrototype(const std::vector<std::pair<uint32_t, float>>& weights);

    // Biome sampling
    float GetDensityAtPosition(VegetationType type, float x, float z) const;

    // Terrain rejection function for sampling
    bool IsValidTerrainPosition(float x, float z, VegetationType type) const;
};

} // namespace Cortex::Editor
