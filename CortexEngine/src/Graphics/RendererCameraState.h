#pragma once

#include <glm/glm.hpp>

namespace Cortex::Graphics {

struct RendererCameraFrameState {
    glm::vec3 positionWS{0.0f};
    glm::vec3 forwardWS{0.0f, 0.0f, 1.0f};
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Previous camera state used by temporal systems to detect large motion.
    glm::vec3 prevPositionWS{0.0f};
    glm::vec3 prevForwardWS{0.0f, 0.0f, 1.0f};
    bool hasPrevious = false;

    void ResetHistory() {
        prevPositionWS = glm::vec3(0.0f);
        prevForwardWS = glm::vec3(0.0f, 0.0f, 1.0f);
        hasPrevious = false;
    }
};

} // namespace Cortex::Graphics
