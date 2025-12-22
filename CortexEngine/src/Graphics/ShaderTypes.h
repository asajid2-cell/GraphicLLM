#pragma once

// Configure GLM for DirectX 12
#define GLM_ENABLE_EXPERIMENTAL     // Allow experimental extensions used transitively by GLM headers
#define GLM_FORCE_LEFT_HANDED       // DirectX uses left-handed coordinate system
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // DirectX uses [0,1] depth range (not [-1,1])

#include <glm/glm.hpp>
#include "RHI/BindlessConstants.h"

// Shared structures between C++ and HLSL shaders
// IMPORTANT: Alignment must match HLSL constant buffer rules (16-byte alignment)

namespace Cortex::Graphics {

// Keep this in sync with the g_Lights array size in the HLSL FrameConstants
// definitions (Basic.hlsl, PostProcess.hlsl, SSAO.hlsl, SSR.hlsl, MotionVectors.hlsl).
static constexpr uint32_t kMaxForwardLights = 16;

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
    // xyz: position (for point/spot/area), w: type (0 = directional, 1 = point, 2 = spot, 3 = rect area)
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
// Note: lightViewProjection includes 3 cascades for the directional sun and
// up to 3 additional local shadow-casting lights (total 6 matrices).
struct FrameConstants {
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjectionMatrix;
    glm::mat4 invProjectionMatrix;
    glm::vec4 cameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    glm::vec4 timeAndExposure;
    // rgb: ambient color * intensity, w unused
    glm::vec4 ambientColor;
// Forward light list (currently up to 4 lights; light[0] is the sun)
    alignas(16) glm::uvec4 lightCount;
    alignas(16) Light lights[kMaxForwardLights];
    // Directional + local light view-projection matrices:
    // indices 0-2: cascades for the sun
    // indices 3-5: shadowed local lights (spot)
    alignas(16) glm::mat4 lightViewProjection[6];
    // x,y,z = cascade split depths in view space, w = far plane
    glm::vec4 cascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w = PCSS enabled (>0.5)
    glm::vec4 shadowParams;
    // x = debug view mode (0 = shaded, 1 = normals, 2 = roughness, 3 = metallic,
    //                      4 = albedo, 5 = cascade index, 6 = debug screen,
    //                      7 = fractal height, 8 = IBL diffuse only,
    //                      9 = IBL specular only, 10 = env direction/UV,
    //                      11 = Fresnel (Fibl), 12 = specular mip,
    //                      13 = SSAO only, 14 = SSAO overlay,
    //                      15 = SSR only, 16 = SSR overlay,
    //                      17 = forward light debug,
    //                      18 = RT shadow mask debug,
    //                      19 = RT shadow history debug,
    //                      20 = RT reflection buffer debug (post-process),
    //                      21 = RT GI buffer debug,
    //                      22 = shaded with RT GI disabled,
    //                      23 = shaded with RT reflections disabled (SSR only),
    //                      24 = SDF debug / RT reflection ray direction (mode-dependent),
    //                      25 = TAA history weight debug,
    //                      26 = material layers debug (clear-coat / sheen / SSS),
    //                      27 = anisotropy debug,
    //                      28 = fog factor debug (post-process),
    //                      29 = water debug (height/slope/foam)),
    //     w = RT history valid (>0.5), y/z reserved
    glm::vec4 debugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight,
    // z = FXAA enabled (>0.5),
    // w = RT sun shadows enabled (>0.5)
    glm::vec4 postParams;
    // x = diffuse IBL intensity, y = specular IBL intensity,
    // z = IBL enabled (>0.5), w = environment index (0 = studio, 1 = sunset, 2 = night)
    glm::vec4 envParams;
    // x = warm tint (-1..1), y = cool tint (-1..1),
    // z = god-ray intensity scale, w reserved
    glm::vec4 colorGrade;
    // Exponential height fog parameters:
    // x = density, y = base height, z = height falloff, w = enabled (>0.5)
    glm::vec4 fogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    glm::vec4 aoParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution,
    // w = SSR enabled (>0.5) for the post-process debug overlay
    glm::vec4 bloomParams;
    // x = jitterX, y = jitterY, z = TAA blend factor, w = TAA enabled (>0.5)
    glm::vec4 taaParams;
    // Non-jittered view-projection for RT world-position reconstruction.
    glm::mat4 viewProjectionNoJitter;
    glm::mat4 invViewProjectionNoJitter;
    // Previous frame jittered view-projection and inverse of current
    glm::mat4 prevViewProjectionMatrix;
    glm::mat4 invViewProjectionMatrix;
    // Water/wave parameters:
    // waterParams0: x = base wave amplitude, y = base wave length,
    //               z = wave speed,          w = global water level (Y)
    // waterParams1: x = primary wave dir X,  y = primary wave dir Z,
    //               z = secondary amplitude, w = steepness (0..1)
    glm::vec4 waterParams0;
    glm::vec4 waterParams1;
};

// Material properties
struct MaterialConstants {
    glm::vec4 albedo;
    float metallic;
    float roughness;
    float ao;  // Ambient occlusion
    float _pad0;  // Padding for 16-byte alignment
    // Bindless texture indices for SM6.6 ResourceDescriptorHeap access
    // Use 0xFFFFFFFF for invalid/unused textures (shader checks this)
    alignas(16) glm::uvec4 textureIndices;  // x: albedo, y: normal, z: metallic, w: roughness
    alignas(16) glm::uvec4 mapFlags;        // x: albedo, y: normal, z: metallic, w: roughness (legacy, for transition)
    alignas(16) glm::vec4 fractalParams0;   // x=amplitude, y=frequency, z=octaves, w=useFractalNormal
    alignas(16) glm::vec4 fractalParams1;   // x=coordMode (0=UV,1=worldXZ), y=scaleX, z=scaleZ, w=reserved
    alignas(16) glm::vec4 fractalParams2;   // x=lacunarity, y=gain, z=warpStrength, w=noiseType (0=fbm,1=ridged,2=turb)
    // x = clear-coat intensity (0..1), y = clear-coat roughness (0..1),
    // z,w reserved for future layering parameters.
    alignas(16) glm::vec4 coatParams;
};

// Invalid bindless index sentinel - shaders check for this to use fallback.
// kInvalidBindlessIndex is defined in RHI/BindlessConstants.h

} // namespace Cortex::Graphics
