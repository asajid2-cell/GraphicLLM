#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct HZBResources {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle fullSRV;
    uint32_t mipCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    bool valid = false;

    void Reset() {
        texture.Reset();
        fullSRV = {};
        mipCount = 0;
        width = 0;
        height = 0;
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        valid = false;
    }
};

struct HZBDescriptorTables {
    std::vector<DescriptorHandle> mipSRVStaging;
    std::vector<DescriptorHandle> mipUAVStaging;
    std::array<std::vector<DescriptorHandle>, kFrameCount> dispatchSrvTables;
    std::array<std::vector<DescriptorHandle>, kFrameCount> dispatchUavTables;
    bool dispatchTablesValid = false;

    void Reset() {
        mipSRVStaging.clear();
        mipUAVStaging.clear();
        for (auto& table : dispatchSrvTables) {
            table.clear();
        }
        for (auto& table : dispatchUavTables) {
            table.clear();
        }
        dispatchTablesValid = false;
    }
};

struct HZBDebugControls {
    uint32_t debugMip = 0;
};

struct HZBCaptureState {
    glm::mat4 captureViewMatrix{1.0f};
    glm::mat4 captureViewProjMatrix{1.0f};
    glm::vec3 captureCameraPosWS{0.0f};
    glm::vec3 captureCameraForwardWS{0.0f, 0.0f, 1.0f};
    float captureNearPlane = 0.1f;
    float captureFarPlane = 1000.0f;
    uint64_t captureFrameCounter = 0;
    bool captureValid = false;

    void Reset() {
        captureValid = false;
        captureFrameCounter = 0;
    }
};

struct HZBPassState {
    HZBResources resources;
    HZBDescriptorTables descriptors;
    HZBDebugControls debug;
    HZBCaptureState capture;

    void ResetResources() {
        resources.Reset();
        descriptors.Reset();
        debug.debugMip = 0;
        capture.Reset();
    }
};

} // namespace Cortex::Graphics
