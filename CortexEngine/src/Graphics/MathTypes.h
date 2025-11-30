#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Cortex::Graphics {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f}; // should be normalized
};

struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct OBB {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.5f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius{1.0f};
};

} // namespace Cortex::Graphics

