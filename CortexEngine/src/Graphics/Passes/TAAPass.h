#pragma once

#include "Graphics/Passes/TAACopyPass.h"
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
    ID3D12Resource* ssr = nullptr;
    ID3D12Resource* velocity = nullptr;
    ID3D12Resource* temporalMask = nullptr;
};

struct GraphContext {
    RGResourceHandle hdr;
    RGResourceHandle history;
    RGResourceHandle intermediate;
    RGResourceHandle velocity;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    bool seedOnly = false;
    TAACopyPass::HistoryCopyContext seedHistory;
    TAACopyPass::ResolveInputsContext resolveInputs;
    DescriptorUpdateContext resolveDescriptors;
    ResolveContext resolve;
    TAACopyPass::IntermediateCopyContext copyToHDR;
    TAACopyPass::HistoryCopyContext copyToHistory;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool UpdateResolveDescriptorTable(const DescriptorUpdateContext& context);
[[nodiscard]] bool Resolve(const ResolveContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::TAAPass
