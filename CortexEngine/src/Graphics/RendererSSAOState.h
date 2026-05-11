#pragma once

#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct SSAOResources {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        texture.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct SSAODescriptorTables {
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    bool descriptorTablesValid = false;

    void Reset() {
        descriptorTablesValid = false;
    }
};

struct SSAOControls {
    bool enabled = true;
    float radius = 0.25f;
    float bias = 0.03f;
    float intensity = 0.35f;
};

struct SSAOPassState {
    SSAOControls controls;
    SSAOResources resources;
    SSAODescriptorTables descriptors;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
