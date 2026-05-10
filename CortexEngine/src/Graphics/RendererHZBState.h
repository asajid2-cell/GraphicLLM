#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct HZBPassState {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle fullSRV;
    std::vector<DescriptorHandle> mipSRVStaging;
    std::vector<DescriptorHandle> mipUAVStaging;
    std::array<std::vector<DescriptorHandle>, kFrameCount> dispatchSrvTables;
    std::array<std::vector<DescriptorHandle>, kFrameCount> dispatchUavTables;
    bool dispatchTablesValid = false;
    uint32_t mipCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    bool valid = false;
    uint32_t debugMip = 0;

    glm::mat4 captureViewMatrix{1.0f};
    glm::mat4 captureViewProjMatrix{1.0f};
    glm::vec3 captureCameraPosWS{0.0f};
    glm::vec3 captureCameraForwardWS{0.0f, 0.0f, 1.0f};
    float captureNearPlane = 0.1f;
    float captureFarPlane = 1000.0f;
    uint64_t captureFrameCounter = 0;
    bool captureValid = false;

    void ResetResources() {
        texture.Reset();
        fullSRV = {};
        mipSRVStaging.clear();
        mipUAVStaging.clear();
        for (auto& table : dispatchSrvTables) {
            table.clear();
        }
        for (auto& table : dispatchUavTables) {
            table.clear();
        }
        dispatchTablesValid = false;
        mipCount = 0;
        width = 0;
        height = 0;
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        valid = false;
        debugMip = 0;
        captureValid = false;
        captureFrameCounter = 0;
    }
};

} // namespace Cortex::Graphics
