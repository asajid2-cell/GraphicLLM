#pragma once

#include <array>
#include <cstdint>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/TemporalManager.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct TemporalMaskPassState {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> statsBuffer;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> statsReadback{};
    DescriptorHandle statsUAV;
    D3D12_RESOURCE_STATES statsState = D3D12_RESOURCE_STATE_COMMON;
    std::array<bool, kFrameCount> statsReadbackPending{};
    std::array<uint64_t, kFrameCount> statsSampleFrame{};

    std::array<std::array<DescriptorHandle, 11>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    std::array<std::array<DescriptorHandle, 11>, kFrameCount> statsSrvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> statsUavTables{};
    bool descriptorTablesValid = false;
    bool statsDescriptorTablesValid = false;
    bool builtThisFrame = false;

    void ResetResources() {
        texture.Reset();
        statsBuffer.Reset();
        for (auto& readback : statsReadback) {
            readback.Reset();
        }
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        statsState = D3D12_RESOURCE_STATE_COMMON;
        statsReadbackPending.fill(false);
        statsSampleFrame.fill(0);
        builtThisFrame = false;
    }
};

struct RendererTemporalHistoryState {
    TemporalManager manager;
};

} // namespace Cortex::Graphics
