// ChunkGenerator.cpp
// Threaded terrain chunk generation implementation.

#include "ChunkGenerator.h"
#include "EditorWorld.h"
#include "Utils/MeshGenerator.h"
#include <chrono>

namespace Cortex {

ChunkGenerator::ChunkGenerator() = default;

ChunkGenerator::~ChunkGenerator() {
    Shutdown();
}

void ChunkGenerator::Initialize(uint32_t threadCount) {
    if (!m_workers.empty()) {
        return; // Already initialized
    }

    m_shutdownRequested = false;
    m_activeGenerations = 0;

    // Create worker threads
    for (uint32_t i = 0; i < threadCount; ++i) {
        m_workers.emplace_back(&ChunkGenerator::WorkerThread, this);
    }
}

void ChunkGenerator::Shutdown() {
    if (m_workers.empty()) {
        return;
    }

    // Signal shutdown
    m_shutdownRequested = true;
    m_requestCondition.notify_all();

    // Wait for workers to finish
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        while (!m_pendingRequests.empty()) {
            m_pendingRequests.pop();
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        m_completedChunks.clear();
    }
}

void ChunkGenerator::SetTerrainParams(const Scene::TerrainNoiseParams& params) {
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    m_terrainParams = params;
}

void ChunkGenerator::SetChunkSize(float size) {
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    m_chunkSize = size;
}

void ChunkGenerator::SetBiomeMap(std::shared_ptr<const Scene::BiomeMap> biomeMap) {
    std::lock_guard<std::mutex> lock(m_paramsMutex);
    m_biomeMap = std::move(biomeMap);
}

void ChunkGenerator::RequestChunk(const ChunkCoord& coord, ChunkLOD lod, float priority) {
    ChunkRequest request;
    request.coord = coord;
    request.lod = lod;
    request.priority = priority;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pendingRequests.push(request);
    }

    m_requestCondition.notify_one();
}

void ChunkGenerator::CancelRequest(const ChunkCoord& coord) {
    // Note: This is a simplified implementation that doesn't actually remove
    // from the priority queue (which would be O(n)). Instead, we could add
    // a cancelled set that's checked before processing.
    // For now, we just let the request complete and the caller can ignore it.
    (void)coord;
}

bool ChunkGenerator::HasCompletedChunks() const {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    return !m_completedChunks.empty();
}

std::vector<ChunkResult> ChunkGenerator::GetCompletedChunks(size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_completedMutex);

    if (maxCount == 0 || maxCount >= m_completedChunks.size()) {
        // Return all
        std::vector<ChunkResult> result = std::move(m_completedChunks);
        m_completedChunks.clear();
        return result;
    }

    // Return up to maxCount
    std::vector<ChunkResult> result(
        m_completedChunks.begin(),
        m_completedChunks.begin() + static_cast<ptrdiff_t>(maxCount)
    );
    m_completedChunks.erase(
        m_completedChunks.begin(),
        m_completedChunks.begin() + static_cast<ptrdiff_t>(maxCount)
    );

    return result;
}

size_t ChunkGenerator::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    return m_pendingRequests.size();
}

size_t ChunkGenerator::GetCompletedCount() const {
    std::lock_guard<std::mutex> lock(m_completedMutex);
    return m_completedChunks.size();
}

bool ChunkGenerator::IsIdle() const {
    return GetPendingCount() == 0 && m_activeGenerations == 0;
}

void ChunkGenerator::WorkerThread() {
    while (!m_shutdownRequested) {
        ChunkRequest request;
        bool hasRequest = false;

        // Wait for a request
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_requestCondition.wait(lock, [this] {
                return m_shutdownRequested || !m_pendingRequests.empty();
            });

            if (m_shutdownRequested) {
                break;
            }

            if (!m_pendingRequests.empty()) {
                request = m_pendingRequests.top();
                m_pendingRequests.pop();
                hasRequest = true;
            }
        }

        if (hasRequest) {
            m_activeGenerations++;

            // Generate the chunk
            ChunkResult result = GenerateChunk(request);

            // Add to completed queue
            {
                std::lock_guard<std::mutex> lock(m_completedMutex);
                m_completedChunks.push_back(std::move(result));
            }

            m_activeGenerations--;
        }
    }
}

ChunkResult ChunkGenerator::GenerateChunk(const ChunkRequest& request) {
    auto startTime = std::chrono::high_resolution_clock::now();

    // Get current terrain parameters (copy shared_ptr to keep BiomeMap alive during generation)
    Scene::TerrainNoiseParams params;
    float chunkSize;
    std::shared_ptr<const Scene::BiomeMap> biomeMap;
    {
        std::lock_guard<std::mutex> lock(m_paramsMutex);
        params = m_terrainParams;
        chunkSize = m_chunkSize;
        biomeMap = m_biomeMap;  // Increments refcount, keeps BiomeMap alive
    }

    // Determine grid dimension based on LOD
    uint32_t gridDim = GetGridDimForLOD(request.lod);

    // Generate the mesh - use biome variant if biome map is available
    std::shared_ptr<Scene::MeshData> mesh;
    if (biomeMap) {
        mesh = Utils::MeshGenerator::CreateTerrainHeightmapChunkWithBiomes(
            gridDim,
            chunkSize,
            request.coord.x,
            request.coord.z,
            params,
            biomeMap.get()  // Pass raw pointer, shared_ptr keeps BiomeMap alive
        );
    } else {
        mesh = Utils::MeshGenerator::CreateTerrainHeightmapChunk(
            gridDim,
            chunkSize,
            request.coord.x,
            request.coord.z,
            params
        );
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    float timeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    ChunkResult result;
    result.coord = request.coord;
    result.lod = request.lod;
    result.mesh = mesh;
    result.generationTimeMs = timeMs;

    return result;
}

uint32_t ChunkGenerator::GetGridDimForLOD(ChunkLOD lod) {
    // Use (2^n + 1) grid dimensions for perfect hierarchical vertex alignment.
    // With these values, every vertex in a lower LOD aligns exactly with
    // an even-indexed vertex in the higher LOD, preventing cracks at LOD boundaries.
    // Example: Half(33) vertex at j/32 = Full(65) vertex at 2j/64
    switch (lod) {
        case ChunkLOD::Full:    return 65;  // 64 subdivisions (2^6)
        case ChunkLOD::Half:    return 33;  // 32 subdivisions (2^5)
        case ChunkLOD::Quarter: return 17;  // 16 subdivisions (2^4)
        case ChunkLOD::Eighth:  return 9;   // 8 subdivisions (2^3)
        default:                return 65;
    }
}

} // namespace Cortex
