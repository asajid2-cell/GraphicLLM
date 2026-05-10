#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct DepthTargetState {
    ComPtr<ID3D12Resource> buffer;
    DescriptorHandle dsv;
    DescriptorHandle readOnlyDsv;
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    void ResetResources() {
        buffer.Reset();
        dsv = {};
        readOnlyDsv = {};
        srv = {};
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

} // namespace Cortex::Graphics
