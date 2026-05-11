#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RendererDebugLineState.h"

namespace Cortex::Graphics::DebugLinePass {

struct DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    const DebugLineRenderState* state = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS objectConstants = 0;
    uint32_t vertexCount = 0;
    UINT vertexBytes = 0;
};

[[nodiscard]] bool Draw(const DrawContext& context);

} // namespace Cortex::Graphics::DebugLinePass
