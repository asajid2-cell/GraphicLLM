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

void AddFromDepth(RenderGraph& graph,
                  RGResourceHandle depthHandle,
                  RGResourceHandle hzbHandle,
                  const GraphContext& context);

} // namespace Cortex::Graphics::HZBPass
