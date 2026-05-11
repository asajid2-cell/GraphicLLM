#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::ForwardTargetBindingPass {

struct BindContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    DescriptorHandle hdrRtv{};
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthDsv{};
    DescriptorHandle readOnlyDepthDsv{};
    D3D12_RESOURCE_STATES readOnlyDepthState = D3D12_RESOURCE_STATE_DEPTH_READ;
};

[[nodiscard]] bool BindHdrAndDepthReadOnly(const BindContext& context);

} // namespace Cortex::Graphics::ForwardTargetBindingPass
