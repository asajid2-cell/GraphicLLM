#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "Utils/Result.h"

namespace Cortex::Graphics {

// Meshlet structure - a cluster of triangles for fine-grained GPU culling
// Each meshlet contains up to 64 vertices and 126 triangles (max for DX12 mesh shaders)
struct Meshlet {
    uint32_t vertexOffset;      // Offset into unique vertex indices
    uint32_t triangleOffset;    // Offset into packed triangle indices
    uint32_t vertexCount;       // Number of unique vertices (max 64)
    uint32_t triangleCount;     // Number of triangles (max 126)

    // Bounding sphere for culling (object space)
    glm::vec4 boundingSphere;   // xyz = center, w = radius

    // Normal cone for backface culling
    // If cone apex is visible and all normals face away, meshlet is invisible
    glm::vec4 normalCone;       // xyz = cone axis, w = cos(cone angle)
};

// Meshlet mesh data - output from meshlet builder
struct MeshletMesh {
    std::vector<Meshlet> meshlets;

    // Vertex remapping: maps meshlet-local vertex indices to original mesh vertices
    std::vector<uint32_t> uniqueVertexIndices;

    // Packed triangle indices (3 bytes per triangle, 10-bit local vertex indices)
    // Each uint32_t contains indices for one triangle: (v0 | v1 << 10 | v2 << 20)
    std::vector<uint32_t> primitiveIndices;

    // Statistics
    uint32_t totalTriangles = 0;
    uint32_t totalVertices = 0;
    float averageTrianglesPerMeshlet = 0.0f;
    float averageVerticesPerMeshlet = 0.0f;
};

// Meshlet builder configuration
struct MeshletConfig {
    uint32_t maxVerticesPerMeshlet = 64;    // Max unique vertices (DX12 limit: 256)
    uint32_t maxTrianglesPerMeshlet = 126;  // Max triangles (DX12 limit: 256)
    bool generateNormalCones = true;        // Enable backface culling cones
    bool optimizeForCache = true;           // Optimize vertex/triangle order
};

// Meshlet Builder
// Decomposes an indexed triangle mesh into meshlets for GPU-driven rendering.
//
// Usage:
//   MeshletBuilder builder;
//   MeshletMesh output;
//   builder.Build(vertices, indices, vertexStride, positionOffset, normalOffset, config, output);
//
class MeshletBuilder {
public:
    MeshletBuilder() = default;

    // Build meshlets from a triangle mesh
    // vertices: Raw vertex data
    // indices: Triangle indices (3 per triangle)
    // vertexStride: Bytes per vertex
    // positionOffset: Offset to position (vec3) in vertex struct
    // normalOffset: Offset to normal (vec3) in vertex struct (-1 to skip normal cones)
    Result<void> Build(
        const void* vertices,
        size_t vertexCount,
        const uint32_t* indices,
        size_t indexCount,
        size_t vertexStride,
        size_t positionOffset,
        size_t normalOffset,
        const MeshletConfig& config,
        MeshletMesh& output
    );

    // Build meshlets with typed vertex access (convenience wrapper)
    template<typename Vertex>
    Result<void> Build(
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        size_t positionOffset,
        size_t normalOffset,
        const MeshletConfig& config,
        MeshletMesh& output
    ) {
        return Build(
            vertices.data(),
            vertices.size(),
            indices.data(),
            indices.size(),
            sizeof(Vertex),
            positionOffset,
            normalOffset,
            config,
            output
        );
    }

private:
    // Helper to get position from vertex data
    glm::vec3 GetPosition(const void* vertices, size_t index, size_t stride, size_t offset) const;

    // Helper to get normal from vertex data
    glm::vec3 GetNormal(const void* vertices, size_t index, size_t stride, size_t offset) const;

    // Compute bounding sphere for a set of vertices
    glm::vec4 ComputeBoundingSphere(
        const void* vertices,
        const std::vector<uint32_t>& vertexIndices,
        size_t stride,
        size_t positionOffset
    ) const;

    // Compute normal cone for a set of triangles
    glm::vec4 ComputeNormalCone(
        const void* vertices,
        const std::vector<uint32_t>& vertexIndices,
        const std::vector<uint32_t>& triangleIndices,
        size_t stride,
        size_t normalOffset
    ) const;

    // Score a triangle for meshlet inclusion (locality optimization)
    float ScoreTriangle(
        uint32_t triangleIndex,
        const std::vector<bool>& usedVertices,
        const uint32_t* indices
    ) const;
};

// GPU-side meshlet structures (for mesh shaders)
namespace GPU {

// Meshlet data as seen by mesh shader
struct alignas(16) MeshletData {
    uint32_t vertexOffset;
    uint32_t triangleOffset;
    uint32_t vertexCount;
    uint32_t triangleCount;
    glm::vec4 boundingSphere;
    glm::vec4 normalCone;
};

// Per-meshlet culling data (separate for cache efficiency)
struct alignas(16) MeshletCullData {
    glm::vec4 boundingSphere;
    glm::vec4 normalCone;
    glm::vec4 aabbMin;  // For more precise culling
    glm::vec4 aabbMax;
};

} // namespace GPU

} // namespace Cortex::Graphics
