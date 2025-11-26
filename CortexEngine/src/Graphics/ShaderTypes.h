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
    glm::vec4 tangent; // xyz = tangent, w = bitangent sign
    glm::vec2 texCoord;

    Vertex() = default;
    Vertex(glm::vec3 pos, glm::vec3 norm, glm::vec3 tan, glm::vec2 uv)
        : position(pos), normal(norm), tangent(glm::vec4(tan, 1.0f)), texCoord(uv) {}
};

// Per-object constant buffer (changes per draw call)
struct ObjectConstants {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;  // For lighting calculations
};

// Light data for forward lighting
struct Light {
    // xyz: position (for point/spot), w: type (0 = directional, 1 = point, 2 = spot)
    glm::vec4 position_type;
    // xyz: direction (for dir/spot, normalized), w: inner cone cos (spot)
    glm::vec4 direction_cosInner;
    // rgb: color * intensity, w: range (for point/spot)
    glm::vec4 color_range;
    // x: outer cone cos (spot), y: shadow index (if used), z,w: reserved
    glm::vec4 params;
};

// Shadow-only constants (for cascaded rendering)
struct ShadowConstants {
    glm::uvec4 cascadeIndex;
};

// Per-frame constant buffer (changes per frame)
struct FrameConstants {
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjectionMatrix;
    glm::vec4 cameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    glm::vec4 timeAndExposure;
    // rgb: ambient color * intensity, w unused
    glm::vec4 ambientColor;
    // Forward light list (currently up to 4 lights; light[0] is the sun)
    alignas(16) glm::uvec4 lightCount;
    alignas(16) Light lights[4];
    // Cascaded directional light view-projection matrices (we use first 3)
    alignas(16) glm::mat4 lightViewProjection[4];
    // x,y,z = cascade split depths in view space, w = far plane
    glm::vec4 cascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w = PCSS enabled (>0.5)
    glm::vec4 shadowParams;
    // x = debug view mode (0 = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo, 5 = cascade index), others reserved
    glm::vec4 debugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight, z = FXAA enabled (>0.5), w reserved
    glm::vec4 postParams;
};

// Material properties
struct MaterialConstants {
    glm::vec4 albedo;
    float metallic;
    float roughness;
    float ao;  // Ambient occlusion
    alignas(16) glm::uvec4 mapFlags; // x: albedo, y: normal, z: metallic, w: roughness
};

} // namespace Cortex::Graphics
