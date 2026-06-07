#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RenderGraph.h"

#include <span>

namespace Cortex::Graphics::LocalReflectionRadiancePass {

struct GraphStatus {
    bool* failed = nullptr;
    bool* ran = nullptr;
    const char** stage = nullptr;
};

struct ResourceHandles {
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt1;
    RGResourceHandle materialExt2;
    RGResourceHandle sceneColor;
};

struct DispatchContext {
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    DX12ComputePipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::span<DescriptorHandle> srvTable{};
    std::span<DescriptorHandle> uavTable{};
    ID3D12Resource* envSpecular = nullptr;
    DXGI_FORMAT envSpecularFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct GraphContext {
    ResourceHandles resources;
    DispatchContext dispatch;
    GraphStatus status;
};

[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::LocalReflectionRadiancePass
