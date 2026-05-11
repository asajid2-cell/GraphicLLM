#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>
#include <span>

namespace Cortex::Graphics::SSAOPass {

struct GraphStatus {
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct GraphicsContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12Pipeline* pipeline = nullptr;

    ID3D12Resource* target = nullptr;
    DescriptorHandle targetRtv{};
    ID3D12Resource* depth = nullptr;
    std::span<DescriptorHandle> srvTable{};
};

struct ComputeContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DX12ComputePipeline* pipeline = nullptr;

    UINT frameConstantsRoot = 0;
    UINT srvTableRoot = 0;
    UINT uavTableRoot = 0;

    ID3D12Resource* target = nullptr;
    ID3D12Resource* depth = nullptr;
    std::span<DescriptorHandle> srvTable{};
    std::span<DescriptorHandle> uavTable{};
};

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
    D3D12_RESOURCE_STATES desiredState = D3D12_RESOURCE_STATE_COMMON;
};

struct PrepareContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    bool skipTransitions = false;
    ResourceStateRef depth{};
    ResourceStateRef target{};
};

struct GraphContext {
    RGResourceHandle depth;
    RGResourceHandle ssao;
    bool useCompute = false;
    PrepareContext prepare;
    GraphicsContext graphics;
    ComputeContext compute;
    GraphStatus status;
};

[[nodiscard]] bool PrepareGraphicsTargets(const PrepareContext& context);
[[nodiscard]] bool PrepareComputeTargets(const PrepareContext& context);
[[nodiscard]] bool FinishComputeTarget(const PrepareContext& context);
[[nodiscard]] bool DrawGraphics(const GraphicsContext& context);
[[nodiscard]] bool DispatchCompute(const ComputeContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::SSAOPass
