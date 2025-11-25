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
};

} // namespace Cortex::Utils
