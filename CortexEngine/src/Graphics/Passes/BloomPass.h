#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>
#include <span>
#include <string>

namespace Cortex::Graphics::BloomPass {

struct FullscreenContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    const DescriptorHandle* srvTable = nullptr;
    uint32_t srvTableCount = 0;
    bool srvTableValid = false;
};

struct TargetContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* target = nullptr;
    DescriptorHandle targetRtv{};
};

[[nodiscard]] uint32_t BaseDownsampleSlot();
[[nodiscard]] uint32_t DownsampleChainSlot(uint32_t level);
[[nodiscard]] uint32_t BlurHSlot(uint32_t level, uint32_t stageLevels);
[[nodiscard]] uint32_t BlurVSlot(uint32_t level, uint32_t stageLevels);
[[nodiscard]] uint32_t CompositeSlot(uint32_t compositeIndex, uint32_t stageLevels);

[[nodiscard]] RGResourceDesc MakeTextureDesc(ID3D12Resource* resource, const std::string& name);

void SetFullscreenViewport(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource);
[[nodiscard]] bool BindAndClearTarget(const TargetContext& context);

[[nodiscard]] bool PrepareFullscreenState(const FullscreenContext& context);
[[nodiscard]] bool BindGraphTexture(const FullscreenContext& context,
                                    ID3D12Resource* source,
                                    const char* label,
                                    uint32_t tableSlot);
[[nodiscard]] bool EnsureGraphRTV(const FullscreenContext& context,
                                  ID3D12Resource* target,
                                  DescriptorHandle& cachedRtv);
[[nodiscard]] bool RenderFullscreen(const FullscreenContext& context,
                                    ID3D12Resource* target,
                                    DX12Pipeline* pipeline,
                                    ID3D12Resource* source,
                                    uint32_t sourceSlot,
                                    const char* label,
                                    DescriptorHandle& targetRtv);
[[nodiscard]] bool RenderComposite(const FullscreenContext& context,
                                   const RenderGraph& graph,
                                   RGResourceHandle targetHandle,
                                   std::span<const RGResourceHandle> bloomSources,
                                   uint32_t activeLevels,
                                   uint32_t stageLevels,
                                   DX12Pipeline* pipeline,
                                   DescriptorHandle& targetRtv);

} // namespace Cortex::Graphics::BloomPass
