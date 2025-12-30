// SpatialGrid.cpp
// Spatial acceleration structure implementation.

#include "SpatialGrid.h"
#include "EditorWorld.h"
#include <cmath>
#include <algorithm>

namespace Cortex {

// ============================================================================
// Frustum
// ============================================================================

Frustum Frustum::FromViewProj(const glm::mat4& vp) {
    Frustum f;

    // Extract frustum planes from view-projection matrix
    // Each plane is: ax + by + cz + d = 0
    // Using Gribb/Hartmann method

    // Left plane
    f.planes[0] = glm::vec4(
        vp[0][3] + vp[0][0],
        vp[1][3] + vp[1][0],
        vp[2][3] + vp[2][0],
        vp[3][3] + vp[3][0]
    );

    // Right plane
    f.planes[1] = glm::vec4(
        vp[0][3] - vp[0][0],
        vp[1][3] - vp[1][0],
        vp[2][3] - vp[2][0],
        vp[3][3] - vp[3][0]
    );

    // Bottom plane
    f.planes[2] = glm::vec4(
        vp[0][3] + vp[0][1],
        vp[1][3] + vp[1][1],
        vp[2][3] + vp[2][1],
        vp[3][3] + vp[3][1]
    );

    // Top plane
    f.planes[3] = glm::vec4(
        vp[0][3] - vp[0][1],
        vp[1][3] - vp[1][1],
        vp[2][3] - vp[2][1],
        vp[3][3] - vp[3][1]
    );

    // Near plane
    f.planes[4] = glm::vec4(
        vp[0][2],
        vp[1][2],
        vp[2][2],
        vp[3][2]
    );

    // Far plane
    f.planes[5] = glm::vec4(
        vp[0][3] - vp[0][2],
        vp[1][3] - vp[1][2],
        vp[2][3] - vp[2][2],
        vp[3][3] - vp[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(f.planes[i]));
        if (len > 0.0001f) {
            f.planes[i] /= len;
        }
    }

    return f;
}

bool Frustum::IsAABBVisible(const glm::vec3& min, const glm::vec3& max) const {
    // Test AABB against each frustum plane
    // If the AABB is entirely behind any plane, it's not visible
    for (int i = 0; i < 6; ++i) {
        const glm::vec4& plane = planes[i];

        // Find the p-vertex (most positive relative to plane normal)
        glm::vec3 pVertex;
        pVertex.x = (plane.x >= 0) ? max.x : min.x;
        pVertex.y = (plane.y >= 0) ? max.y : min.y;
        pVertex.z = (plane.z >= 0) ? max.z : min.z;

        // If p-vertex is behind plane, AABB is entirely outside
        float dist = glm::dot(glm::vec3(plane), pVertex) + plane.w;
        if (dist < 0) {
            return false;
        }
    }

    return true;
}

bool Frustum::IsPointInside(const glm::vec3& point) const {
    for (int i = 0; i < 6; ++i) {
        float dist = glm::dot(glm::vec3(planes[i]), point) + planes[i].w;
        if (dist < 0) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// SpatialGrid
// ============================================================================

SpatialGrid::SpatialGrid() = default;

void SpatialGrid::RegisterChunk(const ChunkCoord& coord) {
    int64_t key = CoordToKey(coord);
    m_chunks[key] = coord;
}

void SpatialGrid::UnregisterChunk(const ChunkCoord& coord) {
    int64_t key = CoordToKey(coord);
    m_chunks.erase(key);
}

void SpatialGrid::Clear() {
    m_chunks.clear();
}

bool SpatialGrid::HasChunk(const ChunkCoord& coord) const {
    int64_t key = CoordToKey(coord);
    return m_chunks.find(key) != m_chunks.end();
}

ChunkCoord SpatialGrid::WorldToChunkCoord(const glm::vec3& worldPos) const {
    ChunkCoord coord;
    coord.x = static_cast<int32_t>(std::floor(worldPos.x / m_chunkSize));
    coord.z = static_cast<int32_t>(std::floor(worldPos.z / m_chunkSize));
    return coord;
}

glm::vec3 SpatialGrid::ChunkCoordToWorld(const ChunkCoord& coord) const {
    // Returns center of chunk
    float halfSize = m_chunkSize * 0.5f;
    return glm::vec3(
        coord.x * m_chunkSize + halfSize,
        0.0f,
        coord.z * m_chunkSize + halfSize
    );
}

std::vector<ChunkCoord> SpatialGrid::GetChunksInRadius(const glm::vec3& center, float radius) const {
    std::vector<ChunkCoord> result;

    // Calculate bounding chunk range
    int32_t minCX = static_cast<int32_t>(std::floor((center.x - radius) / m_chunkSize));
    int32_t maxCX = static_cast<int32_t>(std::floor((center.x + radius) / m_chunkSize));
    int32_t minCZ = static_cast<int32_t>(std::floor((center.z - radius) / m_chunkSize));
    int32_t maxCZ = static_cast<int32_t>(std::floor((center.z + radius) / m_chunkSize));

    float radiusSq = radius * radius;

    for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
        for (int32_t cx = minCX; cx <= maxCX; ++cx) {
            ChunkCoord coord{cx, cz};
            if (HasChunk(coord)) {
                // Check if chunk center is within radius
                glm::vec3 chunkCenter = ChunkCoordToWorld(coord);
                float dx = chunkCenter.x - center.x;
                float dz = chunkCenter.z - center.z;
                if (dx * dx + dz * dz <= radiusSq) {
                    result.push_back(coord);
                }
            }
        }
    }

    return result;
}

std::vector<ChunkCoord> SpatialGrid::GetChunksInAABB(const glm::vec3& min, const glm::vec3& max) const {
    std::vector<ChunkCoord> result;

    int32_t minCX = static_cast<int32_t>(std::floor(min.x / m_chunkSize));
    int32_t maxCX = static_cast<int32_t>(std::floor(max.x / m_chunkSize));
    int32_t minCZ = static_cast<int32_t>(std::floor(min.z / m_chunkSize));
    int32_t maxCZ = static_cast<int32_t>(std::floor(max.z / m_chunkSize));

    for (int32_t cz = minCZ; cz <= maxCZ; ++cz) {
        for (int32_t cx = minCX; cx <= maxCX; ++cx) {
            ChunkCoord coord{cx, cz};
            if (HasChunk(coord)) {
                result.push_back(coord);
            }
        }
    }

    return result;
}

std::vector<ChunkCoord> SpatialGrid::GetChunksInFrustum(const Frustum& frustum) const {
    std::vector<ChunkCoord> result;
    result.reserve(m_chunks.size());

    for (const auto& [key, coord] : m_chunks) {
        glm::vec3 min, max;
        GetChunkAABB(coord, min, max);

        if (frustum.IsAABBVisible(min, max)) {
            result.push_back(coord);
        }
    }

    return result;
}

std::vector<ChunkCoord> SpatialGrid::GetAllChunks() const {
    std::vector<ChunkCoord> result;
    result.reserve(m_chunks.size());

    for (const auto& [key, coord] : m_chunks) {
        result.push_back(coord);
    }

    return result;
}

float SpatialGrid::GetDistanceToChunk(const glm::vec3& worldPos, const ChunkCoord& coord) const {
    return std::sqrt(GetDistanceToChunkSq(worldPos, coord));
}

float SpatialGrid::GetDistanceToChunkSq(const glm::vec3& worldPos, const ChunkCoord& coord) const {
    glm::vec3 chunkCenter = ChunkCoordToWorld(coord);
    float dx = chunkCenter.x - worldPos.x;
    float dz = chunkCenter.z - worldPos.z;
    return dx * dx + dz * dz;
}

void SpatialGrid::GetChunkAABB(const ChunkCoord& coord, glm::vec3& outMin, glm::vec3& outMax) const {
    outMin.x = coord.x * m_chunkSize;
    outMin.y = -100.0f;  // Approximate terrain min height
    outMin.z = coord.z * m_chunkSize;

    outMax.x = outMin.x + m_chunkSize;
    outMax.y = 200.0f;   // Approximate terrain max height
    outMax.z = outMin.z + m_chunkSize;
}

int64_t SpatialGrid::CoordToKey(const ChunkCoord& coord) {
    return (static_cast<int64_t>(coord.x) << 32) | static_cast<uint32_t>(coord.z);
}

ChunkCoord SpatialGrid::KeyToCoord(int64_t key) {
    ChunkCoord coord;
    coord.x = static_cast<int32_t>(key >> 32);
    coord.z = static_cast<int32_t>(key & 0xFFFFFFFF);
    return coord;
}

} // namespace Cortex
