#pragma once

#include <glm/glm.hpp>

namespace Cortex::Graphics {

struct FrameConstantCameraState {
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraForward{0.0f, 0.0f, 1.0f};
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float fovY = 0.0f;
    float invWidth = 1.0f;
    float invHeight = 1.0f;
    glm::mat4 viewProjectionNoJitter{1.0f};
};

} // namespace Cortex::Graphics
