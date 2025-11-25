#pragma once

// Configure GLM for DirectX 12
#define GLM_ENABLE_EXPERIMENTAL     // Allow experimental extensions used transitively by GLM headers
#define GLM_FORCE_LEFT_HANDED       // DirectX uses left-handed coordinate system
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // DirectX uses [0,1] depth range (not [-1,1])

#include <glm/glm.hpp>

// Shared structures between C++ and HLSL shaders
// IMPORTANT: Alignment must match HLSL constant buffer rules (16-byte alignment)

namespace Cortex::Graphics {

// Vertex input structure
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;

    Vertex() = default;
    Vertex(glm::vec3 pos, glm::vec3 norm, glm::vec2 uv)
        : position(pos), normal(norm), texCoord(uv) {}
};

// Per-object constant buffer (changes per draw call)
struct ObjectConstants {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;  // For lighting calculations
};

// Per-frame constant buffer (changes per frame)
struct FrameConstants {
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjectionMatrix;
    glm::vec4 cameraPosition;
    float time;
    float deltaTime;
    float _padding[2];  // Pad to 16-byte boundary
};

// Material properties
struct MaterialConstants {
    glm::vec4 albedo;
    float metallic;
    float roughness;
    float ao;  // Ambient occlusion
    float _padding;
};

} // namespace Cortex::Graphics
