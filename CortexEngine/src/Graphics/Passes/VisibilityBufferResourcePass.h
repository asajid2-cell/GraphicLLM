#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::VisibilityBufferResourcePass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

[[nodiscard]] bool PrepareDepthForVisibility(ID3D12GraphicsCommandList* commandList,
                                             const ResourceStateRef& depth);
[[nodiscard]] bool PrepareDepthForSampling(ID3D12GraphicsCommandList* commandList,
                                           const ResourceStateRef& depth);
[[nodiscard]] bool PrepareHZBForCulling(ID3D12GraphicsCommandList* commandList,
                                        const ResourceStateRef& hzb);

} // namespace Cortex::Graphics::VisibilityBufferResourcePass
