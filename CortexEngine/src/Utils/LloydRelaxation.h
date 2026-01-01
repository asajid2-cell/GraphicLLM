#pragma once

// LloydRelaxation.h
// Lloyd's Algorithm for Voronoi-Based Point Relaxation
// Reference: Lloyd, S. "Least Squares Quantization in PCM" IEEE Trans. Info. Theory 1982
//
// Iteratively moves points to Voronoi cell centroids for uniform distribution.
// Produces Centroidal Voronoi Tessellation (CVT) with optimal coverage.

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cstdint>

namespace Cortex::Utils {

// A Voronoi cell with its generator point and computed centroid
struct VoronoiCell {
    glm::vec2 generator;        // Original point (site)
    glm::vec2 centroid;         // Computed centroid of the cell
    float area;                 // Cell area (for weighted distributions)
    std::vector<glm::vec2> vertices;  // Cell boundary vertices (optional)
    std::vector<uint32_t> neighbors;  // Indices of neighboring cells
};

// Parameters for Lloyd relaxation
struct LloydParams {
    int maxIterations = 50;             // Maximum relaxation iterations
    float convergenceThreshold = 0.001f; // Stop when max movement < this
    float dampingFactor = 1.0f;         // Movement dampening (0-1, 1 = full move)
    bool computeCellGeometry = false;   // Whether to compute cell vertices
    bool wrapBounds = false;            // Toroidal wrapping at boundaries

    // Density weighting function (optional)
    // Returns density at position (higher = attracts more points)
    std::function<float(float x, float y)> densityFunc = nullptr;

    // Bounds
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 100.0f;
    float maxY = 100.0f;
};

// Statistics for Lloyd relaxation
struct LloydStats {
    uint32_t iterations = 0;            // Iterations performed
    float maxMovement = 0.0f;           // Maximum point movement in last iteration
    float averageMovement = 0.0f;       // Average movement in last iteration
    float executionTimeMs = 0.0f;       // Total time
    bool converged = false;             // Whether convergence was reached
};

class LloydRelaxation {
public:
    LloydRelaxation();
    ~LloydRelaxation() = default;

    // Main relaxation function
    // Modifies points in-place and returns final positions
    std::vector<glm::vec2> Relax(std::vector<glm::vec2>& points, const LloydParams& params);

    // Relax with full Voronoi cell data
    std::vector<VoronoiCell> RelaxWithCells(std::vector<glm::vec2>& points, const LloydParams& params);

    // Single relaxation iteration
    // Returns maximum movement distance
    float RelaxStep(std::vector<glm::vec2>& points, const LloydParams& params);

    // Compute Voronoi diagram for current points
    std::vector<VoronoiCell> ComputeVoronoi(const std::vector<glm::vec2>& points,
                                             const LloydParams& params);

    // Get cell centroids (weighted by density if provided)
    std::vector<glm::vec2> ComputeCentroids(const std::vector<glm::vec2>& points,
                                             const LloydParams& params);

    // Get statistics from last operation
    const LloydStats& GetStats() const { return m_stats; }

    // Utility: Find nearest point (Voronoi generator)
    int FindNearestPoint(const glm::vec2& position, const std::vector<glm::vec2>& points) const;

    // Utility: Compute distance to nearest boundary
    float DistanceToBoundary(const glm::vec2& position, const LloydParams& params) const;

    // Utility: Check if point is on boundary
    bool IsOnBoundary(const glm::vec2& position, const LloydParams& params, float threshold = 0.01f) const;

private:
    LloydStats m_stats;

    // Grid-accelerated nearest neighbor search
    struct AccelerationGrid {
        std::vector<std::vector<int>> cells;
        int width = 0;
        int height = 0;
        float cellSize = 0.0f;
        float minX = 0.0f;
        float minY = 0.0f;

        void Build(const std::vector<glm::vec2>& points, const LloydParams& params);
        int FindNearest(const glm::vec2& pos, const std::vector<glm::vec2>& points) const;
    };

    AccelerationGrid m_grid;

    // Compute centroid via Monte Carlo sampling
    glm::vec2 ComputeCentroidMonteCarlo(int pointIndex,
                                         const std::vector<glm::vec2>& points,
                                         const LloydParams& params,
                                         int numSamples = 256);

    // Compute centroid via analytical Voronoi cell integration
    glm::vec2 ComputeCentroidAnalytical(int pointIndex,
                                         const std::vector<glm::vec2>& points,
                                         const VoronoiCell& cell,
                                         const LloydParams& params);

    // Clip Voronoi cell to bounding box
    std::vector<glm::vec2> ClipCellToBounds(const std::vector<glm::vec2>& cellVertices,
                                             const LloydParams& params);

    // Compute polygon centroid
    glm::vec2 PolygonCentroid(const std::vector<glm::vec2>& vertices) const;

    // Compute polygon area
    float PolygonArea(const std::vector<glm::vec2>& vertices) const;

    // Fortune's algorithm for Voronoi diagram (simplified)
    void ComputeVoronoiDiagram(const std::vector<glm::vec2>& points,
                                const LloydParams& params,
                                std::vector<VoronoiCell>& cells);
};

// Convenience functions

// Relax points to uniform distribution
std::vector<glm::vec2> RelaxPoints(std::vector<glm::vec2> points,
                                    float minX, float minY, float maxX, float maxY,
                                    int iterations = 50);

// Relax with density weighting
std::vector<glm::vec2> RelaxPointsWeighted(std::vector<glm::vec2> points,
                                            float minX, float minY, float maxX, float maxY,
                                            std::function<float(float, float)> densityFunc,
                                            int iterations = 50);

// Generate relaxed points from initial random distribution
std::vector<glm::vec2> GenerateRelaxedPoints(int numPoints,
                                              float minX, float minY, float maxX, float maxY,
                                              int iterations = 50, uint32_t seed = 0);

// Apply relaxation to existing Poisson disk sample for even improvement
std::vector<glm::vec2> RefineWithRelaxation(const std::vector<glm::vec2>& poissonPoints,
                                             float minX, float minY, float maxX, float maxY,
                                             int iterations = 10);

} // namespace Cortex::Utils
