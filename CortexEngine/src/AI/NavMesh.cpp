// NavMesh.cpp
// Navigation mesh implementation.

#include "NavMesh.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <random>

namespace Cortex::AI {

uint64_t NavMesh::GetSpatialKey(const glm::vec3& pos) const {
    int32_t x = static_cast<int32_t>(std::floor(pos.x / m_spatialCellSize));
    int32_t z = static_cast<int32_t>(std::floor(pos.z / m_spatialCellSize));
    return (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(z);
}

void NavMesh::RebuildSpatialHash() {
    m_spatialHash.clear();

    for (uint32_t i = 0; i < m_polygons.size(); i++) {
        const NavPoly& poly = m_polygons[i];

        // Get polygon AABB
        glm::vec3 polyMin(FLT_MAX), polyMax(-FLT_MAX);
        for (uint8_t v = 0; v < poly.vertCount; v++) {
            const glm::vec3& vert = m_vertices[poly.vertIndices[v]];
            polyMin = glm::min(polyMin, vert);
            polyMax = glm::max(polyMax, vert);
        }

        // Add to all overlapping cells
        int32_t minX = static_cast<int32_t>(std::floor(polyMin.x / m_spatialCellSize));
        int32_t maxX = static_cast<int32_t>(std::floor(polyMax.x / m_spatialCellSize));
        int32_t minZ = static_cast<int32_t>(std::floor(polyMin.z / m_spatialCellSize));
        int32_t maxZ = static_cast<int32_t>(std::floor(polyMax.z / m_spatialCellSize));

        for (int32_t x = minX; x <= maxX; x++) {
            for (int32_t z = minZ; z <= maxZ; z++) {
                uint64_t key = (static_cast<uint64_t>(x) << 32) | static_cast<uint64_t>(z);
                m_spatialHash[key].polyRefs.push_back(i);
            }
        }
    }
}

bool NavMesh::Build(const std::vector<glm::vec3>& vertices,
                     const std::vector<uint32_t>& indices,
                     const NavMeshBuildSettings& settings) {
    m_settings = settings;
    m_vertices.clear();
    m_polygons.clear();

    if (vertices.empty() || indices.empty()) {
        return false;
    }

    // Compute bounds
    m_boundsMin = vertices[0];
    m_boundsMax = vertices[0];
    for (const auto& v : vertices) {
        m_boundsMin = glm::min(m_boundsMin, v);
        m_boundsMax = glm::max(m_boundsMax, v);
    }

    // Simple triangle-based navmesh for now
    // A full implementation would do voxelization, watershed, etc.
    m_vertices = vertices;

    // Create polygons from triangles
    for (size_t i = 0; i < indices.size(); i += 3) {
        NavPoly poly;
        poly.vertCount = 3;
        poly.vertIndices[0] = indices[i];
        poly.vertIndices[1] = indices[i + 1];
        poly.vertIndices[2] = indices[i + 2];

        // Check slope
        glm::vec3 v0 = vertices[indices[i]];
        glm::vec3 v1 = vertices[indices[i + 1]];
        glm::vec3 v2 = vertices[indices[i + 2]];

        glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        float slopeDot = glm::dot(normal, glm::vec3(0, 1, 0));
        float slopeAngle = glm::degrees(std::acos(slopeDot));

        if (slopeAngle > settings.agentMaxSlope) {
            continue;  // Too steep
        }

        poly.areaType = NavAreaType::Walkable;
        poly.flags = NavAreaFlags::Walk;
        poly.cost = 1.0f;

        m_polygons.push_back(poly);
    }

    // Build adjacency
    for (size_t i = 0; i < m_polygons.size(); i++) {
        NavPoly& polyA = m_polygons[i];

        for (size_t j = i + 1; j < m_polygons.size(); j++) {
            NavPoly& polyB = m_polygons[j];

            // Check for shared edges
            for (uint8_t ea = 0; ea < polyA.vertCount; ea++) {
                uint32_t a0 = polyA.vertIndices[ea];
                uint32_t a1 = polyA.vertIndices[(ea + 1) % polyA.vertCount];

                for (uint8_t eb = 0; eb < polyB.vertCount; eb++) {
                    uint32_t b0 = polyB.vertIndices[eb];
                    uint32_t b1 = polyB.vertIndices[(eb + 1) % polyB.vertCount];

                    // Shared edge (reversed winding)
                    if ((a0 == b1 && a1 == b0) || (a0 == b0 && a1 == b1)) {
                        polyA.neighborPolys[ea] = static_cast<uint32_t>(j);
                        polyB.neighborPolys[eb] = static_cast<uint32_t>(i);
                    }
                }
            }
        }
    }

    RebuildSpatialHash();
    return true;
}

bool NavMesh::BuildFromHeightfield(const float* heightData, uint32_t width, uint32_t height,
                                    const glm::vec3& origin, float cellSize,
                                    const NavMeshBuildSettings& settings) {
    m_settings = settings;
    m_vertices.clear();
    m_polygons.clear();

    // Create vertex grid
    m_vertices.resize(width * height);
    for (uint32_t z = 0; z < height; z++) {
        for (uint32_t x = 0; x < width; x++) {
            float h = heightData[z * width + x];
            m_vertices[z * width + x] = origin + glm::vec3(x * cellSize, h, z * cellSize);
        }
    }

    // Create triangles
    std::vector<uint32_t> indices;
    for (uint32_t z = 0; z < height - 1; z++) {
        for (uint32_t x = 0; x < width - 1; x++) {
            uint32_t i0 = z * width + x;
            uint32_t i1 = z * width + x + 1;
            uint32_t i2 = (z + 1) * width + x;
            uint32_t i3 = (z + 1) * width + x + 1;

            // Two triangles per cell
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return Build(m_vertices, indices, settings);
}

bool NavMesh::AddTile(const NavMeshTile& tile) {
    // Check for existing tile
    for (auto& t : m_tiles) {
        if (t.tileX == tile.tileX && t.tileZ == tile.tileZ) {
            t = tile;
            return true;
        }
    }
    m_tiles.push_back(tile);
    return true;
}

bool NavMesh::RemoveTile(int32_t tileX, int32_t tileZ) {
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (it->tileX == tileX && it->tileZ == tileZ) {
            m_tiles.erase(it);
            return true;
        }
    }
    return false;
}

NavMeshTile* NavMesh::GetTile(int32_t tileX, int32_t tileZ) {
    for (auto& t : m_tiles) {
        if (t.tileX == tileX && t.tileZ == tileZ) {
            return &t;
        }
    }
    return nullptr;
}

const NavMeshTile* NavMesh::GetTile(int32_t tileX, int32_t tileZ) const {
    for (const auto& t : m_tiles) {
        if (t.tileX == tileX && t.tileZ == tileZ) {
            return &t;
        }
    }
    return nullptr;
}

uint32_t NavMesh::AddOffMeshConnection(const OffMeshConnection& connection) {
    m_offMeshConnections.push_back(connection);
    return static_cast<uint32_t>(m_offMeshConnections.size() - 1);
}

void NavMesh::RemoveOffMeshConnection(uint32_t connectionId) {
    if (connectionId < m_offMeshConnections.size()) {
        m_offMeshConnections.erase(m_offMeshConnections.begin() + connectionId);
    }
}

NavMeshPoint NavMesh::FindNearestPoint(const glm::vec3& position, float searchRadius) const {
    NavMeshPoint result;
    float bestDistSq = searchRadius * searchRadius;

    // Check spatial hash cells in range
    int32_t cellRange = static_cast<int32_t>(std::ceil(searchRadius / m_spatialCellSize));
    int32_t centerX = static_cast<int32_t>(std::floor(position.x / m_spatialCellSize));
    int32_t centerZ = static_cast<int32_t>(std::floor(position.z / m_spatialCellSize));

    for (int32_t dx = -cellRange; dx <= cellRange; dx++) {
        for (int32_t dz = -cellRange; dz <= cellRange; dz++) {
            uint64_t key = (static_cast<uint64_t>(centerX + dx) << 32) |
                           static_cast<uint64_t>(centerZ + dz);

            auto it = m_spatialHash.find(key);
            if (it == m_spatialHash.end()) continue;

            for (uint32_t polyRef : it->second.polyRefs) {
                const NavPoly& poly = m_polygons[polyRef];

                // Get polygon center
                glm::vec3 center(0.0f);
                for (uint8_t i = 0; i < poly.vertCount; i++) {
                    center += m_vertices[poly.vertIndices[i]];
                }
                center /= static_cast<float>(poly.vertCount);

                // Check distance
                glm::vec3 diff = position - center;
                float distSq = glm::dot(diff, diff);

                if (distSq < bestDistSq) {
                    bestDistSq = distSq;
                    result.position = center;
                    result.polyRef = polyRef;
                    result.valid = true;
                }
            }
        }
    }

    return result;
}

NavMeshPoint NavMesh::FindRandomPoint() const {
    if (m_polygons.empty()) {
        return NavMeshPoint();
    }

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, m_polygons.size() - 1);

    uint32_t polyRef = static_cast<uint32_t>(dist(rng));
    return NavMeshPoint(GetPolyCenter(polyRef), polyRef);
}

NavMeshPoint NavMesh::FindRandomPointInRadius(const glm::vec3& center, float radius) const {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> radiusDist(0.0f, radius);

    for (int attempt = 0; attempt < 30; attempt++) {
        float angle = angleDist(rng);
        float r = std::sqrt(radiusDist(rng) / radius) * radius;  // Uniform in disk

        glm::vec3 testPos = center + glm::vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        NavMeshPoint result = FindNearestPoint(testPos, m_settings.agentRadius * 2.0f);

        if (result.valid) {
            return result;
        }
    }

    return NavMeshPoint();
}

uint32_t NavMesh::GetPolyAt(const glm::vec3& position) const {
    uint64_t key = GetSpatialKey(position);
    auto it = m_spatialHash.find(key);

    if (it != m_spatialHash.end()) {
        for (uint32_t polyRef : it->second.polyRefs) {
            if (IsPointInPoly(polyRef, position)) {
                return polyRef;
            }
        }
    }

    return UINT32_MAX;
}

bool NavMesh::IsValidPoly(uint32_t polyRef) const {
    return polyRef < m_polygons.size();
}

glm::vec3 NavMesh::GetPolyCenter(uint32_t polyRef) const {
    if (!IsValidPoly(polyRef)) {
        return glm::vec3(0.0f);
    }

    const NavPoly& poly = m_polygons[polyRef];
    glm::vec3 center(0.0f);

    for (uint8_t i = 0; i < poly.vertCount; i++) {
        center += m_vertices[poly.vertIndices[i]];
    }

    return center / static_cast<float>(poly.vertCount);
}

NavAreaType NavMesh::GetPolyAreaType(uint32_t polyRef) const {
    if (!IsValidPoly(polyRef)) {
        return NavAreaType::NotWalkable;
    }
    return m_polygons[polyRef].areaType;
}

float NavMesh::GetPolyCost(uint32_t polyRef) const {
    if (!IsValidPoly(polyRef)) {
        return FLT_MAX;
    }
    return m_polygons[polyRef].cost;
}

float NavMesh::GetHeight(const glm::vec3& position, float searchRange) const {
    NavMeshPoint point = FindNearestPoint(position, searchRange);
    if (point.valid) {
        return point.position.y;
    }
    return position.y;
}

NavMeshRaycastResult NavMesh::Raycast(const glm::vec3& start, const glm::vec3& end) const {
    NavMeshRaycastResult result;
    result.hit = false;
    result.hitDistance = FLT_MAX;

    glm::vec3 dir = end - start;
    float maxDist = glm::length(dir);
    if (maxDist < 0.0001f) {
        return result;
    }
    dir /= maxDist;

    // Simple approach: test all polygons
    for (uint32_t i = 0; i < m_polygons.size(); i++) {
        const NavPoly& poly = m_polygons[i];

        // Triangulate polygon and test each triangle
        for (uint8_t v = 1; v < poly.vertCount - 1; v++) {
            glm::vec3 v0 = m_vertices[poly.vertIndices[0]];
            glm::vec3 v1 = m_vertices[poly.vertIndices[v]];
            glm::vec3 v2 = m_vertices[poly.vertIndices[v + 1]];

            // Ray-triangle intersection
            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 h = glm::cross(dir, edge2);
            float a = glm::dot(edge1, h);

            if (std::abs(a) < 0.0001f) continue;

            float f = 1.0f / a;
            glm::vec3 s = start - v0;
            float u = f * glm::dot(s, h);

            if (u < 0.0f || u > 1.0f) continue;

            glm::vec3 q = glm::cross(s, edge1);
            float v_coord = f * glm::dot(dir, q);

            if (v_coord < 0.0f || u + v_coord > 1.0f) continue;

            float t = f * glm::dot(edge2, q);

            if (t > 0.0001f && t < result.hitDistance && t < maxDist) {
                result.hit = true;
                result.hitDistance = t;
                result.hitPoint = start + dir * t;
                result.hitNormal = glm::normalize(glm::cross(edge1, edge2));
                result.hitPolyRef = i;
                result.hitAreaType = poly.areaType;
            }
        }
    }

    return result;
}

bool NavMesh::IsPointInPoly(uint32_t polyRef, const glm::vec3& point) const {
    if (!IsValidPoly(polyRef)) {
        return false;
    }

    const NavPoly& poly = m_polygons[polyRef];

    // 2D point-in-polygon test (XZ plane)
    bool inside = false;
    for (uint8_t i = 0, j = poly.vertCount - 1; i < poly.vertCount; j = i++) {
        const glm::vec3& vi = m_vertices[poly.vertIndices[i]];
        const glm::vec3& vj = m_vertices[poly.vertIndices[j]];

        if (((vi.z > point.z) != (vj.z > point.z)) &&
            (point.x < (vj.x - vi.x) * (point.z - vi.z) / (vj.z - vi.z) + vi.x)) {
            inside = !inside;
        }
    }

    return inside;
}

std::vector<uint32_t> NavMesh::GetPolyNeighbors(uint32_t polyRef) const {
    std::vector<uint32_t> neighbors;

    if (!IsValidPoly(polyRef)) {
        return neighbors;
    }

    const NavPoly& poly = m_polygons[polyRef];
    for (uint8_t i = 0; i < poly.vertCount; i++) {
        if (poly.neighborPolys[i] != UINT32_MAX) {
            neighbors.push_back(poly.neighborPolys[i]);
        }
    }

    return neighbors;
}

float NavMesh::DistanceToPolyEdge(uint32_t polyRef, const glm::vec3& point) const {
    if (!IsValidPoly(polyRef)) {
        return FLT_MAX;
    }

    const NavPoly& poly = m_polygons[polyRef];
    float minDist = FLT_MAX;

    for (uint8_t i = 0; i < poly.vertCount; i++) {
        const glm::vec3& v0 = m_vertices[poly.vertIndices[i]];
        const glm::vec3& v1 = m_vertices[poly.vertIndices[(i + 1) % poly.vertCount]];

        // Distance to line segment (2D)
        glm::vec2 p(point.x, point.z);
        glm::vec2 a(v0.x, v0.z);
        glm::vec2 b(v1.x, v1.z);

        glm::vec2 ab = b - a;
        float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        glm::vec2 closest = a + ab * t;
        float dist = glm::length(p - closest);

        minDist = std::min(minDist, dist);
    }

    return minDist;
}

void NavMesh::GetDebugGeometry(std::vector<glm::vec3>& outVertices,
                                std::vector<uint32_t>& outIndices,
                                std::vector<glm::vec4>& outColors) const {
    outVertices.clear();
    outIndices.clear();
    outColors.clear();

    // Color by area type
    auto getAreaColor = [](NavAreaType type) -> glm::vec4 {
        switch (type) {
            case NavAreaType::Walkable: return glm::vec4(0.2f, 0.8f, 0.2f, 0.5f);
            case NavAreaType::Road: return glm::vec4(0.6f, 0.6f, 0.6f, 0.5f);
            case NavAreaType::Grass: return glm::vec4(0.4f, 0.9f, 0.3f, 0.5f);
            case NavAreaType::Water: return glm::vec4(0.2f, 0.5f, 0.9f, 0.5f);
            case NavAreaType::Obstacle: return glm::vec4(0.9f, 0.2f, 0.2f, 0.5f);
            case NavAreaType::Jump: return glm::vec4(0.9f, 0.9f, 0.2f, 0.5f);
            default: return glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);
        }
    };

    for (uint32_t p = 0; p < m_polygons.size(); p++) {
        const NavPoly& poly = m_polygons[p];
        glm::vec4 color = getAreaColor(poly.areaType);

        uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

        // Add vertices with slight Y offset for visibility
        for (uint8_t i = 0; i < poly.vertCount; i++) {
            glm::vec3 v = m_vertices[poly.vertIndices[i]];
            v.y += 0.1f;
            outVertices.push_back(v);
            outColors.push_back(color);
        }

        // Triangulate polygon
        for (uint8_t i = 1; i < poly.vertCount - 1; i++) {
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + i);
            outIndices.push_back(baseIdx + i + 1);
        }
    }
}

