#pragma once

#include <array>
#include <cstdint>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/D3D12Includes.h"
#include "RHI/DescriptorHeap.h"
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

struct ExposureDispatchConstants {
    float manualExposureCompensation = 1.0f;
    float deltaTime = 1.0f / 60.0f;
    uint32_t width = 1;
    uint32_t height = 1;
};

struct ExposureStateGpuData {
    float exposure = 1.0f;
    float meteredLuminance = 0.18f;
    float targetExposure = 1.0f;
    float initialized = 0.0f;
};

struct RendererExposureState {
    ComPtr<ID3D12Resource> stateBuffer;
    ComPtr<ID3D12Resource> initUploadBuffer;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> constantBuffers{};
    std::array<ComPtr<ID3D12Resource>, kFrameCount> readbackBuffers{};
    std::array<ExposureDispatchConstants*, kFrameCount> mappedConstants{};

    std::array<DescriptorHandle, kFrameCount> srvTables{};
    std::array<DescriptorHandle, kFrameCount> uavTables{};
    std::array<bool, kFrameCount> readbackValid{};

    D3D12_RESOURCE_STATES stateBufferState = D3D12_RESOURCE_STATE_COMMON;
    bool descriptorsValid = false;
    bool initializedOnGpu = false;
    bool hasReadback = false;
    float adaptedExposure = 1.0f;
    float meteredLuminance = 0.18f;
    float targetExposure = 1.0f;

    void Reset() {
        stateBuffer.Reset();
        initUploadBuffer.Reset();
        for (auto& buffer : constantBuffers) {
            buffer.Reset();
        }
        for (auto& buffer : readbackBuffers) {
            buffer.Reset();
        }
        mappedConstants.fill(nullptr);
        srvTables.fill({});
        uavTables.fill({});
        readbackValid.fill(false);
        stateBufferState = D3D12_RESOURCE_STATE_COMMON;
        descriptorsValid = false;
        initializedOnGpu = false;
        hasReadback = false;
        adaptedExposure = 1.0f;
        meteredLuminance = 0.18f;
        targetExposure = 1.0f;
    }
};

} // namespace Cortex::Graphics
