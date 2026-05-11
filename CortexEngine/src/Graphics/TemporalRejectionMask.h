#pragma once

#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Utils/Result.h"

#include <functional>
#include <memory>

namespace Cortex::Graphics {

class TemporalRejectionMask {
public:
    struct DispatchDesc {
        uint32_t width = 0;
        uint32_t height = 0;
        D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;

        DescriptorHandle depthSRV;
        DescriptorHandle normalRoughnessSRV;
        DescriptorHandle velocitySRV;
        DescriptorHandle outputUAV;
        ID3D12Resource* depthResource = nullptr;
        ID3D12Resource* normalRoughnessResource = nullptr;
        ID3D12Resource* velocityResource = nullptr;
        ID3D12Resource* outputResource = nullptr;
        DescriptorHandle srvTable;
        DescriptorHandle uavTable;
    };

    struct StatsDispatchDesc {
        uint32_t width = 0;
        uint32_t height = 0;
        DescriptorHandle maskSRV;
        DescriptorHandle statsUAV;
        ID3D12Resource* maskResource = nullptr;
        ID3D12Resource* statsResource = nullptr;
        DescriptorHandle srvTable;
        DescriptorHandle uavTable;
    };

    struct ResourceStateRef {
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES* state = nullptr;
    };

    struct PrepareResourcesContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        ResourceStateRef depth;
        ResourceStateRef normalRoughness;
        ResourceStateRef velocity;
        ResourceStateRef output;
        D3D12_RESOURCE_STATES depthSampleState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES shaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        bool skipTransitions = false;
    };

    struct StatsResourcesContext {
        ID3D12GraphicsCommandList* commandList = nullptr;
        ResourceStateRef mask;
        ResourceStateRef statsBuffer;
        ID3D12Resource* statsReadback = nullptr;
        D3D12_RESOURCE_STATES shaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        uint64_t copyBytes = 0;
    };

    struct GraphContext {
        RGResourceHandle depth;
        RGResourceHandle normalRoughness;
        RGResourceHandle velocity;
        RGResourceHandle mask;
        std::function<bool()> dispatch;
        std::function<void(const char*)> failStage;
    };

    Result<void> Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] bool Dispatch(ID3D12GraphicsCommandList* cmdList,
                                ID3D12Device* device,
                                DescriptorHeapManager* descriptorManager,
                                const DispatchDesc& desc) const;
    [[nodiscard]] bool DispatchStats(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12Device* device,
                                     DescriptorHeapManager* descriptorManager,
                                     const StatsDispatchDesc& desc) const;
    [[nodiscard]] static bool PrepareDispatchResources(const PrepareResourcesContext& context);
    [[nodiscard]] static bool FinalizeDispatchResources(ID3D12GraphicsCommandList* commandList,
                                                        const ResourceStateRef& output,
                                                        D3D12_RESOURCE_STATES shaderResourceState,
                                                        bool skipTransitions);
    [[nodiscard]] static bool PrepareStatsResources(const StatsResourcesContext& context);
    [[nodiscard]] static bool FinalizeStatsReadback(const StatsResourcesContext& context);
    [[nodiscard]] static RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

private:
    ID3D12RootSignature* m_rootSignature = nullptr;
    std::unique_ptr<DX12ComputePipeline> m_pipeline;
    std::unique_ptr<DX12ComputePipeline> m_statsClearPipeline;
    std::unique_ptr<DX12ComputePipeline> m_statsReducePipeline;
};

} // namespace Cortex::Graphics
