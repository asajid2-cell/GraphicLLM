#pragma once

// LODGenerator.h
// Automatic LOD mesh generation using quadric error metrics.
// Reference: "Surface Simplification Using Quadric Error Metrics" - Garland & Heckbert

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <unordered_map>
#include <functional>

namespace Cortex {

// Forward declarations
struct Mesh;
struct Vertex;

namespace Utils {

// Quadric error matrix (4x4 symmetric, stored as 10 floats)
struct Quadric {
    // Q = [a b c d]
    //     [b e f g]
    //     [c f h i]
    //     [d g i j]
    float a = 0, b = 0, c = 0, d = 0;
    float e = 0, f = 0, g = 0;
    float h = 0, i = 0;
    float j = 0;

    Quadric() = default;
    Quadric(const glm::vec4& plane);  // From plane equation

    Quadric operator+(const Quadric& other) const;
    Quadric& operator+=(const Quadric& other);

    // Calculate error for vertex position
    float Evaluate(const glm::vec3& v) const;

    // Find optimal position (minimize error)
    bool FindOptimalPosition(glm::vec3& outPos) const;
};

// Edge for collapse candidates
struct EdgeCollapse {
    uint32_t v1, v2;          // Vertex indices
    float cost = FLT_MAX;     // Error cost of collapse
    glm::vec3 optimalPos;     // Optimal position after collapse
    bool isValid = true;

    bool operator<(const EdgeCollapse& other) const {
        return cost < other.cost;
    }
};

// Vertex attributes to preserve during simplification
struct VertexAttributes {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec2 uv = glm::vec2(0.0f);
    glm::vec4 tangent = glm::vec4(0.0f);
    glm::vec4 color = glm::vec4(1.0f);
    glm::ivec4 boneIndices = glm::ivec4(-1);
    glm::vec4 boneWeights = glm::vec4(0.0f);
};

// Triangle for internal processing
struct SimplifyTriangle {
    std::array<uint32_t, 3> indices;
    glm::vec3 normal;
    bool isRemoved = false;
};

// LOD level data
struct LODLevel {
    float screenPercentage = 1.0f;     // Screen size threshold (0-1)
    float distanceThreshold = 0.0f;    // Distance threshold (alternative to screen %)
    float reductionFactor = 1.0f;      // 0-1, target triangle ratio
    uint32_t triangleCount = 0;
    uint32_t vertexCount = 0;

    // Generated mesh data
    std::vector<VertexAttributes> vertices;
    std::vector<uint32_t> indices;
};

// LOD generation settings
struct LODGeneratorSettings {
    // Target LOD levels
    uint32_t numLODLevels = 4;

    // Reduction factors per level (0 = full, 1 = simplified)
    std::vector<float> reductionFactors = {0.0f, 0.5f, 0.75f, 0.9f};

    // Screen percentage thresholds (when to switch LODs)
    std::vector<float> screenPercentages = {0.5f, 0.25f, 0.1f, 0.01f};

    // Quality settings
    float maxError = FLT_MAX;          // Maximum allowed error
    bool preserveBoundaryEdges = true;  // Keep mesh boundaries
    bool preserveUVSeams = true;        // Keep UV discontinuities
    bool preserveNormalSeams = true;    // Keep hard edges
    float seamAngleThreshold = 30.0f;   // Degrees

    // Attribute weights (0 = ignore, 1 = normal importance)
    float positionWeight = 1.0f;
    float normalWeight = 0.5f;
    float uvWeight = 0.5f;
    float colorWeight = 0.1f;

    // Skinning preservation
    bool preserveSkinning = true;       // Don't collapse across bone boundaries
    float boneWeightThreshold = 0.1f;   // Minimum weight to consider

    // Performance
    uint32_t maxIterations = UINT32_MAX;
    bool useParallelProcessing = true;
};

// LOD chain result
struct LODChain {
    std::vector<LODLevel> levels;
    std::string sourceMeshName;
    uint32_t originalTriangleCount = 0;
    uint32_t originalVertexCount = 0;

    float GetReductionRatio(uint32_t level) const;
    float GetTotalReductionRatio() const;
};

// Progress callback
using LODProgressCallback = std::function<void(float progress, const std::string& status)>;

// LOD Generator class
class LODGenerator {
public:
    LODGenerator();
    ~LODGenerator() = default;

    // Set generation settings
    void SetSettings(const LODGeneratorSettings& settings) { m_settings = settings; }
    const LODGeneratorSettings& GetSettings() const { return m_settings; }

