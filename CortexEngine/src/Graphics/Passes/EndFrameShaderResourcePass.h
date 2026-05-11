#pragma once

#include "Graphics/RHI/D3D12Includes.h"

#include <cstddef>

namespace Cortex::Graphics::EndFrameShaderResourcePass {

struct TransitionTarget {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct TransitionContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    const TransitionTarget* targets = nullptr;
    size_t targetCount = 0;
};

[[nodiscard]] bool TransitionToPixelShaderResources(const TransitionContext& context);

} // namespace Cortex::Graphics::EndFrameShaderResourcePass
