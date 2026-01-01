// LODGenerator.cpp
// Implementation of mesh simplification using quadric error metrics.

#include "LODGenerator.h"
#include <algorithm>
#include <queue>
#include <cmath>
#include <unordered_set>

namespace Cortex::Utils {

// ============================================================================
// Quadric
// ============================================================================

Quadric::Quadric(const glm::vec4& plane) {
    // Q = p * p^T where p is the plane equation [a,b,c,d]
    float A = plane.x, B = plane.y, C = plane.z, D = plane.w;

    a = A * A; b = A * B; c = A * C; d = A * D;
    e = B * B; f = B * C; g = B * D;
    h = C * C; i = C * D;
    j = D * D;
}

Quadric Quadric::operator+(const Quadric& other) const {
    Quadric result;
    result.a = a + other.a;
    result.b = b + other.b;
    result.c = c + other.c;
    result.d = d + other.d;
    result.e = e + other.e;
    result.f = f + other.f;
    result.g = g + other.g;
    result.h = h + other.h;
    result.i = i + other.i;
    result.j = j + other.j;
    return result;
}

Quadric& Quadric::operator+=(const Quadric& other) {
    a += other.a; b += other.b; c += other.c; d += other.d;
    e += other.e; f += other.f; g += other.g;
    h += other.h; i += other.i;
    j += other.j;
    return *this;
}

float Quadric::Evaluate(const glm::vec3& v) const {
    // v^T * Q * v where v is [x, y, z, 1]
    float x = v.x, y = v.y, z = v.z;
    return a*x*x + 2*b*x*y + 2*c*x*z + 2*d*x
         + e*y*y + 2*f*y*z + 2*g*y
         + h*z*z + 2*i*z
         + j;
}

bool Quadric::FindOptimalPosition(glm::vec3& outPos) const {
    // Solve for minimum: Q * [x,y,z,1]^T = 0
    // Need to solve the 3x3 system:
    // [a b c] [x]   [-d]
    // [b e f] [y] = [-g]
    // [c f h] [z]   [-i]

    // Using Cramer's rule
    float det = a*(e*h - f*f) - b*(b*h - c*f) + c*(b*f - c*e);

    if (std::abs(det) < 1e-10f) {
        return false;  // Singular matrix
    }

    float invDet = 1.0f / det;

    outPos.x = invDet * (-d*(e*h - f*f) + g*(b*h - c*f) - i*(b*f - c*e));
    outPos.y = invDet * (a*(-g*h + f*i) - b*(-d*h + c*i) + c*(-d*f + c*g));
    outPos.z = invDet * (a*(e*i - f*g) - b*(b*i - d*f) + c*(b*g - d*e));

    // Validate result
    if (!std::isfinite(outPos.x) || !std::isfinite(outPos.y) || !std::isfinite(outPos.z)) {
        return false;
    }

    return true;
}

// ============================================================================
// LODGenerator
// ============================================================================

LODGenerator::LODGenerator() {}

LODChain LODGenerator::GenerateLODs(const std::vector<VertexAttributes>& vertices,
                                      const std::vector<uint32_t>& indices) {
    LODChain chain;
    chain.originalVertexCount = static_cast<uint32_t>(vertices.size());
    chain.originalTriangleCount = static_cast<uint32_t>(indices.size() / 3);

    // Ensure we have enough reduction factors
    if (m_settings.reductionFactors.size() < m_settings.numLODLevels) {
        // Generate default factors
        for (uint32_t i = static_cast<uint32_t>(m_settings.reductionFactors.size());
             i < m_settings.numLODLevels; i++) {
            float factor = static_cast<float>(i) / static_cast<float>(m_settings.numLODLevels);
            m_settings.reductionFactors.push_back(factor);
        }
    }

    // Generate each LOD level
    for (uint32_t i = 0; i < m_settings.numLODLevels; i++) {
        float reduction = m_settings.reductionFactors[i];

        if (m_progressCallback) {
            float progress = static_cast<float>(i) / static_cast<float>(m_settings.numLODLevels);
            m_progressCallback(progress, "Generating LOD " + std::to_string(i));
        }

        LODLevel level;
        if (reduction < 0.01f) {
            // LOD 0 - full detail (just copy)
            level.vertices = vertices;
            level.indices = indices;
            level.triangleCount = static_cast<uint32_t>(indices.size() / 3);
            level.vertexCount = static_cast<uint32_t>(vertices.size());
        } else {
            level = GenerateLOD(vertices, indices, reduction);
        }

        level.reductionFactor = reduction;
        if (i < m_settings.screenPercentages.size()) {
            level.screenPercentage = m_settings.screenPercentages[i];
        }

        chain.levels.push_back(level);
    }

    if (m_progressCallback) {
        m_progressCallback(1.0f, "LOD generation complete");
    }

    return chain;
}

LODLevel LODGenerator::GenerateLOD(const std::vector<VertexAttributes>& vertices,
                                     const std::vector<uint32_t>& indices,
                                     float reductionFactor) {
    uint32_t originalTriangles = static_cast<uint32_t>(indices.size() / 3);
    uint32_t targetTriangles = static_cast<uint32_t>(originalTriangles * (1.0f - reductionFactor));
    targetTriangles = std::max(targetTriangles, 1u);

    return SimplifyToTriangleCount(vertices, indices, targetTriangles);
}

LODLevel LODGenerator::SimplifyToTriangleCount(const std::vector<VertexAttributes>& vertices,
                                                 const std::vector<uint32_t>& indices,
                                                 uint32_t targetTriangles) {
    LODLevel result;

    if (vertices.empty() || indices.empty()) {
        return result;
    }

    // Build initial triangle list
    std::vector<SimplifyTriangle> triangles;
    triangles.reserve(indices.size() / 3);
    for (size_t i = 0; i < indices.size(); i += 3) {
        SimplifyTriangle tri;
        tri.indices = {indices[i], indices[i+1], indices[i+2]};

        // Calculate face normal
        const glm::vec3& v0 = vertices[tri.indices[0]].position;
        const glm::vec3& v1 = vertices[tri.indices[1]].position;
        const glm::vec3& v2 = vertices[tri.indices[2]].position;
        tri.normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

        triangles.push_back(tri);
    }

    // Make a copy of vertices for modification
    std::vector<VertexAttributes> workVertices = vertices;
    std::vector<bool> vertexRemoved(vertices.size(), false);

    // Initialize quadrics for each vertex
    std::vector<Quadric> quadrics;
    InitializeQuadrics(workVertices, triangles, quadrics);

    // Build edge list
    std::vector<EdgeCollapse> edges;
    BuildEdgeList(workVertices, triangles, edges);

    // Sort edges by cost (min-heap)
    auto comparator = [](const EdgeCollapse& a, const EdgeCollapse& b) {
        return a.cost > b.cost;  // Min-heap
    };
    std::priority_queue<EdgeCollapse, std::vector<EdgeCollapse>, decltype(comparator)> edgeQueue(comparator);

    for (auto& edge : edges) {
        edge.cost = CalculateEdgeCost(edge.v1, edge.v2, workVertices, quadrics, edge.optimalPos);
        if (edge.isValid) {
            edgeQueue.push(edge);
        }
    }

    // Collapse edges until target is reached
    uint32_t currentTriangles = static_cast<uint32_t>(triangles.size());
    uint32_t iterations = 0;

    while (currentTriangles > targetTriangles && !edgeQueue.empty() && iterations < m_settings.maxIterations) {
        EdgeCollapse edge = edgeQueue.top();
        edgeQueue.pop();

        // Skip if vertices already removed
        if (vertexRemoved[edge.v1] || vertexRemoved[edge.v2]) {
            continue;
        }

        // Skip if cost exceeds threshold
        if (edge.cost > m_settings.maxError) {
            break;
        }

        // Skip boundary/seam edges if configured
        if (m_settings.preserveBoundaryEdges && IsBoundaryEdge(edge.v1, edge.v2, triangles)) {
            continue;
        }
        if ((m_settings.preserveUVSeams || m_settings.preserveNormalSeams) &&
            IsSeamEdge(edge.v1, edge.v2, workVertices)) {
            continue;
        }

        // Collapse edge: move v1 to optimal position, remove v2
        workVertices[edge.v1].position = edge.optimalPos;
        workVertices[edge.v1].normal = glm::normalize(
            workVertices[edge.v1].normal + workVertices[edge.v2].normal);
        workVertices[edge.v1].uv = (workVertices[edge.v1].uv + workVertices[edge.v2].uv) * 0.5f;

        vertexRemoved[edge.v2] = true;

        // Update quadric for v1
        quadrics[edge.v1] += quadrics[edge.v2];

        // Update triangles: replace v2 with v1, mark degenerate as removed
        for (auto& tri : triangles) {
            if (tri.isRemoved) continue;

            bool hasV1 = false, hasV2 = false;
            for (int k = 0; k < 3; k++) {
                if (tri.indices[k] == edge.v1) hasV1 = true;
                if (tri.indices[k] == edge.v2) {
                    tri.indices[k] = edge.v1;
                    hasV2 = true;
                }
            }

            // Check if triangle became degenerate
            if (hasV1 && hasV2) {
                tri.isRemoved = true;
                currentTriangles--;
            } else if (tri.indices[0] == tri.indices[1] ||
                       tri.indices[1] == tri.indices[2] ||
                       tri.indices[2] == tri.indices[0]) {
                tri.isRemoved = true;
                currentTriangles--;
            }
        }

        // Add new edge costs for v1's neighbors
        std::unordered_set<uint32_t> neighbors;
        for (const auto& tri : triangles) {
            if (tri.isRemoved) continue;
            for (int k = 0; k < 3; k++) {
                if (tri.indices[k] == edge.v1) {
                    for (int j = 0; j < 3; j++) {
                        if (j != k && !vertexRemoved[tri.indices[j]]) {
                            neighbors.insert(tri.indices[j]);
                        }
                    }
                }
            }
        }

        for (uint32_t neighbor : neighbors) {
            EdgeCollapse newEdge;
            newEdge.v1 = edge.v1;
            newEdge.v2 = neighbor;
            newEdge.cost = CalculateEdgeCost(newEdge.v1, newEdge.v2, workVertices, quadrics, newEdge.optimalPos);
            if (newEdge.cost < m_settings.maxError) {
                edgeQueue.push(newEdge);
            }
        }

        iterations++;
    }

    // Compact the mesh
    CompactMesh(workVertices, triangles, vertexRemoved, result);

    return result;
}

LODLevel LODGenerator::SimplifyToVertexCount(const std::vector<VertexAttributes>& vertices,
                                               const std::vector<uint32_t>& indices,
                                               uint32_t targetVertices) {
    // Estimate triangle count from vertex count (roughly 2:1 ratio)
    uint32_t estimatedTriangles = targetVertices * 2;
    return SimplifyToTriangleCount(vertices, indices, estimatedTriangles);
}

LODLevel LODGenerator::SimplifyToError(const std::vector<VertexAttributes>& vertices,
                                         const std::vector<uint32_t>& indices,
                                         float maxError) {
    LODGeneratorSettings prevSettings = m_settings;
    m_settings.maxError = maxError;

    LODLevel result = SimplifyToTriangleCount(vertices, indices, 1);

    m_settings = prevSettings;
    return result;
}

std::vector<float> LODGenerator::CalculateLODDistances(float meshRadius, uint32_t numLevels) const {
    std::vector<float> distances;
    distances.reserve(numLevels);

    // Use exponential spacing
    for (uint32_t i = 0; i < numLevels; i++) {
        float t = static_cast<float>(i) / static_cast<float>(numLevels - 1);
        float distance = meshRadius * (1.0f + t * 10.0f);  // 1x to 11x radius
        distances.push_back(distance);
    }

    return distances;
}

bool LODGenerator::ValidateLODChain(const LODChain& chain, std::string& errorMsg) const {
    if (chain.levels.empty()) {
        errorMsg = "LOD chain has no levels";
        return false;
    }

    // Check monotonically decreasing triangle counts
    uint32_t prevTriangles = UINT32_MAX;
    for (size_t i = 0; i < chain.levels.size(); i++) {
        if (chain.levels[i].triangleCount > prevTriangles) {
            errorMsg = "LOD " + std::to_string(i) + " has more triangles than previous level";
            return false;
        }
        if (chain.levels[i].triangleCount == 0) {
            errorMsg = "LOD " + std::to_string(i) + " has zero triangles";
            return false;
        }
        prevTriangles = chain.levels[i].triangleCount;
    }

    return true;
}

void LODGenerator::InitializeQuadrics(const std::vector<VertexAttributes>& vertices,
                                        const std::vector<SimplifyTriangle>& triangles,
                                        std::vector<Quadric>& quadrics) {
    quadrics.resize(vertices.size());

    for (const auto& tri : triangles) {
        // Calculate plane equation for triangle
        const glm::vec3& v0 = vertices[tri.indices[0]].position;
        const glm::vec3& v1 = vertices[tri.indices[1]].position;
        const glm::vec3& v2 = vertices[tri.indices[2]].position;

        glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        float d = -glm::dot(normal, v0);
        glm::vec4 plane(normal, d);

        Quadric q(plane);

        // Add to each vertex
        for (int k = 0; k < 3; k++) {
            quadrics[tri.indices[k]] += q;
        }
    }
}

void LODGenerator::BuildEdgeList(const std::vector<VertexAttributes>& vertices,
                                   const std::vector<SimplifyTriangle>& triangles,
                                   std::vector<EdgeCollapse>& edges) {
    std::unordered_set<uint64_t> edgeSet;

    for (const auto& tri : triangles) {
        for (int k = 0; k < 3; k++) {
            uint32_t v1 = tri.indices[k];
            uint32_t v2 = tri.indices[(k + 1) % 3];
            uint64_t hash = EdgeHash(v1, v2);

            if (edgeSet.find(hash) == edgeSet.end()) {
                edgeSet.insert(hash);
                EdgeCollapse edge;
                edge.v1 = std::min(v1, v2);
                edge.v2 = std::max(v1, v2);
                edges.push_back(edge);
            }
        }
    }
}

float LODGenerator::CalculateEdgeCost(uint32_t v1, uint32_t v2,
                                        const std::vector<VertexAttributes>& vertices,
                                        const std::vector<Quadric>& quadrics,
                                        glm::vec3& outOptimalPos) {
    Quadric combined = quadrics[v1] + quadrics[v2];

    // Try to find optimal position
    if (combined.FindOptimalPosition(outOptimalPos)) {
        return combined.Evaluate(outOptimalPos);
    }

    // Fallback: try endpoints and midpoint
    const glm::vec3& p1 = vertices[v1].position;
    const glm::vec3& p2 = vertices[v2].position;
    glm::vec3 mid = (p1 + p2) * 0.5f;

    float e1 = combined.Evaluate(p1);
    float e2 = combined.Evaluate(p2);
    float em = combined.Evaluate(mid);

    if (e1 <= e2 && e1 <= em) {
        outOptimalPos = p1;
        return e1;
    } else if (e2 <= em) {
        outOptimalPos = p2;
        return e2;
    } else {
        outOptimalPos = mid;
        return em;
    }
}

bool LODGenerator::IsBoundaryEdge(uint32_t v1, uint32_t v2,
                                   const std::vector<SimplifyTriangle>& triangles) const {
    int count = 0;
    for (const auto& tri : triangles) {
        if (tri.isRemoved) continue;

        bool hasV1 = false, hasV2 = false;
        for (int k = 0; k < 3; k++) {
            if (tri.indices[k] == v1) hasV1 = true;
            if (tri.indices[k] == v2) hasV2 = true;
        }
        if (hasV1 && hasV2) count++;
    }

    // Boundary edge is used by only one triangle
    return count == 1;
}

bool LODGenerator::IsSeamEdge(uint32_t v1, uint32_t v2,
                               const std::vector<VertexAttributes>& vertices) const {
    const auto& a = vertices[v1];
    const auto& b = vertices[v2];

    // Check UV discontinuity
    if (m_settings.preserveUVSeams) {
        // This is a simplified check - in practice you'd look at face UVs
        // For now, we don't mark as seam based on vertex data alone
    }

    // Check normal discontinuity
    if (m_settings.preserveNormalSeams) {
        float dot = glm::dot(a.normal, b.normal);
        float angleDeg = glm::degrees(std::acos(glm::clamp(dot, -1.0f, 1.0f)));
        if (angleDeg > m_settings.seamAngleThreshold) {
            return true;
        }
    }

    return false;
}

void LODGenerator::CompactMesh(const std::vector<VertexAttributes>& vertices,
                                 const std::vector<SimplifyTriangle>& triangles,
                                 const std::vector<bool>& vertexRemoved,
                                 LODLevel& output) {
    // Build vertex remapping
    std::vector<uint32_t> remap(vertices.size(), UINT32_MAX);
    uint32_t newIndex = 0;

    for (size_t i = 0; i < vertices.size(); i++) {
        if (!vertexRemoved[i]) {
            remap[i] = newIndex++;
            output.vertices.push_back(vertices[i]);
        }
    }

    // Remap and add triangles
    for (const auto& tri : triangles) {
        if (tri.isRemoved) continue;

        for (int k = 0; k < 3; k++) {
            uint32_t oldIdx = tri.indices[k];
            uint32_t newIdx = remap[oldIdx];
            if (newIdx == UINT32_MAX) {
                // Should not happen
                continue;
            }
            output.indices.push_back(newIdx);
        }
    }

    output.vertexCount = static_cast<uint32_t>(output.vertices.size());
    output.triangleCount = static_cast<uint32_t>(output.indices.size() / 3);
}

VertexAttributes LODGenerator::InterpolateAttributes(const VertexAttributes& a,
                                                        const VertexAttributes& b,
                                                        float t) const {
    VertexAttributes result;
    result.position = glm::mix(a.position, b.position, t);
    result.normal = glm::normalize(glm::mix(a.normal, b.normal, t));
    result.uv = glm::mix(a.uv, b.uv, t);
    result.tangent = glm::mix(a.tangent, b.tangent, t);
    result.color = glm::mix(a.color, b.color, t);

    // For bone weights, take from the vertex with higher total weight
    float weightA = a.boneWeights.x + a.boneWeights.y + a.boneWeights.z + a.boneWeights.w;
    float weightB = b.boneWeights.x + b.boneWeights.y + b.boneWeights.z + b.boneWeights.w;
    if (weightA >= weightB) {
        result.boneIndices = a.boneIndices;
        result.boneWeights = a.boneWeights;
    } else {
        result.boneIndices = b.boneIndices;
        result.boneWeights = b.boneWeights;
    }

    return result;
}

float LODChain::GetReductionRatio(uint32_t level) const {
    if (level >= levels.size() || originalTriangleCount == 0) return 0.0f;
    return 1.0f - (static_cast<float>(levels[level].triangleCount) /
                   static_cast<float>(originalTriangleCount));
}

float LODChain::GetTotalReductionRatio() const {
    if (levels.empty() || originalTriangleCount == 0) return 0.0f;
    return 1.0f - (static_cast<float>(levels.back().triangleCount) /
                   static_cast<float>(originalTriangleCount));
}

// ============================================================================
// LODUtils
// ============================================================================

namespace LODUtils {

float CalculateMeshRadius(const std::vector<VertexAttributes>& vertices) {
    if (vertices.empty()) return 0.0f;

    // Calculate centroid
    glm::vec3 center(0.0f);
    for (const auto& v : vertices) {
        center += v.position;
    }
    center /= static_cast<float>(vertices.size());

    // Find maximum distance from center
    float maxDistSq = 0.0f;
    for (const auto& v : vertices) {
        float distSq = glm::dot(v.position - center, v.position - center);
        maxDistSq = std::max(maxDistSq, distSq);
    }

    return std::sqrt(maxDistSq);
}

float CalculateMeshArea(const std::vector<VertexAttributes>& vertices,
                         const std::vector<uint32_t>& indices) {
    float totalArea = 0.0f;

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& v0 = vertices[indices[i]].position;
        const glm::vec3& v1 = vertices[indices[i+1]].position;
        const glm::vec3& v2 = vertices[indices[i+2]].position;

        glm::vec3 cross = glm::cross(v1 - v0, v2 - v0);
        totalArea += glm::length(cross) * 0.5f;
    }

