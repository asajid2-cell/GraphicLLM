#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <span>

namespace Cortex::Graphics::HZBPass {

struct GraphContext {
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12RootSignature* compactRootSignature = nullptr;
    DX12ComputeRootSignature* fallbackRootSignature = nullptr;
    DX12ComputePipeline* initPipeline = nullptr;
    DX12ComputePipeline* downsamplePipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::span<const DescriptorHandle> srvTable;
    std::span<const DescriptorHandle> uavTable;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
};

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct BuildContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ResourceStateRef depth{};
    ResourceStateRef hzb{};
    ID3D12RootSignature* compactRootSignature = nullptr;
    DX12ComputeRootSignature* fallbackRootSignature = nullptr;
    DX12ComputePipeline* initPipeline = nullptr;
    DX12ComputePipeline* downsamplePipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle depthSrv{};
    std::span<const DescriptorHandle> mipSrvStaging;
    std::span<const DescriptorHandle> mipUavStaging;
    std::span<const DescriptorHandle> dispatchSrvTable;
    std::span<const DescriptorHandle> dispatchUavTable;
    bool dispatchTablesValid = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
};

[[nodiscard]] bool BuildFromDepth(const BuildContext& context);

void AddFromDepth(RenderGraph& graph,
                  RGResourceHandle depthHandle,
                  RGResourceHandle hzbHandle,
                  const GraphContext& context);

} // namespace Cortex::Graphics::HZBPass
