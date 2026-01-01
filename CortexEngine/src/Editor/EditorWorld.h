#pragma once

// EditorWorld.h
// Clean world representation for Engine Editor Mode.
// Manages terrain chunks, entities, and spatial queries.

#include <memory>
#include <vector>
#include <unordered_set>
#include <functional>
#include <glm/glm.hpp>
#include "Scene/TerrainNoise.h"
#include "Scene/BiomeTypes.h"
#include "Utils/Result.h"

namespace Cortex {

// Forward declarations
namespace Graphics {
    class Renderer;
}
namespace Scene {
    class ECS_Registry;
    struct MeshData;
    class BiomeMap;
}

class ChunkGenerator;
class SpatialGrid;

// Chunk identifier
struct ChunkCoord {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && z == other.z;
    }
};

struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        return std::hash<int64_t>()((static_cast<int64_t>(c.x) << 32) | static_cast<uint32_t>(c.z));
    }
};

// LOD level for terrain chunks
enum class ChunkLOD : uint8_t {
    Full = 0,     // 64x64 grid (4096 vertices)
    Half = 1,     // 32x32 grid (1024 vertices)
    Quarter = 2,  // 16x16 grid (256 vertices)
    Eighth = 3    // 8x8 grid (64 vertices)
};

// Chunk state
struct ChunkState {
    ChunkCoord coord;
    ChunkLOD lod = ChunkLOD::Full;
    bool isLoaded = false;
    bool isVisible = true;
    float distanceFromCamera = 0.0f;
};

// World configuration
struct EditorWorldConfig {
    // Terrain parameters
    Scene::TerrainNoiseParams terrainParams;

    // Biome parameters
    Scene::BiomeMapParams biomeParams;
    bool useBiomes = true;            // Enable biome-based terrain coloring
    std::string biomesConfigPath = "assets/config/biomes.json";

    // Chunk settings
    float chunkSize = 64.0f;
    int32_t loadRadius = 8;           // Chunks to load around camera
    int32_t maxLoadedChunks = 500;    // Maximum loaded chunks

    // LOD distances (squared for efficiency)
    float lodDistance1Sq = 256.0f * 256.0f;   // Beyond this: Half LOD
    float lodDistance2Sq = 512.0f * 512.0f;   // Beyond this: Quarter LOD
    float lodDistance3Sq = 1024.0f * 1024.0f; // Beyond this: Eighth LOD

    // Threading
    uint32_t chunkGeneratorThreads = 2;
    uint32_t maxChunksPerFrame = 4;   // Max chunks to upload per frame
};

// EditorWorld - manages the game world in Engine Editor mode
class EditorWorld {
public:
    EditorWorld();
    ~EditorWorld();

    EditorWorld(const EditorWorld&) = delete;
    EditorWorld& operator=(const EditorWorld&) = delete;

    // Lifecycle
    Result<void> Initialize(Graphics::Renderer* renderer,
                           Scene::ECS_Registry* registry,
                           const EditorWorldConfig& config = {});
    void Shutdown();

    // Frame update
    void Update(const glm::vec3& cameraPosition, float deltaTime);

    // Configuration
    void SetTerrainParams(const Scene::TerrainNoiseParams& params);
    [[nodiscard]] const Scene::TerrainNoiseParams& GetTerrainParams() const { return m_config.terrainParams; }
    [[nodiscard]] const EditorWorldConfig& GetConfig() const { return m_config; }

    // Biome system
    void SetBiomeParams(const Scene::BiomeMapParams& params);
    [[nodiscard]] const Scene::BiomeMapParams& GetBiomeParams() const { return m_config.biomeParams; }
    [[nodiscard]] Scene::BiomeSample GetBiomeAt(float worldX, float worldZ) const;
    [[nodiscard]] const Scene::BiomeMap* GetBiomeMap() const { return m_biomeMap.get(); }
    [[nodiscard]] std::shared_ptr<const Scene::BiomeMap> GetBiomeMapShared() const { return m_biomeMap; }
    [[nodiscard]] bool AreBiomesEnabled() const { return m_config.useBiomes && m_biomeMap != nullptr; }
    void SetBiomesEnabled(bool enabled);

    // Chunk queries
    [[nodiscard]] bool IsChunkLoaded(const ChunkCoord& coord) const;
    [[nodiscard]] size_t GetLoadedChunkCount() const { return m_loadedChunks.size(); }
    [[nodiscard]] size_t GetPendingChunkCount() const;

    // Terrain height query (uses CPU noise sampling)
    [[nodiscard]] float GetTerrainHeight(float worldX, float worldZ) const;

    // Spatial queries
    [[nodiscard]] std::vector<ChunkCoord> GetChunksInRadius(const glm::vec3& center, float radius) const;
    [[nodiscard]] std::vector<ChunkCoord> GetVisibleChunks() const;

    // Debug info
    struct Stats {
        size_t loadedChunks = 0;
        size_t pendingChunks = 0;
        size_t chunksLoadedThisFrame = 0;
        size_t chunksUnloadedThisFrame = 0;
        float chunkGenerationTimeMs = 0.0f;
    };
    [[nodiscard]] Stats GetStats() const { return m_stats; }

private:
    // Chunk management
    void UpdateChunkLoading(const glm::vec3& cameraPos);
    void ProcessCompletedChunks();
    void UnloadDistantChunks(const glm::vec3& cameraPos);
    ChunkLOD CalculateLOD(float distanceSq) const;

    // Chunk creation/destruction
    void CreateChunkEntity(const ChunkCoord& coord,
                          std::shared_ptr<Scene::MeshData> mesh,
                          ChunkLOD lod);
    void DestroyChunkEntity(const ChunkCoord& coord);

    // Core references (not owned)
    Graphics::Renderer* m_renderer = nullptr;
    Scene::ECS_Registry* m_registry = nullptr;

    // Configuration
    EditorWorldConfig m_config;

    // Subsystems
    std::unique_ptr<ChunkGenerator> m_chunkGenerator;
    std::unique_ptr<SpatialGrid> m_spatialGrid;
    std::shared_ptr<Scene::BiomeMap> m_biomeMap;

    // Chunk tracking
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_loadedChunks;
    std::unordered_set<ChunkCoord, ChunkCoordHash> m_pendingChunks;

    // Statistics
    Stats m_stats;

    bool m_initialized = false;
};

} // namespace Cortex
