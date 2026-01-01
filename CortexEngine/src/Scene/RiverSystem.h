#pragma once

// RiverSystem.h
// River and stream system using spline-based water volumes.
// Supports procedural generation, terrain carving, and flow animation.

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace Cortex::Scene {

// Forward declarations
struct MeshData;

// River spline control point
struct RiverSplinePoint {
    glm::vec3 position;          // World position of this control point
    float width = 5.0f;          // River width at this point
    float depth = 1.0f;          // River depth at this point
    float flowSpeed = 1.0f;      // Flow velocity multiplier
    float turbulence = 0.0f;     // Local turbulence (0-1)
    glm::vec3 normal;            // Up vector at this point (for banking)

    // Tangent vectors (computed from spline)
    glm::vec3 tangent;           // Flow direction
    glm::vec3 binormal;          // Perpendicular to flow (bank direction)
};

// River style/appearance settings
struct RiverStyle {
    std::string name = "default";

    // Water appearance
    glm::vec3 shallowColor = glm::vec3(0.4f, 0.6f, 0.7f);
    glm::vec3 deepColor = glm::vec3(0.1f, 0.2f, 0.4f);
    float transparency = 0.6f;
    float refractionStrength = 0.1f;
    float reflectionStrength = 0.5f;

    // Foam/rapids
    float foamThreshold = 0.5f;      // Turbulence level where foam appears
    glm::vec3 foamColor = glm::vec3(0.9f, 0.95f, 1.0f);
    float foamDensity = 1.0f;

    // Flow animation
    float flowUVScale = 0.1f;        // UV tiling for flow texture
    float waveAmplitude = 0.05f;     // Vertical wave amplitude
    float waveFrequency = 2.0f;      // Wave frequency

    // Ripple/disturbance
    float rippleScale = 10.0f;
    float rippleSpeed = 1.0f;
};

// Complete river definition
struct RiverSpline {
    std::string name;
    std::vector<RiverSplinePoint> controlPoints;
    RiverStyle style;

    // Generation parameters
    int segmentsPerSpan = 8;         // Interpolation segments between control points
    int widthSegments = 4;           // Cross-section resolution
    bool generateBanks = true;       // Generate bank geometry
    float bankWidth = 2.0f;          // Width of bank mesh on each side
    float bankSlope = 0.5f;          // Bank slope angle (0-1)

    // Terrain interaction
    bool carvesTerrain = true;       // Whether this river carves into terrain
    float carveDepth = 1.0f;         // How deep to carve
    float carveBlendRadius = 5.0f;   // Smooth blend radius for carving

    // Runtime state
    bool isDirty = true;             // Needs mesh regeneration
    uint32_t meshId = 0;             // ID of generated mesh

    // Spline interpolation helpers
    glm::vec3 EvaluatePosition(float t) const;
    float EvaluateWidth(float t) const;
    float EvaluateDepth(float t) const;
    float EvaluateFlowSpeed(float t) const;
    glm::vec3 EvaluateTangent(float t) const;

    // Get total spline length (approximate)
    float GetTotalLength() const;

    // Convert parameter t [0,1] to arc-length parameterization
    float TToArcLength(float t) const;
    float ArcLengthToT(float arcLength) const;
};

// Lake/pond volume definition
struct LakeVolume {
    std::string name;

    // Boundary polygon (world XZ coordinates, Y is water level)
    std::vector<glm::vec2> boundaryPoints;
    float waterLevel = 0.0f;         // Y coordinate of water surface
    float depth = 5.0f;              // Maximum depth

    // Style
    RiverStyle style;                // Reuse river style for consistency

    // Wave parameters (lakes have gentler waves than rivers)
    float waveAmplitude = 0.02f;
    float waveSpeed = 0.5f;
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);

    // Shore interaction
    float shoreBlendDistance = 3.0f; // Distance for shore fade
    bool generateShoreline = true;

    // Terrain interaction
    bool carvesTerrain = true;
    float carveBlendRadius = 10.0f;

    // Runtime state
    bool isDirty = true;
    uint32_t meshId = 0;
    glm::vec3 boundsMin;
    glm::vec3 boundsMax;

    // Check if a point is inside the lake boundary
    bool ContainsPoint(float x, float z) const;

    // Get water depth at a point (0 if outside)
    float GetDepthAt(float x, float z) const;

    // Compute bounds from boundary points
    void ComputeBounds();
};

// Waterfall segment (connecting rivers at different elevations)
struct WaterfallSegment {
    glm::vec3 topPosition;
    glm::vec3 bottomPosition;
    float width = 3.0f;
    float flowRate = 1.0f;           // Affects particle density
    float mistRadius = 5.0f;         // Radius of mist effect at base

    // Particle generation
    uint32_t particleCount = 100;
    float particleSize = 0.1f;
};

// River network node (for confluences/splits)
struct RiverNode {
    glm::vec3 position;
    std::vector<uint32_t> connectedRivers;  // Indices of connected rivers
    bool isSource = false;           // Spring/source
    bool isSink = false;             // Lake/ocean connection
    float flowVolume = 1.0f;         // Total flow through this node
};

// GPU constant buffer for river/water rendering
struct alignas(16) RiverConstantsCB {
    glm::mat4 viewProj;
    glm::vec4 cameraPosition;        // xyz = position, w = time
    glm::vec4 shallowColor;          // rgb = color, a = transparency
    glm::vec4 deepColor;             // rgb = color, a = refraction strength
    glm::vec4 foamParams;            // x = threshold, y = density, z = speed, w = unused
    glm::vec4 waveParams;            // x = amplitude, y = frequency, z = speed, w = UV scale
    glm::vec4 flowDirection;         // xy = primary dir, zw = secondary dir
    glm::vec4 rippleParams;          // x = scale, y = speed, z = strength, w = unused
};

