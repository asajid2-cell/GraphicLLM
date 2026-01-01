#pragma once

// PoissonDisk.h
// Implementation of Bridson's Fast Poisson Disk Sampling Algorithm
// Reference: "Fast Poisson Disk Sampling in Arbitrary Dimensions" - Robert Bridson, SIGGRAPH 2007
//
// Generates blue noise distributed points with minimum distance constraint.
// O(n) time complexity where n is the number of generated points.

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <random>
#include <cstdint>

namespace Cortex::Utils {

// Result of Poisson disk sampling
struct PoissonSample {
    glm::vec2 position;     // 2D position in sample space
    float density;          // Local density at this point (0-1)
    uint32_t cellIndex;     // Grid cell index (for spatial queries)
    uint32_t attempt;       // Which attempt generated this point (for debugging)
};

// Parameters for Poisson disk sampling
struct PoissonDiskParams {
    float minDistance = 1.0f;           // Minimum distance between points
    int maxAttempts = 30;               // Attempts per active point (k in Bridson's paper)
    uint32_t seed = 0;                  // Random seed (0 = use time)
    bool variableDensity = false;       // Enable variable density sampling

    // Optional: Density function for variable-radius sampling
    // Returns a density multiplier (0-1) where lower = sparser, higher = denser
    std::function<float(float x, float y)> densityFunc = nullptr;

    // Bounds
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 100.0f;
    float maxY = 100.0f;

    // Optional: Point rejection function
    // Returns true if the point should be rejected (e.g., outside valid terrain)
    std::function<bool(float x, float y)> rejectFunc = nullptr;
};

// Statistics for Poisson disk sampling
struct PoissonStats {
    uint32_t totalPoints = 0;           // Number of generated points
    uint32_t rejectedPoints = 0;        // Points rejected by rejectFunc
    uint32_t distanceRejected = 0;      // Points too close to existing points
    float averageDensity = 0.0f;        // Average density across samples
    float executionTimeMs = 0.0f;       // Time taken for sampling
    uint32_t gridCells = 0;             // Number of grid cells used
};

class PoissonDiskSampler {
public:
    PoissonDiskSampler();
    ~PoissonDiskSampler() = default;

    // Main sampling function
    // Returns vector of 2D sample positions
    std::vector<glm::vec2> Sample(const PoissonDiskParams& params);

    // Extended sampling with full sample data
    std::vector<PoissonSample> SampleExtended(const PoissonDiskParams& params);

    // Sample within a circular region
    std::vector<glm::vec2> SampleCircle(const glm::vec2& center, float radius,
                                         float minDistance, int maxAttempts = 30,
                                         uint32_t seed = 0);

    // Sample within a polygon (convex or concave)
    std::vector<glm::vec2> SamplePolygon(const std::vector<glm::vec2>& polygon,
                                          float minDistance, int maxAttempts = 30,
                                          uint32_t seed = 0);

    // Incremental sampling - add points to existing set
    std::vector<glm::vec2> SampleIncremental(const std::vector<glm::vec2>& existing,
                                              const PoissonDiskParams& params);

    // Get statistics from last sampling operation
    const PoissonStats& GetStats() const { return m_stats; }

    // Utility: Check if a point is valid (respects minimum distance)
    bool IsPointValid(const glm::vec2& point, float minDistance,
                      const std::vector<glm::vec2>& existingPoints) const;

    // Utility: Get effective minimum distance at a position (for variable density)
    float GetEffectiveMinDistance(float x, float y, float baseMinDistance,
                                   const std::function<float(float, float)>& densityFunc) const;

private:
    // Background grid for spatial acceleration
    struct Grid {
        std::vector<int> cells;         // -1 = empty, otherwise index into points
        int width = 0;
        int height = 0;
        float cellSize = 0.0f;
        float invCellSize = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;

        void Initialize(float minX, float minY, float maxX, float maxY, float minDist);
        int GetCellIndex(float x, float y) const;
        void Insert(float x, float y, int pointIndex);
        bool CheckNeighbors(float x, float y, float minDistSq,
                           const std::vector<glm::vec2>& points) const;
    };

    Grid m_grid;
    PoissonStats m_stats;
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_uniformDist;

    // Generate a random point in annulus around center
    glm::vec2 GeneratePointInAnnulus(const glm::vec2& center, float minRadius, float maxRadius);

    // Core Bridson algorithm implementation
    std::vector<glm::vec2> BridsonSample(const PoissonDiskParams& params);

    // Variable density Bridson implementation
    std::vector<glm::vec2> VariableDensitySample(const PoissonDiskParams& params);

    // Check if point is inside polygon (ray casting)
    bool PointInPolygon(const glm::vec2& point, const std::vector<glm::vec2>& polygon) const;
};

// Convenience functions for common use cases

// Generate uniformly distributed points in a rectangle
std::vector<glm::vec2> GeneratePoissonPoints(float width, float height,
                                              float minDistance,
                                              uint32_t seed = 0);

// Generate points with density varying by position
std::vector<glm::vec2> GenerateVariableDensityPoints(
    float width, float height,
    float minDistance, float maxDistance,
    std::function<float(float, float)> densityFunc,
    uint32_t seed = 0);

// Generate points for terrain placement (respects slope and height)
std::vector<glm::vec2> GenerateTerrainPlacementPoints(
    float minX, float minZ, float maxX, float maxZ,
    float minDistance,
    std::function<bool(float x, float z)> isValidPosition,
    std::function<float(float x, float z)> getDensity,
    uint32_t seed = 0);

} // namespace Cortex::Utils
