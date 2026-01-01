// PoissonDisk.cpp
// Implementation of Bridson's Fast Poisson Disk Sampling Algorithm
// Reference: "Fast Poisson Disk Sampling in Arbitrary Dimensions" - Robert Bridson, SIGGRAPH 2007

#include "PoissonDisk.h"
#include <chrono>
#include <algorithm>
#include <cmath>

namespace Cortex::Utils {

// ============================================================================
// Grid Implementation
// ============================================================================

void PoissonDiskSampler::Grid::Initialize(float minX, float minY, float maxX, float maxY, float minDist) {
    // Cell size is minDist / sqrt(2) to ensure at most one sample per cell
    // This guarantees O(1) neighbor lookups
    cellSize = minDist / std::sqrt(2.0f);
    invCellSize = 1.0f / cellSize;
    offsetX = minX;
    offsetY = minY;

    width = static_cast<int>(std::ceil((maxX - minX) * invCellSize)) + 1;
    height = static_cast<int>(std::ceil((maxY - minY) * invCellSize)) + 1;

    cells.assign(static_cast<size_t>(width * height), -1);
}

int PoissonDiskSampler::Grid::GetCellIndex(float x, float y) const {
    int cx = static_cast<int>((x - offsetX) * invCellSize);
    int cy = static_cast<int>((y - offsetY) * invCellSize);

    // Clamp to grid bounds
    cx = std::max(0, std::min(width - 1, cx));
    cy = std::max(0, std::min(height - 1, cy));

    return cy * width + cx;
}

void PoissonDiskSampler::Grid::Insert(float x, float y, int pointIndex) {
    int idx = GetCellIndex(x, y);
    if (idx >= 0 && idx < static_cast<int>(cells.size())) {
        cells[idx] = pointIndex;
    }
}

bool PoissonDiskSampler::Grid::CheckNeighbors(float x, float y, float minDistSq,
                                               const std::vector<glm::vec2>& points) const {
    int cx = static_cast<int>((x - offsetX) * invCellSize);
    int cy = static_cast<int>((y - offsetY) * invCellSize);

    // Check 5x5 neighborhood (sufficient for our cell size choice)
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int nx = cx + dx;
            int ny = cy + dy;

            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                continue;
            }

            int cellIdx = ny * width + nx;
            int pointIdx = cells[cellIdx];

            if (pointIdx >= 0) {
                const glm::vec2& other = points[pointIdx];
                float distSq = (x - other.x) * (x - other.x) + (y - other.y) * (y - other.y);
                if (distSq < minDistSq) {
                    return false;  // Too close to existing point
                }
            }
        }
    }

    return true;  // No conflicts found
}

// ============================================================================
// PoissonDiskSampler Implementation
// ============================================================================

PoissonDiskSampler::PoissonDiskSampler()
    : m_uniformDist(0.0f, 1.0f) {
}

