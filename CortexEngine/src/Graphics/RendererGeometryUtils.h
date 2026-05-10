#pragma once

#include "Graphics/GPUCulling.h"
#include "Scene/Components.h"

#include <d3d12.h>
#include <glm/glm.hpp>

namespace Cortex::Graphics {

inline constexpr D3D12_RESOURCE_STATES kDepthSampleState =
    D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

struct AutoDepthSeparation {
    glm::vec3 worldOffset{0.0f};
    float depthBiasNdc = 0.0f;
};

[[nodiscard]] FrustumPlanes ExtractFrustumPlanesCPU(const glm::mat4& viewProj);
[[nodiscard]] bool SphereIntersectsFrustumCPU(const FrustumPlanes& frustum,
                                              const glm::vec3& center,
                                              float radius);
[[nodiscard]] float GetMaxWorldScale(const glm::mat4& worldMatrix);
[[nodiscard]] AutoDepthSeparation ComputeAutoDepthSeparationForThinSurfaces(
    const Scene::RenderableComponent& renderable,
    const glm::mat4& modelMatrix,
    uint32_t stableKey);
void ApplyAutoDepthOffset(glm::mat4& modelMatrix, const glm::vec3& offset);

} // namespace Cortex::Graphics
