#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::RTHistoryCopyPass {

struct CopyContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* source = nullptr;
    D3D12_RESOURCE_STATES* sourceState = nullptr;
    ID3D12Resource* history = nullptr;
    D3D12_RESOURCE_STATES* historyState = nullptr;
};

[[nodiscard]] bool CopyToHistoryAndReturnToShaderResource(const CopyContext& context);

} // namespace Cortex::Graphics::RTHistoryCopyPass
