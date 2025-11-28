#include "Components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Cortex::Scene {

glm::mat4 TransformComponent::GetMatrix() const {
    return worldMatrix;
}

glm::mat4 TransformComponent::GetNormalMatrix() const {
    return normalMatrix;
}

glm::mat4 TransformComponent::GetLocalMatrix() const {
    glm::mat4 matrix = glm::mat4(1.0f);
    matrix = glm::translate(matrix, position);
    matrix = matrix * glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
    return matrix;
}

glm::mat4 CameraComponent::GetProjectionMatrix(float aspectRatio) const {
    // DirectX-style: left-handed with depth in [0,1]
    return glm::perspectiveLH_ZO(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

glm::mat4 CameraComponent::GetViewMatrix(const TransformComponent& transform) const {
    // Left-handed: look down +Z by default
    glm::vec3 forward = transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 up = transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);

    return glm::lookAtLH(transform.position, transform.position + forward, up);
}

} // namespace Cortex::Scene
