#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>
#include <functional>
#include <span>

namespace Cortex::Graphics::SSRPass {

constexpr uint32_t kDescriptorSlots = 10;

struct DrawContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* pipeline = nullptr;

    ID3D12Resource* target = nullptr;
    DescriptorHandle targetRtv{};

    ID3D12Resource* hdr = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* normalRoughness = nullptr;

    std::span<DescriptorHandle> srvTable{};
    DescriptorHandle shadowAndEnvironmentTable{};
};

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
    D3D12_RESOURCE_STATES desiredState = D3D12_RESOURCE_STATE_COMMON;
};

struct PrepareContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    bool skipTransitions = false;
    ResourceStateRef ssrTarget{};
    ResourceStateRef hdr{};
    ResourceStateRef normalRoughness{};
    ResourceStateRef depth{};
};

struct GraphContext {
    RGResourceHandle hdr;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    RGResourceHandle ssr;
    PrepareContext prepare;
    DrawContext draw;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool PrepareTargets(const PrepareContext& context);
[[nodiscard]] bool Draw(const DrawContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::SSRPass
