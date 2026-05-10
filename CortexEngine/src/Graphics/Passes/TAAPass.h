#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <functional>
#include <span>

namespace Cortex::Graphics::TAAPass {

struct ResolveContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* pipeline = nullptr;

    ID3D12Resource* viewportSource = nullptr;
    DescriptorHandle targetRtv{};
    std::span<DescriptorHandle> srvTable{};
    DescriptorHandle shadowAndEnvironmentTable{};
};

struct GraphContext {
    RGResourceHandle hdr;
    RGResourceHandle history;
    RGResourceHandle intermediate;
    RGResourceHandle velocity;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    bool seedOnly = false;
    std::function<bool()> seedHistory;
    std::function<bool()> resolve;
    std::function<bool()> copyToHDR;
    std::function<bool()> copyToHistory;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool Resolve(const ResolveContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::TAAPass