std::vector<glm::vec2> PoissonDiskSampler::Sample(const PoissonDiskParams& params) {
    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<glm::vec2> result;

    if (params.variableDensity && params.densityFunc) {
        result = VariableDensitySample(params);
    } else {
        result = BridsonSample(params);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.executionTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    m_stats.totalPoints = static_cast<uint32_t>(result.size());
    m_stats.gridCells = static_cast<uint32_t>(m_grid.cells.size());

    return result;
}

std::vector<PoissonSample> PoissonDiskSampler::SampleExtended(const PoissonDiskParams& params) {
    auto points = Sample(params);

    std::vector<PoissonSample> result;
    result.reserve(points.size());

    for (size_t i = 0; i < points.size(); ++i) {
        PoissonSample sample;
        sample.position = points[i];
        sample.cellIndex = static_cast<uint32_t>(m_grid.GetCellIndex(points[i].x, points[i].y));
        sample.attempt = 0;

        if (params.densityFunc) {
            sample.density = params.densityFunc(points[i].x, points[i].y);
        } else {
            sample.density = 1.0f;
        }

        result.push_back(sample);
    }

    return result;
}

std::vector<glm::vec2> PoissonDiskSampler::SampleCircle(const glm::vec2& center, float radius,
                                                         float minDistance, int maxAttempts,
                                                         uint32_t seed) {
    PoissonDiskParams params;
    params.minDistance = minDistance;
    params.maxAttempts = maxAttempts;
    params.seed = seed;
    params.minX = center.x - radius;
    params.minY = center.y - radius;
    params.maxX = center.x + radius;
    params.maxY = center.y + radius;

    float radiusSq = radius * radius;
    params.rejectFunc = [center, radiusSq](float x, float y) {
        float dx = x - center.x;
        float dy = y - center.y;
        return (dx * dx + dy * dy) > radiusSq;
    };

    return Sample(params);
}

std::vector<glm::vec2> PoissonDiskSampler::SamplePolygon(const std::vector<glm::vec2>& polygon,
                                                          float minDistance, int maxAttempts,
                                                          uint32_t seed) {
    if (polygon.size() < 3) {
        return {};
    }

    // Compute bounding box
    glm::vec2 minBounds(std::numeric_limits<float>::max());
    glm::vec2 maxBounds(std::numeric_limits<float>::lowest());

    for (const auto& p : polygon) {
        minBounds = glm::min(minBounds, p);
        maxBounds = glm::max(maxBounds, p);
    }

    PoissonDiskParams params;
    params.minDistance = minDistance;
    params.maxAttempts = maxAttempts;
    params.seed = seed;
    params.minX = minBounds.x;
    params.minY = minBounds.y;
    params.maxX = maxBounds.x;
    params.maxY = maxBounds.y;

    // Capture polygon for rejection test
    params.rejectFunc = [this, &polygon](float x, float y) {
        return !PointInPolygon(glm::vec2(x, y), polygon);
    };

    return Sample(params);
}

std::vector<glm::vec2> PoissonDiskSampler::SampleIncremental(
    const std::vector<glm::vec2>& existing,
    const PoissonDiskParams& params) {

    // Seed RNG
    if (params.seed != 0) {
        m_rng.seed(params.seed);
    } else {
        m_rng.seed(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    // Initialize grid
    m_grid.Initialize(params.minX, params.minY, params.maxX, params.maxY, params.minDistance);

    // Reset stats
    m_stats = PoissonStats();

    // Insert existing points into grid
    std::vector<glm::vec2> points = existing;
    for (size_t i = 0; i < existing.size(); ++i) {
        m_grid.Insert(existing[i].x, existing[i].y, static_cast<int>(i));
    }

    // Active list starts with all existing points
    std::vector<int> activeList;
    for (size_t i = 0; i < existing.size(); ++i) {
        activeList.push_back(static_cast<int>(i));
    }

    float minDistSq = params.minDistance * params.minDistance;
    float width = params.maxX - params.minX;
    float height = params.maxY - params.minY;

    // If no existing points, add initial random point
    if (activeList.empty()) {
        float x = params.minX + m_uniformDist(m_rng) * width;
        float y = params.minY + m_uniformDist(m_rng) * height;

        if (!params.rejectFunc || !params.rejectFunc(x, y)) {
            points.emplace_back(x, y);
            m_grid.Insert(x, y, static_cast<int>(points.size()) - 1);
            activeList.push_back(static_cast<int>(points.size()) - 1);
        }
    }

    // Main loop
    while (!activeList.empty()) {
        // Pick random active point
        int randIdx = static_cast<int>(m_uniformDist(m_rng) * activeList.size());
        int activePointIdx = activeList[randIdx];
        const glm::vec2& activePoint = points[activePointIdx];

        bool foundValid = false;

        for (int attempt = 0; attempt < params.maxAttempts; ++attempt) {
            glm::vec2 newPoint = GeneratePointInAnnulus(activePoint,
                                                        params.minDistance,
                                                        2.0f * params.minDistance);

            // Bounds check
            if (newPoint.x < params.minX || newPoint.x > params.maxX ||
                newPoint.y < params.minY || newPoint.y > params.maxY) {
                continue;
            }

            // Custom rejection
            if (params.rejectFunc && params.rejectFunc(newPoint.x, newPoint.y)) {
                m_stats.rejectedPoints++;
                continue;
            }

            // Distance check
            if (!m_grid.CheckNeighbors(newPoint.x, newPoint.y, minDistSq, points)) {
                m_stats.distanceRejected++;
                continue;
            }

            // Valid point found
            points.push_back(newPoint);
            m_grid.Insert(newPoint.x, newPoint.y, static_cast<int>(points.size()) - 1);
            activeList.push_back(static_cast<int>(points.size()) - 1);
            foundValid = true;
            break;
        }

        if (!foundValid) {
            // Remove from active list (swap and pop for O(1))
            activeList[randIdx] = activeList.back();
            activeList.pop_back();
        }
    }

    // Return only newly generated points
    std::vector<glm::vec2> newPoints(points.begin() + existing.size(), points.end());
    return newPoints;
}

bool PoissonDiskSampler::IsPointValid(const glm::vec2& point, float minDistance,
                                       const std::vector<glm::vec2>& existingPoints) const {
    float minDistSq = minDistance * minDistance;

    for (const auto& existing : existingPoints) {
        float distSq = glm::dot(point - existing, point - existing);
        if (distSq < minDistSq) {
            return false;
        }
    }

    return true;
}

float PoissonDiskSampler::GetEffectiveMinDistance(float x, float y, float baseMinDistance,
                                                   const std::function<float(float, float)>& densityFunc) const {
    if (!densityFunc) {
        return baseMinDistance;
    }

    float density = std::max(0.01f, std::min(1.0f, densityFunc(x, y)));
    // Inverse relationship: lower density = larger minimum distance
    return baseMinDistance / density;
}

glm::vec2 PoissonDiskSampler::GeneratePointInAnnulus(const glm::vec2& center,
                                                      float minRadius, float maxRadius) {
    // Generate random point in annulus between minRadius and maxRadius
    float angle = m_uniformDist(m_rng) * 2.0f * 3.14159265358979f;
    float radius = std::sqrt(m_uniformDist(m_rng) * (maxRadius * maxRadius - minRadius * minRadius)
                             + minRadius * minRadius);

    return glm::vec2(center.x + radius * std::cos(angle),
                     center.y + radius * std::sin(angle));
}

std::vector<glm::vec2> PoissonDiskSampler::BridsonSample(const PoissonDiskParams& params) {
    // Seed RNG
    if (params.seed != 0) {
        m_rng.seed(params.seed);
    } else {
        m_rng.seed(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    // Initialize grid
    m_grid.Initialize(params.minX, params.minY, params.maxX, params.maxY, params.minDistance);

    // Reset stats
    m_stats = PoissonStats();

    std::vector<glm::vec2> points;
    std::vector<int> activeList;

    float minDistSq = params.minDistance * params.minDistance;
    float width = params.maxX - params.minX;
    float height = params.maxY - params.minY;

    // Step 1: Initial random sample
    float x0 = params.minX + m_uniformDist(m_rng) * width;
    float y0 = params.minY + m_uniformDist(m_rng) * height;

    // Retry if initial point is rejected
    int initAttempts = 0;
    while (params.rejectFunc && params.rejectFunc(x0, y0) && initAttempts < 1000) {
        x0 = params.minX + m_uniformDist(m_rng) * width;
        y0 = params.minY + m_uniformDist(m_rng) * height;
        initAttempts++;
    }

    if (initAttempts >= 1000) {
        return points;  // Couldn't find valid initial point
    }

    points.emplace_back(x0, y0);
    m_grid.Insert(x0, y0, 0);
    activeList.push_back(0);

    // Step 2: Main loop
    while (!activeList.empty()) {
        // Pick random active point
        int randIdx = static_cast<int>(m_uniformDist(m_rng) * activeList.size());
        int activePointIdx = activeList[randIdx];
        const glm::vec2& activePoint = points[activePointIdx];

        bool foundValid = false;

        // Step 3: Try k candidates around active point
        for (int attempt = 0; attempt < params.maxAttempts; ++attempt) {
            glm::vec2 newPoint = GeneratePointInAnnulus(activePoint,
                                                        params.minDistance,
                                                        2.0f * params.minDistance);

            // Bounds check
            if (newPoint.x < params.minX || newPoint.x > params.maxX ||
                newPoint.y < params.minY || newPoint.y > params.maxY) {
                continue;
            }

            // Custom rejection
            if (params.rejectFunc && params.rejectFunc(newPoint.x, newPoint.y)) {
                m_stats.rejectedPoints++;
                continue;
            }

            // Distance check using grid acceleration
            if (!m_grid.CheckNeighbors(newPoint.x, newPoint.y, minDistSq, points)) {
                m_stats.distanceRejected++;
                continue;
            }

            // Valid point found
            points.push_back(newPoint);
            m_grid.Insert(newPoint.x, newPoint.y, static_cast<int>(points.size()) - 1);
            activeList.push_back(static_cast<int>(points.size()) - 1);
            foundValid = true;
            break;
        }

        if (!foundValid) {
            // No valid point found - remove from active list
            activeList[randIdx] = activeList.back();
            activeList.pop_back();
        }
    }

    return points;
}

std::vector<glm::vec2> PoissonDiskSampler::VariableDensitySample(const PoissonDiskParams& params) {
    // Seed RNG
    if (params.seed != 0) {
        m_rng.seed(params.seed);
    } else {
        m_rng.seed(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    // For variable density, we use the minimum of the minDistance range for the grid
    m_grid.Initialize(params.minX, params.minY, params.maxX, params.maxY, params.minDistance);

    // Reset stats
    m_stats = PoissonStats();
    float densitySum = 0.0f;

    std::vector<glm::vec2> points;
    std::vector<int> activeList;
    std::vector<float> pointMinDists;  // Track min distance per point

    float width = params.maxX - params.minX;
    float height = params.maxY - params.minY;

    // Initial random sample
    float x0 = params.minX + m_uniformDist(m_rng) * width;
    float y0 = params.minY + m_uniformDist(m_rng) * height;

    int initAttempts = 0;
    while (params.rejectFunc && params.rejectFunc(x0, y0) && initAttempts < 1000) {
        x0 = params.minX + m_uniformDist(m_rng) * width;
        y0 = params.minY + m_uniformDist(m_rng) * height;
        initAttempts++;
    }

    if (initAttempts >= 1000) {
        return points;
    }

    float initialDensity = params.densityFunc(x0, y0);
    float initialMinDist = GetEffectiveMinDistance(x0, y0, params.minDistance, params.densityFunc);

    points.emplace_back(x0, y0);
    pointMinDists.push_back(initialMinDist);
    m_grid.Insert(x0, y0, 0);
    activeList.push_back(0);
    densitySum += initialDensity;

    // Main loop
    while (!activeList.empty()) {
        int randIdx = static_cast<int>(m_uniformDist(m_rng) * activeList.size());
        int activePointIdx = activeList[randIdx];
        const glm::vec2& activePoint = points[activePointIdx];
        float activeMinDist = pointMinDists[activePointIdx];

        bool foundValid = false;

        for (int attempt = 0; attempt < params.maxAttempts; ++attempt) {
            glm::vec2 newPoint = GeneratePointInAnnulus(activePoint,
                                                        activeMinDist,
                                                        2.0f * activeMinDist);

            // Bounds check
            if (newPoint.x < params.minX || newPoint.x > params.maxX ||
                newPoint.y < params.minY || newPoint.y > params.maxY) {
                continue;
            }

            // Custom rejection
            if (params.rejectFunc && params.rejectFunc(newPoint.x, newPoint.y)) {
                m_stats.rejectedPoints++;
                continue;
            }

            // Get effective min distance at new point
            float newMinDist = GetEffectiveMinDistance(newPoint.x, newPoint.y,
                                                       params.minDistance, params.densityFunc);

            // Check distance against all nearby points using the minimum of both distances
            bool tooClose = false;
            int cx = static_cast<int>((newPoint.x - m_grid.offsetX) * m_grid.invCellSize);
            int cy = static_cast<int>((newPoint.y - m_grid.offsetY) * m_grid.invCellSize);

            for (int dy = -3; dy <= 3 && !tooClose; ++dy) {
                for (int dx = -3; dx <= 3 && !tooClose; ++dx) {
                    int nx = cx + dx;
                    int ny = cy + dy;

                    if (nx < 0 || nx >= m_grid.width || ny < 0 || ny >= m_grid.height) {
                        continue;
                    }

                    int cellIdx = ny * m_grid.width + nx;
                    int pointIdx = m_grid.cells[cellIdx];

                    if (pointIdx >= 0) {
                        const glm::vec2& other = points[pointIdx];
                        float otherMinDist = pointMinDists[pointIdx];
                        float reqDist = std::min(newMinDist, otherMinDist);
                        float distSq = glm::dot(newPoint - other, newPoint - other);

                        if (distSq < reqDist * reqDist) {
                            tooClose = true;
                        }
                    }
                }
            }

            if (tooClose) {
                m_stats.distanceRejected++;
                continue;
            }

            // Valid point
            float density = params.densityFunc(newPoint.x, newPoint.y);
            densitySum += density;

            points.push_back(newPoint);
            pointMinDists.push_back(newMinDist);
            m_grid.Insert(newPoint.x, newPoint.y, static_cast<int>(points.size()) - 1);
            activeList.push_back(static_cast<int>(points.size()) - 1);
            foundValid = true;
            break;
        }

        if (!foundValid) {
            activeList[randIdx] = activeList.back();
            activeList.pop_back();
        }
    }

    if (!points.empty()) {
        m_stats.averageDensity = densitySum / static_cast<float>(points.size());
    }

    return points;
}

bool PoissonDiskSampler::PointInPolygon(const glm::vec2& point,
                                         const std::vector<glm::vec2>& polygon) const {
    // Ray casting algorithm for point-in-polygon test
    bool inside = false;
    size_t n = polygon.size();

    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const glm::vec2& pi = polygon[i];
        const glm::vec2& pj = polygon[j];

        if (((pi.y > point.y) != (pj.y > point.y)) &&
            (point.x < (pj.x - pi.x) * (point.y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }

    return inside;
}

// ============================================================================
// Convenience Functions
// ============================================================================

std::vector<glm::vec2> GeneratePoissonPoints(float width, float height,
                                              float minDistance, uint32_t seed) {
    PoissonDiskSampler sampler;
    PoissonDiskParams params;
    params.minX = 0.0f;
    params.minY = 0.0f;
    params.maxX = width;
    params.maxY = height;
    params.minDistance = minDistance;
    params.seed = seed;

    return sampler.Sample(params);
}

std::vector<glm::vec2> GenerateVariableDensityPoints(
    float width, float height,
    float minDistance, float maxDistance,
    std::function<float(float, float)> densityFunc,
    uint32_t seed) {

    PoissonDiskSampler sampler;
    PoissonDiskParams params;
    params.minX = 0.0f;
    params.minY = 0.0f;
    params.maxX = width;
    params.maxY = height;
    params.minDistance = minDistance;
    params.variableDensity = true;
    params.densityFunc = densityFunc;
    params.seed = seed;

    return sampler.Sample(params);
}

std::vector<glm::vec2> GenerateTerrainPlacementPoints(
    float minX, float minZ, float maxX, float maxZ,
    float minDistance,
    std::function<bool(float x, float z)> isValidPosition,
    std::function<float(float x, float z)> getDensity,
    uint32_t seed) {

    PoissonDiskSampler sampler;
    PoissonDiskParams params;
    params.minX = minX;
    params.minY = minZ;
    params.maxX = maxX;
    params.maxY = maxZ;
    params.minDistance = minDistance;
    params.seed = seed;
    params.rejectFunc = [isValidPosition](float x, float y) {
        return !isValidPosition(x, y);
    };

    if (getDensity) {
        params.variableDensity = true;
        params.densityFunc = getDensity;
    }

    return sampler.Sample(params);
}

} // namespace Cortex::Utils
