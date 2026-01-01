// LloydRelaxation.cpp
// Lloyd's Algorithm Implementation for Voronoi-Based Point Relaxation
// Reference: Lloyd, S. "Least Squares Quantization in PCM"

#include "LloydRelaxation.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>
#include <limits>

namespace Cortex::Utils {

// ============================================================================
// Acceleration Grid Implementation
// ============================================================================

void LloydRelaxation::AccelerationGrid::Build(const std::vector<glm::vec2>& points,
                                               const LloydParams& params) {
    if (points.empty()) return;

    float areaWidth = params.maxX - params.minX;
    float areaHeight = params.maxY - params.minY;

    // Cell size based on expected point density
    float avgSpacing = std::sqrt(areaWidth * areaHeight / static_cast<float>(points.size()));
    cellSize = avgSpacing * 2.0f;

    minX = params.minX;
    minY = params.minY;

    width = static_cast<int>(std::ceil(areaWidth / cellSize)) + 1;
    height = static_cast<int>(std::ceil(areaHeight / cellSize)) + 1;

    cells.clear();
    cells.resize(width * height);

    for (size_t i = 0; i < points.size(); ++i) {
        int cx = static_cast<int>((points[i].x - minX) / cellSize);
        int cy = static_cast<int>((points[i].y - minY) / cellSize);
        cx = std::max(0, std::min(width - 1, cx));
        cy = std::max(0, std::min(height - 1, cy));

        cells[cy * width + cx].push_back(static_cast<int>(i));
    }
}

int LloydRelaxation::AccelerationGrid::FindNearest(const glm::vec2& pos,
                                                    const std::vector<glm::vec2>& points) const {
    if (points.empty()) return -1;

    int cx = static_cast<int>((pos.x - minX) / cellSize);
    int cy = static_cast<int>((pos.y - minY) / cellSize);
    cx = std::max(0, std::min(width - 1, cx));
    cy = std::max(0, std::min(height - 1, cy));

    int nearestIdx = -1;
    float nearestDistSq = std::numeric_limits<float>::max();

    // Expanding ring search
    int maxRadius = std::max(width, height);
    for (int r = 0; r <= maxRadius; ++r) {
        bool foundInRing = false;

        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                // Only check cells on the ring boundary
                if (r > 0 && std::abs(dx) != r && std::abs(dy) != r) continue;

                int nx = cx + dx;
                int ny = cy + dy;

                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;

                const auto& cell = cells[ny * width + nx];
                for (int idx : cell) {
                    float distSq = glm::dot(pos - points[idx], pos - points[idx]);
                    if (distSq < nearestDistSq) {
                        nearestDistSq = distSq;
                        nearestIdx = idx;
                        foundInRing = true;
                    }
                }
            }
        }

        // If we found something and the ring is beyond the nearest distance, stop
        if (nearestIdx >= 0 && r > 0) {
            float ringDist = static_cast<float>(r - 1) * cellSize;
            if (ringDist * ringDist > nearestDistSq) break;
        }
    }

    return nearestIdx;
}

// ============================================================================
// LloydRelaxation Implementation
// ============================================================================

LloydRelaxation::LloydRelaxation() {}