bool NavMesh::Save(const std::string& filePath) const {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Write header
    uint32_t magic = 0x4E41564D;  // "NAVM"
    uint32_t version = 1;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write counts
    uint32_t vertCount = static_cast<uint32_t>(m_vertices.size());
    uint32_t polyCount = static_cast<uint32_t>(m_polygons.size());
    file.write(reinterpret_cast<const char*>(&vertCount), sizeof(vertCount));
    file.write(reinterpret_cast<const char*>(&polyCount), sizeof(polyCount));

    // Write bounds
    file.write(reinterpret_cast<const char*>(&m_boundsMin), sizeof(m_boundsMin));
    file.write(reinterpret_cast<const char*>(&m_boundsMax), sizeof(m_boundsMax));

    // Write vertices
    file.write(reinterpret_cast<const char*>(m_vertices.data()),
               m_vertices.size() * sizeof(glm::vec3));

    // Write polygons
    file.write(reinterpret_cast<const char*>(m_polygons.data()),
               m_polygons.size() * sizeof(NavPoly));

    return true;
}

bool NavMesh::Load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read header
    uint32_t magic, version;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != 0x4E41564D || version != 1) {
        return false;
    }

    // Read counts
    uint32_t vertCount, polyCount;
    file.read(reinterpret_cast<char*>(&vertCount), sizeof(vertCount));
    file.read(reinterpret_cast<char*>(&polyCount), sizeof(polyCount));

    // Read bounds
    file.read(reinterpret_cast<char*>(&m_boundsMin), sizeof(m_boundsMin));
    file.read(reinterpret_cast<char*>(&m_boundsMax), sizeof(m_boundsMax));

    // Read vertices
    m_vertices.resize(vertCount);
    file.read(reinterpret_cast<char*>(m_vertices.data()),
              m_vertices.size() * sizeof(glm::vec3));

    // Read polygons
    m_polygons.resize(polyCount);
    file.read(reinterpret_cast<char*>(m_polygons.data()),
              m_polygons.size() * sizeof(NavPoly));

    RebuildSpatialHash();
    return true;
}

} // namespace Cortex::AI
