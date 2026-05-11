#pragma once

#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct SSRPassState {
    ComPtr<ID3D12Resource> color;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    bool srvTableValid = false;
    bool enabled = true;
    bool activeThisFrame = false;
    float maxDistance = 30.0f;
    float thickness = 0.20f;
    float strength = 1.0f;

    void ResetResources() {
        color.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        srvTableValid = false;
    }
};

} // namespace Cortex::Graphics
