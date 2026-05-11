#pragma once

#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct SSRResources {
    ComPtr<ID3D12Resource> color;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        color.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct SSRDescriptorTables {
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    bool srvTableValid = false;

    void Reset() {
        srvTableValid = false;
    }
};

struct SSRControls {
    bool enabled = true;
    float maxDistance = 30.0f;
    float thickness = 0.20f;
    float strength = 1.0f;
};

struct SSRFrameState {
    bool activeThisFrame = false;
};

struct SSRPassState {
    SSRControls controls;
    SSRResources resources;
    SSRDescriptorTables descriptors;
    SSRFrameState frame;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
