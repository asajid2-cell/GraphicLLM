#pragma once

// SpatialGrid.h
// Spatial acceleration structure for chunk lookups.
// O(1) chunk coordinate queries, efficient radius and frustum queries.

#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "EditorWorld.h"  // For ChunkCoord, ChunkCoordHash

namespace Cortex {

// Frustum for visibility culling
struct Frustum {
    glm::vec4 planes[6];  // Left, Right, Bottom, Top, Near, Far

    // Create frustum from view-projection matrix
    static Frustum FromViewProj(const glm::mat4& viewProj);

    // Test if AABB is visible (conservative - may return true for hidden boxes)
    bool IsAABBVisible(const glm::vec3& min, const glm::vec3& max) const;

    // Test if point is inside frustum
    bool IsPointInside(const glm::vec3& point) const;
};

// SpatialGrid - acceleration structure for terrain chunk queries
class SpatialGrid {
public:
    SpatialGrid();
    ~SpatialGrid() = default;

    SpatialGrid(const SpatialGrid&) = delete;
    SpatialGrid& operator=(const SpatialGrid&) = delete;

    // Configuration
    void SetChunkSize(float size) { m_chunkSize = size; }
    [[nodiscard]] float GetChunkSize() const { return m_chunkSize; }

    // Chunk registration
    void RegisterChunk(const ChunkCoord& coord);
    void UnregisterChunk(const ChunkCoord& coord);
    void Clear();

    // Point queries
    [[nodiscard]] bool HasChunk(const ChunkCoord& coord) const;
    [[nodiscard]] ChunkCoord WorldToChunkCoord(const glm::vec3& worldPos) const;
    [[nodiscard]] glm::vec3 ChunkCoordToWorld(const ChunkCoord& coord) const;

    // Range queries
    [[nodiscard]] std::vector<ChunkCoord> GetChunksInRadius(const glm::vec3& center, float radius) const;
    [[nodiscard]] std::vector<ChunkCoord> GetChunksInAABB(const glm::vec3& min, const glm::vec3& max) const;
    [[nodiscard]] std::vector<ChunkCoord> GetChunksInFrustum(const Frustum& frustum) const;

    // Get all registered chunks
    [[nodiscard]] std::vector<ChunkCoord> GetAllChunks() const;
    [[nodiscard]] size_t GetChunkCount() const { return m_chunks.size(); }

    // Distance calculations
    [[nodiscard]] float GetDistanceToChunk(const glm::vec3& worldPos, const ChunkCoord& coord) const;
    [[nodiscard]] float GetDistanceToChunkSq(const glm::vec3& worldPos, const ChunkCoord& coord) const;

    // Get chunk AABB
    void GetChunkAABB(const ChunkCoord& coord, glm::vec3& outMin, glm::vec3& outMax) const;

private:
    // Chunk size (world units per chunk)
    float m_chunkSize = 64.0f;

    // Registered chunks (hash set for O(1) lookup)
    std::unordered_map<int64_t, ChunkCoord> m_chunks;

    // Helper to create hash key from coord
    static int64_t CoordToKey(const ChunkCoord& coord);
    static ChunkCoord KeyToCoord(int64_t key);
};

} // namespace Cortex