    // Set progress callback
    void SetProgressCallback(LODProgressCallback callback) { m_progressCallback = callback; }

    // Generate LOD chain from mesh data
    LODChain GenerateLODs(const std::vector<VertexAttributes>& vertices,
                           const std::vector<uint32_t>& indices);

    // Generate single LOD level at target reduction
    LODLevel GenerateLOD(const std::vector<VertexAttributes>& vertices,
                          const std::vector<uint32_t>& indices,
                          float reductionFactor);

    // Simplify mesh to target triangle count
    LODLevel SimplifyToTriangleCount(const std::vector<VertexAttributes>& vertices,
                                      const std::vector<uint32_t>& indices,
                                      uint32_t targetTriangles);

    // Simplify mesh to target vertex count
    LODLevel SimplifyToVertexCount(const std::vector<VertexAttributes>& vertices,
                                    const std::vector<uint32_t>& indices,
                                    uint32_t targetVertices);

    // Simplify mesh to error threshold
    LODLevel SimplifyToError(const std::vector<VertexAttributes>& vertices,
                              const std::vector<uint32_t>& indices,
                              float maxError);

    // Calculate recommended LOD distances based on mesh size
    std::vector<float> CalculateLODDistances(float meshRadius, uint32_t numLevels) const;

    // Validate LOD chain (check for issues)
    bool ValidateLODChain(const LODChain& chain, std::string& errorMsg) const;

private:
    // Internal simplification
    void InitializeQuadrics(const std::vector<VertexAttributes>& vertices,
                            const std::vector<SimplifyTriangle>& triangles,
                            std::vector<Quadric>& quadrics);

    void BuildEdgeList(const std::vector<VertexAttributes>& vertices,
                       const std::vector<SimplifyTriangle>& triangles,
                       std::vector<EdgeCollapse>& edges);

    float CalculateEdgeCost(uint32_t v1, uint32_t v2,
                            const std::vector<VertexAttributes>& vertices,
                            const std::vector<Quadric>& quadrics,
                            glm::vec3& outOptimalPos);

    void CollapseEdge(uint32_t edgeIndex,
                      std::vector<VertexAttributes>& vertices,
                      std::vector<SimplifyTriangle>& triangles,
                      std::vector<Quadric>& quadrics,
                      std::vector<EdgeCollapse>& edges,
                      std::vector<bool>& vertexRemoved);

    bool IsBoundaryEdge(uint32_t v1, uint32_t v2,
                        const std::vector<SimplifyTriangle>& triangles) const;

    bool IsSeamEdge(uint32_t v1, uint32_t v2,
                    const std::vector<VertexAttributes>& vertices) const;

    void CompactMesh(const std::vector<VertexAttributes>& vertices,
                     const std::vector<SimplifyTriangle>& triangles,
                     const std::vector<bool>& vertexRemoved,
                     LODLevel& output);

    // Attribute interpolation
    VertexAttributes InterpolateAttributes(const VertexAttributes& a,
                                            const VertexAttributes& b,
                                            float t) const;

    // Edge hash for lookup
    uint64_t EdgeHash(uint32_t v1, uint32_t v2) const {
        if (v1 > v2) std::swap(v1, v2);
        return (static_cast<uint64_t>(v1) << 32) | static_cast<uint64_t>(v2);
    }

    LODGeneratorSettings m_settings;
    LODProgressCallback m_progressCallback;
};

// Utility functions
namespace LODUtils {

// Calculate mesh bounding sphere radius
float CalculateMeshRadius(const std::vector<VertexAttributes>& vertices);

// Calculate mesh surface area
float CalculateMeshArea(const std::vector<VertexAttributes>& vertices,
                        const std::vector<uint32_t>& indices);

// Weld vertices within threshold
void WeldVertices(std::vector<VertexAttributes>& vertices,
                  std::vector<uint32_t>& indices,
                  float positionThreshold = 0.0001f);

// Remove degenerate triangles
void RemoveDegenerateTriangles(std::vector<uint32_t>& indices);

// Calculate screen percentage from distance and size
float ScreenPercentageFromDistance(float distance, float objectRadius, float fovY, float screenHeight);

// Calculate distance from screen percentage
float DistanceFromScreenPercentage(float screenPercentage, float objectRadius, float fovY, float screenHeight);

} // namespace LODUtils

} // namespace Utils
} // namespace Cortex
