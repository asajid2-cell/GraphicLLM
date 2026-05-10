#pragma once

#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct SSAOPassState {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    bool descriptorTablesValid = false;
    bool enabled = true;
    float radius = 0.25f;
    float bias = 0.03f;
    float intensity = 0.35f;

    void ResetResources() {
        texture.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        descriptorTablesValid = false;
    }
};

} // namespace Cortex::Graphics