    return totalArea;
}

void WeldVertices(std::vector<VertexAttributes>& vertices,
                   std::vector<uint32_t>& indices,
                   float positionThreshold) {
    // Simple spatial hashing
    float cellSize = positionThreshold * 2.0f;
    std::unordered_map<uint64_t, std::vector<uint32_t>> spatialHash;

    auto hashPosition = [cellSize](const glm::vec3& p) -> uint64_t {
        int32_t x = static_cast<int32_t>(std::floor(p.x / cellSize));
        int32_t y = static_cast<int32_t>(std::floor(p.y / cellSize));
        int32_t z = static_cast<int32_t>(std::floor(p.z / cellSize));
        return (static_cast<uint64_t>(x & 0x1FFFFF) << 42) |
               (static_cast<uint64_t>(y & 0x1FFFFF) << 21) |
               static_cast<uint64_t>(z & 0x1FFFFF);
    };

    std::vector<uint32_t> remap(vertices.size());
    std::vector<VertexAttributes> newVertices;

    for (uint32_t i = 0; i < vertices.size(); i++) {
        const auto& v = vertices[i];
        uint64_t hash = hashPosition(v.position);

        bool found = false;
        auto it = spatialHash.find(hash);
        if (it != spatialHash.end()) {
            for (uint32_t j : it->second) {
                if (glm::distance(v.position, newVertices[j].position) < positionThreshold) {
                    remap[i] = j;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            uint32_t newIdx = static_cast<uint32_t>(newVertices.size());
            remap[i] = newIdx;
            spatialHash[hash].push_back(newIdx);
            newVertices.push_back(v);
        }
    }

    // Remap indices
    for (auto& idx : indices) {
        idx = remap[idx];
    }

    vertices = std::move(newVertices);
}

void RemoveDegenerateTriangles(std::vector<uint32_t>& indices) {
    std::vector<uint32_t> newIndices;
    newIndices.reserve(indices.size());

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] != indices[i+1] &&
            indices[i+1] != indices[i+2] &&
            indices[i+2] != indices[i]) {
            newIndices.push_back(indices[i]);
            newIndices.push_back(indices[i+1]);
            newIndices.push_back(indices[i+2]);
        }
    }

    indices = std::move(newIndices);
}

float ScreenPercentageFromDistance(float distance, float objectRadius, float fovY, float screenHeight) {
    if (distance <= 0.0f) return 1.0f;

    float projectedSize = (objectRadius * 2.0f) / (distance * std::tan(fovY * 0.5f));
    return projectedSize / screenHeight;
}

float DistanceFromScreenPercentage(float screenPercentage, float objectRadius, float fovY, float screenHeight) {
    if (screenPercentage <= 0.0f) return FLT_MAX;

    float projectedSize = screenPercentage * screenHeight;
    return (objectRadius * 2.0f) / (projectedSize * std::tan(fovY * 0.5f));
}

} // namespace LODUtils

} // namespace Cortex::Utils
