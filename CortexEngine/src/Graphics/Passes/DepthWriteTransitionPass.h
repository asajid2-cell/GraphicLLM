#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::DepthWriteTransitionPass {

struct TransitionContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
};

[[nodiscard]] bool TransitionToDepthWrite(const TransitionContext& context);

} // namespace Cortex::Graphics::DepthWriteTransitionPass
