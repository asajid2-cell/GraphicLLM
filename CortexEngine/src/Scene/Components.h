#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <memory>

namespace Cortex::Graphics {
    class DX12Texture;
}

namespace Cortex::Scene {

// Transform Component - Position, rotation, scale
struct TransformComponent {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion
    glm::vec3 scale = glm::vec3(1.0f);

    // Get transformation matrix
    [[nodiscard]] glm::mat4 GetMatrix() const;

    // Get normal matrix (for lighting)
    [[nodiscard]] glm::mat4 GetNormalMatrix() const;
};

// Tag Component - Semantic labels for AI context (Phase 4)
struct TagComponent {
    std::string tag;

    TagComponent() = default;
    explicit TagComponent(std::string t) : tag(std::move(t)) {}
};

// Mesh data
struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;
    std::vector<uint32_t> indices;

    // GPU buffer handles (to be filled by renderer)
    void* vertexBuffer = nullptr;  // ID3D12Resource*
    void* indexBuffer = nullptr;   // ID3D12Resource*
};

// Renderable Component - What to draw
struct RenderableComponent {
    std::shared_ptr<MeshData> mesh;
    std::shared_ptr<Graphics::DX12Texture> texture;

    // Material properties
    glm::vec4 albedoColor = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;

    // Visibility
    bool visible = true;
};

// Rotation Component - For spinning cube demo
struct RotationComponent {
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    float speed = 1.0f;  // Radians per second
};

// Camera Component
struct CameraComponent {
    float fov = 60.0f;  // Field of view in degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool isActive = true;

    // Get projection matrix
    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspectRatio) const;

    // Get view matrix (requires TransformComponent)
    [[nodiscard]] glm::mat4 GetViewMatrix(const TransformComponent& transform) const;
};

} // namespace Cortex::Scene
