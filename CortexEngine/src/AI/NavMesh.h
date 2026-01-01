#pragma once

// NavMesh.h
// Navigation mesh data structures and query interface.
// Used for pathfinding and AI movement.
// Reference: "Recast/Detour Navigation Toolkit" - Mononen

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <string>

namespace Cortex::AI {

// Forward declarations
class NavMeshQuery;
class NavMeshBuilder;

// Maximum vertices per polygon
constexpr uint32_t MAX_VERTS_PER_POLY = 6;

// Navigation area types
enum class NavAreaType : uint8_t {
    Walkable = 0,       // Normal walkable ground
    Road = 1,           // Roads/paths (preferred)
    Grass = 2,          // Grass/terrain (slower)
    Water = 3,          // Shallow water (slow, splashes)
    Obstacle = 4,       // Temporary obstacle
    Jump = 5,           // Jump connection
    Ladder = 6,         // Ladder connection
    Door = 7,           // Door (may be locked)
    NotWalkable = 255   // Blocked
};

// Navigation area flags
enum class NavAreaFlags : uint16_t {
    None = 0,
    Walk = 1 << 0,
    Swim = 1 << 1,
    Jump = 1 << 2,
    Climb = 1 << 3,
    Fly = 1 << 4,
    All = Walk | Swim | Jump | Climb | Fly
};

inline NavAreaFlags operator|(NavAreaFlags a, NavAreaFlags b) {
    return static_cast<NavAreaFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline NavAreaFlags operator&(NavAreaFlags a, NavAreaFlags b) {
    return static_cast<NavAreaFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

// Navigation polygon
struct NavPoly {
    uint32_t vertIndices[MAX_VERTS_PER_POLY];   // Indices into vertex array
    uint32_t neighborPolys[MAX_VERTS_PER_POLY]; // Adjacent polygon indices (UINT32_MAX = no neighbor)
    uint8_t vertCount;                           // Number of vertices in polygon
    NavAreaType areaType;
    NavAreaFlags flags;
    float cost;                                  // Traversal cost multiplier

    NavPoly() {
        for (int i = 0; i < MAX_VERTS_PER_POLY; i++) {
            vertIndices[i] = UINT32_MAX;
            neighborPolys[i] = UINT32_MAX;
        }
        vertCount = 0;
        areaType = NavAreaType::Walkable;
        flags = NavAreaFlags::Walk;
        cost = 1.0f;
    }
};

// Off-mesh connection (jumps, ladders, teleports)
struct OffMeshConnection {
    glm::vec3 startPos;
    glm::vec3 endPos;
    float radius;
    uint32_t startPolyRef;
    uint32_t endPolyRef;
    NavAreaType areaType;
    NavAreaFlags flags;
    bool bidirectional;
    float cost;
    std::string tag;    // Optional identifier ("jump_high", "ladder_01", etc.)
};

// NavMesh tile for streaming large worlds
struct NavMeshTile {
    int32_t tileX;
    int32_t tileZ;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;

    std::vector<glm::vec3> vertices;
    std::vector<NavPoly> polygons;
    std::vector<OffMeshConnection> offMeshConnections;

    // Connectivity to adjacent tiles
    std::vector<uint32_t> borderPolygons;   // Polygons on tile edges
};

// NavMesh build settings
struct NavMeshBuildSettings {
    // Agent properties
    float agentRadius = 0.5f;           // Agent radius
    float agentHeight = 2.0f;           // Agent height
    float agentMaxClimb = 0.4f;         // Max step height
    float agentMaxSlope = 45.0f;        // Max walkable slope (degrees)

    // Voxelization
    float cellSize = 0.3f;              // XZ cell size
    float cellHeight = 0.2f;            // Y cell height

    // Region generation
    uint32_t minRegionArea = 8;         // Min cells for region
    uint32_t mergeRegionArea = 20;      // Merge regions smaller than this

    // Polygon generation
    float edgeMaxLength = 12.0f;        // Max edge length
    float edgeMaxError = 1.3f;          // Max edge deviation
    uint32_t vertsPerPoly = 6;          // Max verts per polygon

    // Detail mesh
    float detailSampleDist = 6.0f;      // Detail sampling distance
    float detailSampleMaxError = 1.0f;  // Detail max error

    // Tiling
    bool useTiles = false;
    float tileSize = 48.0f;             // Tile size in world units
};

// Point on navmesh
struct NavMeshPoint {
    glm::vec3 position;
    uint32_t polyRef;       // Polygon reference
    bool valid;

    NavMeshPoint() : position(0.0f), polyRef(UINT32_MAX), valid(false) {}
    NavMeshPoint(const glm::vec3& pos, uint32_t poly)
        : position(pos), polyRef(poly), valid(poly != UINT32_MAX) {}
};

// Path node for A* search
struct PathNode {
    uint32_t polyRef;
    float gCost;            // Cost from start
    float hCost;            // Heuristic to goal
    float fCost() const { return gCost + hCost; }
    uint32_t parentRef;
    glm::vec3 position;
};

// Raycast hit result
struct NavMeshRaycastResult {
    bool hit;
    glm::vec3 hitPoint;
    glm::vec3 hitNormal;
    uint32_t hitPolyRef;
    float hitDistance;
    NavAreaType hitAreaType;
};

// Navigation mesh
class NavMesh {
public:
    NavMesh() = default;
    ~NavMesh() = default;

    // Build from geometry
    bool Build(const std::vector<glm::vec3>& vertices,
               const std::vector<uint32_t>& indices,
               const NavMeshBuildSettings& settings);

    // Build from heightfield
    bool BuildFromHeightfield(const float* heightData, uint32_t width, uint32_t height,
                               const glm::vec3& origin, float cellSize,
                               const NavMeshBuildSettings& settings);

    // Tile management (for large worlds)
    bool AddTile(const NavMeshTile& tile);
    bool RemoveTile(int32_t tileX, int32_t tileZ);
    NavMeshTile* GetTile(int32_t tileX, int32_t tileZ);
    const NavMeshTile* GetTile(int32_t tileX, int32_t tileZ) const;

    // Off-mesh connections
    uint32_t AddOffMeshConnection(const OffMeshConnection& connection);
    void RemoveOffMeshConnection(uint32_t connectionId);

    // Query interface
    NavMeshPoint FindNearestPoint(const glm::vec3& position, float searchRadius) const;
    NavMeshPoint FindRandomPoint() const;
    NavMeshPoint FindRandomPointInRadius(const glm::vec3& center, float radius) const;

    // Polygon queries
    uint32_t GetPolyAt(const glm::vec3& position) const;
    bool IsValidPoly(uint32_t polyRef) const;
    glm::vec3 GetPolyCenter(uint32_t polyRef) const;
    NavAreaType GetPolyAreaType(uint32_t polyRef) const;
    float GetPolyCost(uint32_t polyRef) const;

    // Height sampling
    float GetHeight(const glm::vec3& position, float searchRange = 2.0f) const;

    // Raycast
    NavMeshRaycastResult Raycast(const glm::vec3& start, const glm::vec3& end) const;

    // Bounds
    glm::vec3 GetBoundsMin() const { return m_boundsMin; }
    glm::vec3 GetBoundsMax() const { return m_boundsMax; }

    // Statistics
    uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }
    uint32_t GetPolygonCount() const { return static_cast<uint32_t>(m_polygons.size()); }
    uint32_t GetTileCount() const { return static_cast<uint32_t>(m_tiles.size()); }

    // Serialization
    bool Save(const std::string& filePath) const;
    bool Load(const std::string& filePath);

    // Debug visualization
    void GetDebugGeometry(std::vector<glm::vec3>& outVertices,
                          std::vector<uint32_t>& outIndices,
                          std::vector<glm::vec4>& outColors) const;

    // Access raw data
    const std::vector<glm::vec3>& GetVertices() const { return m_vertices; }
    const std::vector<NavPoly>& GetPolygons() const { return m_polygons; }

private:
    friend class NavMeshQuery;
    friend class NavMeshBuilder;

    // Polygon containment test
    bool IsPointInPoly(uint32_t polyRef, const glm::vec3& point) const;

    // Get neighbors
    std::vector<uint32_t> GetPolyNeighbors(uint32_t polyRef) const;

    // Distance to polygon edge
    float DistanceToPolyEdge(uint32_t polyRef, const glm::vec3& point) const;

    std::vector<glm::vec3> m_vertices;
    std::vector<NavPoly> m_polygons;
    std::vector<OffMeshConnection> m_offMeshConnections;
    std::vector<NavMeshTile> m_tiles;

    glm::vec3 m_boundsMin = glm::vec3(0.0f);
    glm::vec3 m_boundsMax = glm::vec3(0.0f);

    NavMeshBuildSettings m_settings;

    // Spatial hash for fast polygon lookup
    struct SpatialCell {
        std::vector<uint32_t> polyRefs;
    };
    std::unordered_map<uint64_t, SpatialCell> m_spatialHash;
    float m_spatialCellSize = 4.0f;

    uint64_t GetSpatialKey(const glm::vec3& pos) const;
    void RebuildSpatialHash();
};

} // namespace Cortex::AI
