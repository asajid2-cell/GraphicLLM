#pragma once

#include "Scene/Components.h"
#include "Scene/TerrainNoise.h"
#include <memory>

namespace Cortex::Utils {

// Procedural mesh generation utilities
class MeshGenerator {
public:
    // Generate a unit cube (1x1x1 centered at origin)
    static std::shared_ptr<Scene::MeshData> CreateCube();

    // Generate a plane
    static std::shared_ptr<Scene::MeshData> CreatePlane(float width = 1.0f, float height = 1.0f);

    // Generate a quad in the XY plane (useful for billboards and screens)
    static std::shared_ptr<Scene::MeshData> CreateQuad(float width = 1.0f, float height = 1.0f);

    // Generate a sphere
    static std::shared_ptr<Scene::MeshData> CreateSphere(float radius = 0.5f, uint32_t segments = 32);

    // Generate a cylinder
    static std::shared_ptr<Scene::MeshData> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

    // Generate a pyramid (square base)
    static std::shared_ptr<Scene::MeshData> CreatePyramid(float baseSize = 1.0f, float height = 1.0f);

    // Generate a cone
    static std::shared_ptr<Scene::MeshData> CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

    // Generate a torus (donut shape)
    static std::shared_ptr<Scene::MeshData> CreateTorus(float majorRadius = 0.5f, float minorRadius = 0.2f, uint32_t majorSegments = 32, uint32_t minorSegments = 16);

    // Generate a disk in the XZ plane
    static std::shared_ptr<Scene::MeshData> CreateDisk(float radius = 0.5f, uint32_t segments = 32);

    // Generate a simple capsule approximation (cylinder-based)
    static std::shared_ptr<Scene::MeshData> CreateCapsule(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);

    // Generate a thin rectangular prism that can be used as a "line" or
    // segment when oriented and scaled via TransformComponent.
    static std::shared_ptr<Scene::MeshData> CreateLine(float length = 1.0f, float thickness = 0.02f);

    // Terrain clipmap support: generate an XZ grid centered at origin with optional
    // inner-hole (ring) topology and boundary skirts. Intended for vertex-shader
    // displacement using an analytic height function.
    static std::shared_ptr<Scene::MeshData> CreateTerrainClipmapGrid(uint32_t gridDim, bool ring, bool skirts = true);

    // CPU-generated heightmap terrain chunk for the visibility buffer pipeline.
    static std::shared_ptr<Scene::MeshData> CreateTerrainHeightmapChunk(
        uint32_t gridDim,
        float chunkSize,
        int32_t chunkX,
        int32_t chunkZ,
        const Scene::TerrainNoiseParams& params,
        float skirtDepth = 80.0f
    );
};

} // namespace Cortex::Utils