std::vector<glm::vec2> LloydRelaxation::Relax(std::vector<glm::vec2>& points,
                                               const LloydParams& params) {
    auto startTime = std::chrono::high_resolution_clock::now();

    m_stats = LloydStats();

    if (points.size() < 2) {
        return points;
    }

    // Iterative relaxation
    for (int iter = 0; iter < params.maxIterations; ++iter) {
        float maxMovement = RelaxStep(points, params);

        m_stats.maxMovement = maxMovement;
        m_stats.iterations = iter + 1;

        if (maxMovement < params.convergenceThreshold) {
            m_stats.converged = true;
            break;
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    m_stats.executionTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

    return points;
}

std::vector<VoronoiCell> LloydRelaxation::RelaxWithCells(std::vector<glm::vec2>& points,
                                                          const LloydParams& params) {
    // First, perform standard relaxation
    Relax(points, params);

    // Then compute final Voronoi diagram
    LloydParams cellParams = params;
    cellParams.computeCellGeometry = true;

    return ComputeVoronoi(points, cellParams);
}

float LloydRelaxation::RelaxStep(std::vector<glm::vec2>& points, const LloydParams& params) {
    if (points.empty()) return 0.0f;

    // Build acceleration grid
    m_grid.Build(points, params);

    // Compute centroids for all cells
    std::vector<glm::vec2> centroids = ComputeCentroids(points, params);

    // Move points toward centroids
    float maxMovement = 0.0f;
    float totalMovement = 0.0f;

    for (size_t i = 0; i < points.size(); ++i) {
        glm::vec2 movement = (centroids[i] - points[i]) * params.dampingFactor;
        glm::vec2 newPos = points[i] + movement;

        // Clamp to bounds (or wrap if toroidal)
        if (params.wrapBounds) {
            float width = params.maxX - params.minX;
            float height = params.maxY - params.minY;

            while (newPos.x < params.minX) newPos.x += width;
            while (newPos.x > params.maxX) newPos.x -= width;
            while (newPos.y < params.minY) newPos.y += height;
            while (newPos.y > params.maxY) newPos.y -= height;
        } else {
            newPos.x = std::max(params.minX, std::min(params.maxX, newPos.x));
            newPos.y = std::max(params.minY, std::min(params.maxY, newPos.y));
        }

        float moveDist = glm::length(newPos - points[i]);
        maxMovement = std::max(maxMovement, moveDist);
        totalMovement += moveDist;

        points[i] = newPos;
    }

    m_stats.averageMovement = totalMovement / static_cast<float>(points.size());

    return maxMovement;
}

std::vector<VoronoiCell> LloydRelaxation::ComputeVoronoi(const std::vector<glm::vec2>& points,
                                                          const LloydParams& params) {
    std::vector<VoronoiCell> cells(points.size());

    // Initialize cells with generators
    for (size_t i = 0; i < points.size(); ++i) {
        cells[i].generator = points[i];
        cells[i].area = 0.0f;
    }

    // Compute cell geometry if requested
    if (params.computeCellGeometry) {
        ComputeVoronoiDiagram(points, params, cells);
    }

    // Compute centroids
    std::vector<glm::vec2> centroids = ComputeCentroids(points, params);
    for (size_t i = 0; i < points.size(); ++i) {
        cells[i].centroid = centroids[i];
    }

    return cells;
}

std::vector<glm::vec2> LloydRelaxation::ComputeCentroids(const std::vector<glm::vec2>& points,
                                                          const LloydParams& params) {
    std::vector<glm::vec2> centroids(points.size());

    // Use Monte Carlo sampling for centroid estimation
    // This is more robust than analytical methods for arbitrary shapes
    #pragma omp parallel for
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        centroids[i] = ComputeCentroidMonteCarlo(i, points, params, 512);
    }

    return centroids;
}

int LloydRelaxation::FindNearestPoint(const glm::vec2& position,
                                       const std::vector<glm::vec2>& points) const {
    if (points.empty()) return -1;

    int nearestIdx = 0;
    float nearestDistSq = glm::dot(position - points[0], position - points[0]);

    for (size_t i = 1; i < points.size(); ++i) {
        float distSq = glm::dot(position - points[i], position - points[i]);
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearestIdx = static_cast<int>(i);
        }
    }

    return nearestIdx;
}

float LloydRelaxation::DistanceToBoundary(const glm::vec2& position,
                                           const LloydParams& params) const {
    float dx1 = position.x - params.minX;
    float dx2 = params.maxX - position.x;
    float dy1 = position.y - params.minY;
    float dy2 = params.maxY - position.y;

    return std::min({dx1, dx2, dy1, dy2});
}

bool LloydRelaxation::IsOnBoundary(const glm::vec2& position,
                                    const LloydParams& params, float threshold) const {
    return DistanceToBoundary(position, params) < threshold;
}

glm::vec2 LloydRelaxation::ComputeCentroidMonteCarlo(int pointIndex,
                                                      const std::vector<glm::vec2>& points,
                                                      const LloydParams& params,
                                                      int numSamples) {
    const glm::vec2& generator = points[pointIndex];

    // Estimate cell bounds based on nearest neighbors
    float searchRadius = 0.0f;
    for (const auto& other : points) {
        if (&other != &generator) {
            float dist = glm::length(other - generator);
            searchRadius = std::max(searchRadius, dist);
        }
    }
    searchRadius = std::min(searchRadius, std::max(params.maxX - params.minX, params.maxY - params.minY) * 0.5f);

    // Monte Carlo sampling with stratified jitter
    glm::vec2 centroidSum(0.0f);
    float weightSum = 0.0f;

    std::mt19937 rng(static_cast<uint32_t>(pointIndex * 12345 + 67890));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    int sqrtSamples = static_cast<int>(std::sqrt(static_cast<float>(numSamples)));

    for (int sy = 0; sy < sqrtSamples; ++sy) {
        for (int sx = 0; sx < sqrtSamples; ++sx) {
            // Stratified sample with jitter
            float jx = dist(rng) * 0.5f;
            float jy = dist(rng) * 0.5f;

            float nx = (static_cast<float>(sx) + 0.5f + jx) / static_cast<float>(sqrtSamples);
            float ny = (static_cast<float>(sy) + 0.5f + jy) / static_cast<float>(sqrtSamples);

            // Map to search area
            glm::vec2 sample(
                generator.x + (nx * 2.0f - 1.0f) * searchRadius,
                generator.y + (ny * 2.0f - 1.0f) * searchRadius
            );

            // Bounds check
            if (sample.x < params.minX || sample.x > params.maxX ||
                sample.y < params.minY || sample.y > params.maxY) {
                continue;
            }

            // Check if this sample belongs to our cell (nearest to generator)
            bool belongsToCell = true;
            float myDistSq = glm::dot(sample - generator, sample - generator);

            for (size_t j = 0; j < points.size(); ++j) {
                if (j == static_cast<size_t>(pointIndex)) continue;

                float otherDistSq = glm::dot(sample - points[j], sample - points[j]);
                if (otherDistSq < myDistSq) {
                    belongsToCell = false;
                    break;
                }
            }

            if (belongsToCell) {
                float weight = 1.0f;

                // Apply density weighting if provided
                if (params.densityFunc) {
                    weight = params.densityFunc(sample.x, sample.y);
                }

                centroidSum += sample * weight;
                weightSum += weight;
            }
        }
    }

    if (weightSum > 0.0f) {
        return centroidSum / weightSum;
    }

    return generator;  // Fallback to original position
}

