#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::MinimalFramePass {

struct ClearContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* backBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    bool* backBufferUsedAsRTThisFrame = nullptr;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

[[nodiscard]] bool ClearBackBuffer(const ClearContext& context);

} // namespace Cortex::Graphics::MinimalFramePass
