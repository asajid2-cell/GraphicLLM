#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Cortex::Graphics {
    class DX12Texture;
    struct MeshBuffers;
    struct MaterialGPUState;
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

    // GPU buffer handles (renderer-owned)
    std::shared_ptr<Cortex::Graphics::MeshBuffers> gpuBuffers;

    void ResetGPUResources() {
        gpuBuffers.reset();
    }
};

// Renderable Component - What to draw
struct RenderableComponent {
    std::shared_ptr<MeshData> mesh;
    struct MaterialTextures {
        std::shared_ptr<Cortex::Graphics::DX12Texture> albedo;
        std::shared_ptr<Cortex::Graphics::DX12Texture> normal;
        std::shared_ptr<Cortex::Graphics::DX12Texture> metallic;
        std::shared_ptr<Cortex::Graphics::DX12Texture> roughness;
        std::string albedoPath;
        std::string normalPath;
        std::string metallicPath;
        std::string roughnessPath;
        std::shared_ptr<Cortex::Graphics::MaterialGPUState> gpuState;
    } textures;

    // Material properties
    glm::vec4 albedoColor = glm::vec4(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    // Optional logical material preset (e.g. "chrome", "gold") used by LLM commands.
    std::string presetName;

    // Visibility
    bool visible = true;
};

// Rotation Component - For spinning cube demo
struct RotationComponent {
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    float speed = 1.0f;  // Radians per second
};

// Light types for forward lighting
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

// Light Component - Forward lighting sources
struct LightComponent {
    LightType type = LightType::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float range = 10.0f;          // For point/spot
    float innerConeDegrees = 20.0f; // For spot (ignored for others)
    float outerConeDegrees = 30.0f; // For spot
    bool castsShadows = false;    // Reserved for future shadowed lights
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
