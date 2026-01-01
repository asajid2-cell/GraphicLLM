#pragma once

// ChunkGenerator.h
// Threaded terrain chunk generation system.
// Generates chunk meshes on worker threads to avoid main thread blocking.

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "EditorWorld.h"  // For ChunkCoord, ChunkLOD
#include "Scene/TerrainNoise.h"

namespace Cortex {

// Forward declarations
namespace Scene {
    struct MeshData;
    class BiomeMap;
}

// Request for chunk generation
struct ChunkRequest {
    ChunkCoord coord;
    ChunkLOD lod;
    float priority = 0.0f;  // Higher = more urgent (closer to camera)
};

// Comparison for priority queue (higher priority first)
struct ChunkRequestCompare {
    bool operator()(const ChunkRequest& a, const ChunkRequest& b) const {
        return a.priority < b.priority;
    }
};

// Result of chunk generation
struct ChunkResult {
    ChunkCoord coord;
    ChunkLOD lod;
    std::shared_ptr<Scene::MeshData> mesh;
    float generationTimeMs = 0.0f;
};

// ChunkGenerator - multi-threaded terrain chunk generator
class ChunkGenerator {
public:
    ChunkGenerator();
    ~ChunkGenerator();

    ChunkGenerator(const ChunkGenerator&) = delete;
    ChunkGenerator& operator=(const ChunkGenerator&) = delete;

    // Initialize with worker thread count
    void Initialize(uint32_t threadCount = 2);
    void Shutdown();

    // Configure terrain parameters (thread-safe)
    void SetTerrainParams(const Scene::TerrainNoiseParams& params);
    void SetChunkSize(float size);
    void SetBiomeMap(std::shared_ptr<const Scene::BiomeMap> biomeMap);

    // Request chunk generation (thread-safe)
    void RequestChunk(const ChunkCoord& coord, ChunkLOD lod, float priority);

    // Cancel pending request (thread-safe)
    void CancelRequest(const ChunkCoord& coord);

    // Check for completed chunks (thread-safe, non-blocking)
    bool HasCompletedChunks() const;
    std::vector<ChunkResult> GetCompletedChunks(size_t maxCount = 0);

    // Statistics
    [[nodiscard]] size_t GetPendingCount() const;
    [[nodiscard]] size_t GetCompletedCount() const;
    [[nodiscard]] bool IsIdle() const;

private:
    // Worker thread function
    void WorkerThread();

    // Generate a single chunk (called from worker thread)
    ChunkResult GenerateChunk(const ChunkRequest& request);

    // Get grid dimension for LOD level
    static uint32_t GetGridDimForLOD(ChunkLOD lod);

    // Worker threads
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_shutdownRequested{false};

    // Request queue (priority sorted)
    std::priority_queue<ChunkRequest, std::vector<ChunkRequest>, ChunkRequestCompare> m_pendingRequests;
    mutable std::mutex m_requestMutex;
    std::condition_variable m_requestCondition;

    // Completed chunks
    std::vector<ChunkResult> m_completedChunks;
    mutable std::mutex m_completedMutex;

    // Terrain parameters (read by workers)
    Scene::TerrainNoiseParams m_terrainParams;
    float m_chunkSize = 64.0f;
    std::shared_ptr<const Scene::BiomeMap> m_biomeMap;  // Shared ownership prevents use-after-free
    mutable std::mutex m_paramsMutex;

    // Active generation tracking
    std::atomic<uint32_t> m_activeGenerations{0};
};

} // namespace Cortex