glm::vec2 LloydRelaxation::ComputeCentroidAnalytical(int pointIndex,
                                                      const std::vector<glm::vec2>& points,
                                                      const VoronoiCell& cell,
                                                      const LloydParams& params) {
    if (cell.vertices.size() < 3) {
        return cell.generator;
    }

    // Compute weighted centroid of polygon
    if (params.densityFunc) {
        // With density weighting, use numerical integration
        return ComputeCentroidMonteCarlo(pointIndex, points, params, 512);
    }

    // Without density weighting, use analytical polygon centroid
    return PolygonCentroid(cell.vertices);
}

std::vector<glm::vec2> LloydRelaxation::ClipCellToBounds(const std::vector<glm::vec2>& cellVertices,
                                                          const LloydParams& params) {
    if (cellVertices.empty()) return {};

    // Sutherland-Hodgman polygon clipping
    std::vector<glm::vec2> output = cellVertices;

    // Clip against each edge of bounding box
    struct Edge {
        glm::vec2 p1, p2;
    };

    std::vector<Edge> clipEdges = {
        {glm::vec2(params.minX, params.minY), glm::vec2(params.maxX, params.minY)},  // Bottom
        {glm::vec2(params.maxX, params.minY), glm::vec2(params.maxX, params.maxY)},  // Right
        {glm::vec2(params.maxX, params.maxY), glm::vec2(params.minX, params.maxY)},  // Top
        {glm::vec2(params.minX, params.maxY), glm::vec2(params.minX, params.minY)}   // Left
    };

    for (const auto& edge : clipEdges) {
        if (output.empty()) break;

        std::vector<glm::vec2> input = output;
        output.clear();

        glm::vec2 edgeNormal(edge.p2.y - edge.p1.y, edge.p1.x - edge.p2.x);

        for (size_t i = 0; i < input.size(); ++i) {
            const glm::vec2& current = input[i];
            const glm::vec2& next = input[(i + 1) % input.size()];

            float currentDot = glm::dot(current - edge.p1, edgeNormal);
            float nextDot = glm::dot(next - edge.p1, edgeNormal);

            if (currentDot >= 0) {
                output.push_back(current);
            }

            if ((currentDot >= 0) != (nextDot >= 0)) {
                // Edge crosses clip boundary
                float t = currentDot / (currentDot - nextDot);
                output.push_back(current + t * (next - current));
            }
        }
    }

    return output;
}

glm::vec2 LloydRelaxation::PolygonCentroid(const std::vector<glm::vec2>& vertices) const {
    if (vertices.size() < 3) {
        if (!vertices.empty()) return vertices[0];
        return glm::vec2(0.0f);
    }

    glm::vec2 centroid(0.0f);
    float signedArea = 0.0f;

    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec2& v0 = vertices[i];
        const glm::vec2& v1 = vertices[(i + 1) % vertices.size()];

        float cross = v0.x * v1.y - v1.x * v0.y;
        signedArea += cross;
        centroid += (v0 + v1) * cross;
    }

    signedArea *= 0.5f;

    if (std::abs(signedArea) > 1e-8f) {
        centroid /= (6.0f * signedArea);
    }

    return centroid;
}

float LloydRelaxation::PolygonArea(const std::vector<glm::vec2>& vertices) const {
    if (vertices.size() < 3) return 0.0f;

    float area = 0.0f;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec2& v0 = vertices[i];
        const glm::vec2& v1 = vertices[(i + 1) % vertices.size()];
        area += v0.x * v1.y - v1.x * v0.y;
    }

    return std::abs(area) * 0.5f;
}

