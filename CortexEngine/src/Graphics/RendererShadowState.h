#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

template <uint32_t ShadowArraySize>
struct ShadowMapResources {
    ComPtr<ID3D12Resource> map;
    std::array<DescriptorHandle, ShadowArraySize> dsvs{};
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    bool initializedForEditor = false;

    void Reset() {
        map.Reset();
        srv = {};
        for (auto& dsv : dsvs) {
            dsv = {};
        }
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        initializedForEditor = false;
    }
};

struct ShadowMapRasterState {
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
};

template <uint32_t ShadowCascadeCount = 3>
struct ShadowMapControls {
    bool enabled = true;
    bool pcssEnabled = false;
    float mapSize = 2048.0f;
    float bias = 0.002f; // Increased for terrain (was 0.0005f)
    float pcfRadius = 1.5f;
    float cascadeSplitLambda = 0.5f;
    std::array<float, ShadowCascadeCount> cascadeResolutionScale{1.0f, 1.0f, 1.0f};
};

template <uint32_t ShadowArraySize, uint32_t ShadowCascadeCount = 3>
struct ShadowMapPassState {
    ShadowMapResources<ShadowArraySize> resources;
    ShadowMapRasterState raster;
    ShadowMapControls<ShadowCascadeCount> controls;

    void ResetResources() {
        resources.Reset();
    }
};

template <uint32_t ShadowCascadeCount = 3>
struct ShadowCascadeFrameState {
    float orthoRange = 20.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;

    glm::mat4 lightViewMatrix{1.0f};
    std::array<glm::mat4, ShadowCascadeCount> lightProjectionMatrices{};
    std::array<glm::mat4, ShadowCascadeCount> lightViewProjectionMatrices{};
    std::array<float, ShadowCascadeCount> cascadeSplits{};
};

} // namespace Cortex::Graphics
