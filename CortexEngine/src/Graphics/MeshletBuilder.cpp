#include "MeshletBuilder.h"
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <cmath>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

glm::vec3 MeshletBuilder::GetPosition(const void* vertices, size_t index, size_t stride, size_t offset) const {
    const uint8_t* ptr = static_cast<const uint8_t*>(vertices) + index * stride + offset;
    return *reinterpret_cast<const glm::vec3*>(ptr);
}

glm::vec3 MeshletBuilder::GetNormal(const void* vertices, size_t index, size_t stride, size_t offset) const {
    const uint8_t* ptr = static_cast<const uint8_t*>(vertices) + index * stride + offset;
    return *reinterpret_cast<const glm::vec3*>(ptr);
}

glm::vec4 MeshletBuilder::ComputeBoundingSphere(
    const void* vertices,
    const std::vector<uint32_t>& vertexIndices,
    size_t stride,
    size_t positionOffset
) const {
    if (vertexIndices.empty()) {
        return glm::vec4(0.0f);
    }

    // Compute centroid
    glm::vec3 center(0.0f);
    for (uint32_t idx : vertexIndices) {
        center += GetPosition(vertices, idx, stride, positionOffset);
    }
    center /= static_cast<float>(vertexIndices.size());

    // Compute radius
    float radiusSq = 0.0f;
    for (uint32_t idx : vertexIndices) {
        glm::vec3 pos = GetPosition(vertices, idx, stride, positionOffset);
        float distSq = glm::dot(pos - center, pos - center);
        radiusSq = std::max(radiusSq, distSq);
    }

    return glm::vec4(center, std::sqrt(radiusSq));
}

glm::vec4 MeshletBuilder::ComputeNormalCone(
    const void* vertices,
    const std::vector<uint32_t>& vertexIndices,
    const std::vector<uint32_t>& triangleIndices,
    size_t stride,
    size_t normalOffset
) const {
    if (triangleIndices.size() < 3 || normalOffset == static_cast<size_t>(-1)) {
        // Invalid or no normal cone requested
        return glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);  // Degenerate cone (always visible)
    }

    // Compute average normal
    glm::vec3 avgNormal(0.0f);
    size_t numTriangles = triangleIndices.size() / 3;

    for (size_t i = 0; i < numTriangles; ++i) {
        uint32_t i0 = triangleIndices[i * 3 + 0];
        uint32_t i1 = triangleIndices[i * 3 + 1];
        uint32_t i2 = triangleIndices[i * 3 + 2];

        glm::vec3 p0 = GetPosition(vertices, i0, stride, 0);  // Use actual position offset
        glm::vec3 p1 = GetPosition(vertices, i1, stride, 0);
        glm::vec3 p2 = GetPosition(vertices, i2, stride, 0);

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        glm::vec3 faceNormal = glm::normalize(glm::cross(e1, e2));

        avgNormal += faceNormal;
    }

    if (glm::length(avgNormal) < 0.001f) {
        return glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);  // Degenerate
    }

    avgNormal = glm::normalize(avgNormal);

    // Compute cone angle (max deviation from average normal)
    float minDot = 1.0f;
    for (size_t i = 0; i < numTriangles; ++i) {
        uint32_t i0 = triangleIndices[i * 3 + 0];
        uint32_t i1 = triangleIndices[i * 3 + 1];
        uint32_t i2 = triangleIndices[i * 3 + 2];

        glm::vec3 p0 = GetPosition(vertices, i0, stride, 0);
        glm::vec3 p1 = GetPosition(vertices, i1, stride, 0);
        glm::vec3 p2 = GetPosition(vertices, i2, stride, 0);

        glm::vec3 e1 = p1 - p0;
        glm::vec3 e2 = p2 - p0;
        glm::vec3 faceNormal = glm::normalize(glm::cross(e1, e2));

        float dot = glm::dot(avgNormal, faceNormal);
        minDot = std::min(minDot, dot);
    }

    return glm::vec4(avgNormal, minDot);
}