void LloydRelaxation::ComputeVoronoiDiagram(const std::vector<glm::vec2>& points,
                                             const LloydParams& params,
                                             std::vector<VoronoiCell>& cells) {
    // Simplified Voronoi computation using incremental construction
    // For production use, consider Fortune's algorithm

    float width = params.maxX - params.minX;
    float height = params.maxY - params.minY;

    for (size_t i = 0; i < points.size(); ++i) {
        // Start with bounding box as cell
        std::vector<glm::vec2> cellVerts = {
            glm::vec2(params.minX, params.minY),
            glm::vec2(params.maxX, params.minY),
            glm::vec2(params.maxX, params.maxY),
            glm::vec2(params.minX, params.maxY)
        };

        // Clip against half-planes formed by each other point
        for (size_t j = 0; j < points.size(); ++j) {
            if (i == j) continue;

            // Half-plane: points closer to i than j
            glm::vec2 mid = (points[i] + points[j]) * 0.5f;
            glm::vec2 normal = glm::normalize(points[j] - points[i]);

            // Clip polygon against this half-plane
            std::vector<glm::vec2> clipped;

            for (size_t k = 0; k < cellVerts.size(); ++k) {
                const glm::vec2& v0 = cellVerts[k];
                const glm::vec2& v1 = cellVerts[(k + 1) % cellVerts.size()];

                float d0 = glm::dot(v0 - mid, normal);
                float d1 = glm::dot(v1 - mid, normal);

                if (d0 <= 0) {
                    clipped.push_back(v0);
                }

                if ((d0 <= 0) != (d1 <= 0)) {
                    float t = d0 / (d0 - d1);
                    clipped.push_back(v0 + t * (v1 - v0));
                }
            }

            cellVerts = clipped;
            if (cellVerts.size() < 3) break;
        }

        cells[i].vertices = cellVerts;
        cells[i].area = PolygonArea(cellVerts);

        // Find neighbors (cells sharing an edge)
        for (size_t j = 0; j < points.size(); ++j) {
            if (i == j) continue;

            // Check if cells share a boundary (simplified check)
            glm::vec2 mid = (points[i] + points[j]) * 0.5f;
            if (mid.x >= params.minX && mid.x <= params.maxX &&
                mid.y >= params.minY && mid.y <= params.maxY) {
                cells[i].neighbors.push_back(static_cast<uint32_t>(j));
            }
        }
    }
}

// ============================================================================
// Convenience Functions
// ============================================================================

std::vector<glm::vec2> RelaxPoints(std::vector<glm::vec2> points,
                                    float minX, float minY, float maxX, float maxY,
                                    int iterations) {
    LloydRelaxation relaxer;
    LloydParams params;
    params.minX = minX;
    params.minY = minY;
    params.maxX = maxX;
    params.maxY = maxY;
    params.maxIterations = iterations;

    return relaxer.Relax(points, params);
}

std::vector<glm::vec2> RelaxPointsWeighted(std::vector<glm::vec2> points,
                                            float minX, float minY, float maxX, float maxY,
                                            std::function<float(float, float)> densityFunc,
                                            int iterations) {
    LloydRelaxation relaxer;
    LloydParams params;
    params.minX = minX;
    params.minY = minY;
    params.maxX = maxX;
    params.maxY = maxY;
    params.maxIterations = iterations;
    params.densityFunc = densityFunc;

    return relaxer.Relax(points, params);
}

std::vector<glm::vec2> GenerateRelaxedPoints(int numPoints,
                                              float minX, float minY, float maxX, float maxY,
                                              int iterations, uint32_t seed) {
    // Generate initial random distribution
    std::mt19937 rng(seed != 0 ? seed : static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<float> distX(minX, maxX);
    std::uniform_real_distribution<float> distY(minY, maxY);

    std::vector<glm::vec2> points;
    points.reserve(numPoints);

    for (int i = 0; i < numPoints; ++i) {
        points.emplace_back(distX(rng), distY(rng));
    }

    return RelaxPoints(points, minX, minY, maxX, maxY, iterations);
}

std::vector<glm::vec2> RefineWithRelaxation(const std::vector<glm::vec2>& poissonPoints,
                                             float minX, float minY, float maxX, float maxY,
                                             int iterations) {
    std::vector<glm::vec2> points = poissonPoints;

    LloydRelaxation relaxer;
    LloydParams params;
    params.minX = minX;
    params.minY = minY;
    params.maxX = maxX;
    params.maxY = maxY;
    params.maxIterations = iterations;
    params.dampingFactor = 0.5f;  // Gentler movement to preserve Poisson properties

    return relaxer.Relax(points, params);
}

} // namespace Cortex::Utils
