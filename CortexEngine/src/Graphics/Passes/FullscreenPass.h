#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>

namespace Cortex::Graphics::FullscreenPass {

struct GraphicsStateContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
};

void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList,
                           uint32_t width,
                           uint32_t height);
void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList,
                           ID3D12Resource* resource);

[[nodiscard]] bool BindGraphicsState(const GraphicsStateContext& context);
void DrawTriangle(ID3D12GraphicsCommandList* commandList);

} // namespace Cortex::Graphics::FullscreenPass
