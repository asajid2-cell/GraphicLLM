#pragma once

#include "Scene/Components.h"
#include <memory>

namespace Cortex::Utils {

// Procedural mesh generation utilities
class MeshGenerator {
public:
    // Generate a unit cube (1x1x1 centered at origin)
    static std::shared_ptr<Scene::MeshData> CreateCube();

    // Generate a plane
    static std::shared_ptr<Scene::MeshData> CreatePlane(float width = 1.0f, float height = 1.0f);

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
};

} // namespace Cortex::Utils
