#pragma once

#include "Graphics/RHI/D3D12Includes.h"

#include <cstddef>

namespace Cortex::Graphics::PostProcessTargetPass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
    D3D12_RESOURCE_STATES desiredState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};

struct PrepareContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    const ResourceStateRef* shaderResources = nullptr;
    size_t shaderResourceCount = 0;
    ID3D12Resource* backBuffer = nullptr;
    bool* backBufferUsedAsRenderTarget = nullptr;
};

[[nodiscard]] bool PrepareInputsAndBackBuffer(const PrepareContext& context);

} // namespace Cortex::Graphics::PostProcessTargetPass