// River vertex with flow data
struct RiverVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;              // UV for texturing
    glm::vec2 flowUV;                // UV for flow animation
    float flowSpeed;                 // Local flow velocity
    float depth;                     // Water depth at this vertex
    float distanceFromBank;          // 0 at bank, 1 at center
    float turbulence;                // Local turbulence factor
};

// River system manager
class RiverSystem {
public:
    RiverSystem();
    ~RiverSystem();

    // River management
    uint32_t AddRiver(const RiverSpline& river);
    void RemoveRiver(uint32_t riverId);
    RiverSpline* GetRiver(uint32_t riverId);
    const RiverSpline* GetRiver(uint32_t riverId) const;

    // Lake management
    uint32_t AddLake(const LakeVolume& lake);
    void RemoveLake(uint32_t lakeId);
    LakeVolume* GetLake(uint32_t lakeId);
    const LakeVolume* GetLake(uint32_t lakeId) const;

    // Waterfall management
    uint32_t AddWaterfall(const WaterfallSegment& waterfall);
    void RemoveWaterfall(uint32_t waterfallId);

    // Mesh generation
    std::shared_ptr<MeshData> GenerateRiverMesh(const RiverSpline& river);
    std::shared_ptr<MeshData> GenerateLakeMesh(const LakeVolume& lake);
    void RegenerateDirtyMeshes();

    // Terrain interaction
    // Returns height offset for terrain at given position
    // Negative values indicate carving (lowering terrain)
    float GetTerrainCarveOffset(float x, float z) const;

    // Check if a point is underwater
    bool IsPointUnderwater(float x, float y, float z) const;

    // Get water surface height at a position
    float GetWaterSurfaceHeight(float x, float z) const;

    // Get flow direction at a position (for floating objects)
    glm::vec3 GetFlowDirectionAt(float x, float y, float z) const;

    // Get flow speed at a position
    float GetFlowSpeedAt(float x, float y, float z) const;

    // Update (animation, particles)
    void Update(float deltaTime);

    // Serialization
    bool LoadFromJSON(const std::string& path);
    bool SaveToJSON(const std::string& path) const;

    // Get all rivers/lakes for rendering
    const std::vector<RiverSpline>& GetRivers() const { return m_rivers; }
    const std::vector<LakeVolume>& GetLakes() const { return m_lakes; }
    const std::vector<WaterfallSegment>& GetWaterfalls() const { return m_waterfalls; }

    // Statistics
    uint32_t GetRiverCount() const { return static_cast<uint32_t>(m_rivers.size()); }
    uint32_t GetLakeCount() const { return static_cast<uint32_t>(m_lakes.size()); }
    float GetTotalRiverLength() const;
    float GetTotalLakeArea() const;

private:
    std::vector<RiverSpline> m_rivers;
    std::vector<LakeVolume> m_lakes;
    std::vector<WaterfallSegment> m_waterfalls;
    std::vector<RiverNode> m_riverNodes;

    float m_time = 0.0f;

    // Mesh generation helpers
    void GenerateRiverCrossSection(const RiverSpline& river, float t,
                                    std::vector<RiverVertex>& outVertices);
    void GenerateBankGeometry(const RiverSpline& river,
                              std::vector<RiverVertex>& outVertices,
                              std::vector<uint32_t>& outIndices);

    // Catmull-Rom spline interpolation
    static glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                 const glm::vec3& p2, const glm::vec3& p3, float t);
    static float CatmullRomScalar(float p0, float p1, float p2, float p3, float t);

    // Lake mesh helpers
    void TriangulateLakeBoundary(const LakeVolume& lake,
                                  std::vector<RiverVertex>& outVertices,
                                  std::vector<uint32_t>& outIndices);
};

// Predefined river styles for different biomes
namespace RiverStyles {
    inline RiverStyle Mountain() {
        RiverStyle style;
        style.name = "mountain";
        style.shallowColor = glm::vec3(0.5f, 0.7f, 0.8f);
        style.deepColor = glm::vec3(0.2f, 0.3f, 0.5f);
        style.transparency = 0.7f;
        style.foamThreshold = 0.3f;
        style.foamDensity = 1.5f;
        style.waveAmplitude = 0.1f;
        return style;
    }

    inline RiverStyle Forest() {
        RiverStyle style;
        style.name = "forest";
        style.shallowColor = glm::vec3(0.3f, 0.5f, 0.4f);
        style.deepColor = glm::vec3(0.1f, 0.2f, 0.2f);
        style.transparency = 0.5f;
        style.foamThreshold = 0.6f;
        style.foamDensity = 0.5f;
        style.waveAmplitude = 0.03f;
        return style;
    }

    inline RiverStyle Swamp() {
        RiverStyle style;
        style.name = "swamp";
        style.shallowColor = glm::vec3(0.3f, 0.35f, 0.2f);
        style.deepColor = glm::vec3(0.15f, 0.2f, 0.1f);
        style.transparency = 0.3f;
        style.refractionStrength = 0.02f;
        style.foamThreshold = 0.9f;  // Almost no foam
        style.waveAmplitude = 0.01f;
        return style;
    }

    inline RiverStyle Desert() {
        RiverStyle style;
        style.name = "desert";
        style.shallowColor = glm::vec3(0.5f, 0.55f, 0.5f);
        style.deepColor = glm::vec3(0.2f, 0.25f, 0.2f);
        style.transparency = 0.4f;
        style.foamThreshold = 0.7f;
        style.waveAmplitude = 0.02f;
        return style;
    }
}

} // namespace Cortex::Scene