float MeshletBuilder::ScoreTriangle(
    uint32_t triangleIndex,
    const std::vector<bool>& usedVertices,
    const uint32_t* indices
) const {
    // Score based on how many vertices are already in the meshlet
    // Higher score = more shared vertices = better locality
    uint32_t i0 = indices[triangleIndex * 3 + 0];
    uint32_t i1 = indices[triangleIndex * 3 + 1];
    uint32_t i2 = indices[triangleIndex * 3 + 2];

    float score = 0.0f;
    if (usedVertices[i0]) score += 1.0f;
    if (usedVertices[i1]) score += 1.0f;
    if (usedVertices[i2]) score += 1.0f;

    return score;
}

Result<void> MeshletBuilder::Build(
    const void* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount,
    size_t vertexStride,
    size_t positionOffset,
    size_t normalOffset,
    const MeshletConfig& config,
    MeshletMesh& output
) {
    if (!vertices || !indices || indexCount == 0) {
        return Result<void>::Err("Invalid mesh data for meshlet building");
    }

    if (indexCount % 3 != 0) {
        return Result<void>::Err("Index count must be a multiple of 3");
    }

    size_t numTriangles = indexCount / 3;
    output.meshlets.clear();
    output.uniqueVertexIndices.clear();
    output.primitiveIndices.clear();
    output.totalTriangles = static_cast<uint32_t>(numTriangles);
    output.totalVertices = static_cast<uint32_t>(vertexCount);

    // Track which triangles have been assigned to a meshlet
    std::vector<bool> triangleUsed(numTriangles, false);
    size_t trianglesRemaining = numTriangles;

    // Build adjacency for locality optimization
    std::vector<std::vector<uint32_t>> vertexToTriangles(vertexCount);
    for (size_t t = 0; t < numTriangles; ++t) {
        vertexToTriangles[indices[t * 3 + 0]].push_back(static_cast<uint32_t>(t));
        vertexToTriangles[indices[t * 3 + 1]].push_back(static_cast<uint32_t>(t));
        vertexToTriangles[indices[t * 3 + 2]].push_back(static_cast<uint32_t>(t));
    }

    // Build meshlets
    while (trianglesRemaining > 0) {
        // Start a new meshlet
        Meshlet meshlet = {};
        meshlet.vertexOffset = static_cast<uint32_t>(output.uniqueVertexIndices.size());
        meshlet.triangleOffset = static_cast<uint32_t>(output.primitiveIndices.size());

        // Local vertex mapping for this meshlet
        std::unordered_map<uint32_t, uint32_t> globalToLocal;
        std::vector<uint32_t> localVertices;
        std::vector<uint32_t> localTriangles;

        // Track which original vertices are in this meshlet
        std::vector<bool> usedVertices(vertexCount, false);

        // Find seed triangle (first unused)
        uint32_t seedTriangle = UINT32_MAX;
        for (size_t t = 0; t < numTriangles; ++t) {
            if (!triangleUsed[t]) {
                seedTriangle = static_cast<uint32_t>(t);
                break;
            }
        }

        if (seedTriangle == UINT32_MAX) break;

        // Priority queue for triangle selection (score-based)
        std::vector<uint32_t> candidates;
        candidates.push_back(seedTriangle);

        while (!candidates.empty() &&
               localVertices.size() < config.maxVerticesPerMeshlet &&
               localTriangles.size() / 3 < config.maxTrianglesPerMeshlet) {

            // Find best candidate
            float bestScore = -1.0f;
            size_t bestIdx = 0;

            for (size_t i = 0; i < candidates.size(); ++i) {
                if (triangleUsed[candidates[i]]) continue;

                float score = ScoreTriangle(candidates[i], usedVertices, indices);

                // Check if adding this triangle would exceed vertex limit
                uint32_t newVerts = 0;
                uint32_t i0 = indices[candidates[i] * 3 + 0];
                uint32_t i1 = indices[candidates[i] * 3 + 1];
                uint32_t i2 = indices[candidates[i] * 3 + 2];
                if (!usedVertices[i0]) newVerts++;
                if (!usedVertices[i1]) newVerts++;
                if (!usedVertices[i2]) newVerts++;

                if (localVertices.size() + newVerts > config.maxVerticesPerMeshlet) {
                    continue;  // Would exceed vertex limit
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestIdx = i;
                }
            }

            if (bestScore < 0.0f) {
                // No valid candidates found
                break;
            }

            uint32_t triIdx = candidates[bestIdx];
            candidates.erase(candidates.begin() + bestIdx);

            if (triangleUsed[triIdx]) continue;

            // Add triangle to meshlet
            triangleUsed[triIdx] = true;
            trianglesRemaining--;

            uint32_t i0 = indices[triIdx * 3 + 0];
            uint32_t i1 = indices[triIdx * 3 + 1];
            uint32_t i2 = indices[triIdx * 3 + 2];

            // Map vertices
            auto mapVertex = [&](uint32_t globalIdx) -> uint32_t {
                auto it = globalToLocal.find(globalIdx);
                if (it != globalToLocal.end()) {
                    return it->second;
                }
                uint32_t localIdx = static_cast<uint32_t>(localVertices.size());
                globalToLocal[globalIdx] = localIdx;
                localVertices.push_back(globalIdx);
                usedVertices[globalIdx] = true;
                return localIdx;
            };

            uint32_t l0 = mapVertex(i0);
            uint32_t l1 = mapVertex(i1);
            uint32_t l2 = mapVertex(i2);

            // Store packed triangle (local indices)
            localTriangles.push_back(l0);
            localTriangles.push_back(l1);
            localTriangles.push_back(l2);

            // Add adjacent triangles as candidates
            for (uint32_t adj : vertexToTriangles[i0]) {
                if (!triangleUsed[adj]) candidates.push_back(adj);
            }
            for (uint32_t adj : vertexToTriangles[i1]) {
                if (!triangleUsed[adj]) candidates.push_back(adj);
            }
            for (uint32_t adj : vertexToTriangles[i2]) {
                if (!triangleUsed[adj]) candidates.push_back(adj);
            }

            // Remove duplicates from candidates
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        }

        if (localTriangles.empty()) continue;

        // Finalize meshlet
        meshlet.vertexCount = static_cast<uint32_t>(localVertices.size());
        meshlet.triangleCount = static_cast<uint32_t>(localTriangles.size() / 3);

        // Compute bounding sphere
        meshlet.boundingSphere = ComputeBoundingSphere(
            vertices, localVertices, vertexStride, positionOffset
        );

        // Compute normal cone (if requested)
        if (config.generateNormalCones && normalOffset != static_cast<size_t>(-1)) {
            // Map local triangles back to global for normal cone computation
            std::vector<uint32_t> globalTriIndices;
            for (size_t i = 0; i < localTriangles.size(); i += 3) {
                globalTriIndices.push_back(localVertices[localTriangles[i + 0]]);
                globalTriIndices.push_back(localVertices[localTriangles[i + 1]]);
                globalTriIndices.push_back(localVertices[localTriangles[i + 2]]);
            }
            meshlet.normalCone = ComputeNormalCone(
                vertices, localVertices, globalTriIndices, vertexStride, normalOffset
            );
        } else {
            meshlet.normalCone = glm::vec4(0.0f, 1.0f, 0.0f, -1.0f);
        }

        // Store vertex indices
        for (uint32_t globalIdx : localVertices) {
            output.uniqueVertexIndices.push_back(globalIdx);
        }

        // Store packed triangle indices
        // Pack 3 10-bit indices into one uint32_t
        for (size_t i = 0; i < localTriangles.size(); i += 3) {
            uint32_t packed = localTriangles[i + 0] |
                             (localTriangles[i + 1] << 10) |
                             (localTriangles[i + 2] << 20);
            output.primitiveIndices.push_back(packed);
        }

        output.meshlets.push_back(meshlet);
    }

    // Compute statistics
    if (!output.meshlets.empty()) {
        uint32_t totalTris = 0;
        uint32_t totalVerts = 0;
        for (const auto& m : output.meshlets) {
            totalTris += m.triangleCount;
            totalVerts += m.vertexCount;
        }
        output.averageTrianglesPerMeshlet = static_cast<float>(totalTris) / output.meshlets.size();
        output.averageVerticesPerMeshlet = static_cast<float>(totalVerts) / output.meshlets.size();
    }

    spdlog::debug("Built {} meshlets from {} triangles (avg {:.1f} tris, {:.1f} verts per meshlet)",
                  output.meshlets.size(), numTriangles,
                  output.averageTrianglesPerMeshlet, output.averageVerticesPerMeshlet);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
