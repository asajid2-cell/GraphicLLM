#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct DepthTargetResources {
    ComPtr<ID3D12Resource> buffer;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        buffer.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct DepthTargetDescriptors {
    DescriptorHandle dsv;
    DescriptorHandle readOnlyDsv;
    DescriptorHandle srv;

    void Reset() {
        dsv = {};
        readOnlyDsv = {};
        srv = {};
    }
};

struct DepthTargetState {
    DepthTargetResources resources;
    DepthTargetDescriptors descriptors;

    void ResetResources() {
        resources.Reset();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
