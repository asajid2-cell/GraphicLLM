#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::DepthPrepassTargetPass {

struct BindContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthDsv{};
    bool skipTransitions = false;
    bool clearDepth = true;
};

[[nodiscard]] bool BindAndClear(const BindContext& context);

} // namespace Cortex::Graphics::DepthPrepassTargetPass
