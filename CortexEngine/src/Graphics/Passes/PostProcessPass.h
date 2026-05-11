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

struct DescriptorUpdateContext {
    ID3D12Device* device = nullptr;
    std::span<DescriptorHandle> srvTable{};
    ID3D12Resource* hdr = nullptr;
    float bloomIntensity = 0.0f;
    ID3D12Resource* bloomOverride = nullptr;
    ID3D12Resource* bloomFallback = nullptr;
    ID3D12Resource* ssao = nullptr;
    ID3D12Resource* history = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* normalRoughness = nullptr;
    ID3D12Resource* hzb = nullptr;
    uint32_t hzbMipCount = 1;
    bool wantsHzbDebug = false;
    ID3D12Resource* ssr = nullptr;
    ID3D12Resource* velocity = nullptr;
    ID3D12Resource* rtReflection = nullptr;
    ID3D12Resource* rtReflectionHistory = nullptr;
    ID3D12Resource* emissiveMetallic = nullptr;
    ID3D12Resource* materialExt1 = nullptr;
    ID3D12Resource* materialExt2 = nullptr;
};

[[nodiscard]] bool UpdateDescriptorTable(const DescriptorUpdateContext& context);
[[nodiscard]] bool Draw(const DrawContext& context);

} // namespace Cortex::Graphics::PostProcessPass
