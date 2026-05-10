#pragma once

#include "GPUCulling.h"

namespace Cortex::Graphics {

// GPU culling constants must match assets/shaders/GPUCulling.hlsl.
// The explicit array fields avoid relying on glm::vec3 packing.
struct alignas(16) CullConstants {
    glm::mat4 viewProj;
    glm::vec4 frustumPlanes[6];
    float cameraPos[3];
    uint32_t instanceCount;
    glm::uvec4 occlusionParams0;
    glm::uvec4 occlusionParams1;
    glm::vec4 occlusionParams2;
    glm::vec4 occlusionParams3;
    glm::mat4 hzbViewMatrix;
    glm::mat4 hzbViewProjMatrix;
    glm::vec4 hzbCameraPos;
};

} // namespace Cortex::Graphics
