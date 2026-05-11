#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::VoxelPass {

struct DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle voxelGridSrv{};
    ID3D12Resource* backBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv{};
    uint32_t width = 0;
    uint32_t height = 0;
};

[[nodiscard]] bool Draw(const DrawContext& context);

} // namespace Cortex::Graphics::VoxelPass
