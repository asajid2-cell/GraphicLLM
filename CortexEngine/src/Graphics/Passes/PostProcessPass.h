#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>
#include <span>

namespace Cortex::Graphics::PostProcessPass {

struct DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* pipeline = nullptr;

    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE targetRtv{};
    std::span<DescriptorHandle> srvTable{};
    DescriptorHandle shadowAndEnvironmentTable{};
};

[[nodiscard]] bool Draw(const DrawContext& context);

} // namespace Cortex::Graphics::PostProcessPass
