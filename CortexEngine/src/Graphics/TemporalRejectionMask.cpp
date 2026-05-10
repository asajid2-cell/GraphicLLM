#include "Graphics/TemporalRejectionMask.h"

#include <array>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr uint32_t kSrvSlots = 11;
constexpr uint32_t kUavSlots = 4;
constexpr uint32_t kTemporalMaskStatsWords = 5;
constexpr UINT kFrameConstantsRoot = 1;
constexpr UINT kSrvTableRoot = 3;
constexpr UINT kUavTableRoot = 6;

bool IsValidDispatch(const TemporalRejectionMask::DispatchDesc& desc) {
    return desc.width > 0 &&
           desc.height > 0 &&
           desc.depthSRV.IsValid() &&
           desc.normalRoughnessSRV.IsValid() &&
           desc.velocitySRV.IsValid() &&
           desc.outputUAV.IsValid() &&
           desc.srvTable.IsValid() &&
           desc.uavTable.IsValid() &&
           desc.frameConstants != 0;
}

bool IsValidStatsDispatch(const TemporalRejectionMask::StatsDispatchDesc& desc) {
    return desc.width > 0 &&
           desc.height > 0 &&
           desc.maskSRV.IsValid() &&
           desc.statsUAV.IsValid() &&
           desc.srvTable.IsValid() &&
           desc.uavTable.IsValid();
}

void Fail(const TemporalRejectionMask::GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

bool IsValidGraphContext(const TemporalRejectionMask::GraphContext& context) {
    return context.depth.IsValid() &&
           context.normalRoughness.IsValid() &&
           context.velocity.IsValid() &&
           context.mask.IsValid() &&
           static_cast<bool>(context.dispatch);
}

DXGI_FORMAT SrvFormatForResource(ID3D12Resource* resource, DXGI_FORMAT fallback = DXGI_FORMAT_R8G8B8A8_UNORM) {
    if (!resource) {
        return fallback;
    }
    const DXGI_FORMAT format = resource->GetDesc().Format;
    return (format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_R32_TYPELESS)
        ? DXGI_FORMAT_R32_FLOAT
        : format;
}

void WriteTexture2DSRV(ID3D12Device* device,
                       ID3D12Resource* resource,
                       D3D12_CPU_DESCRIPTOR_HANDLE dst,
                       DXGI_FORMAT fallback = DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = SrvFormatForResource(resource, fallback);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(resource, &srvDesc, dst);
}

void WriteTexture2DUAV(ID3D12Device* device,
                       ID3D12Resource* resource,
                       D3D12_CPU_DESCRIPTOR_HANDLE dst,
                       DXGI_FORMAT fallback = DXGI_FORMAT_R8G8B8A8_UNORM) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = resource ? resource->GetDesc().Format : fallback;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, dst);
}

} // namespace

Result<void> TemporalRejectionMask::Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature) {
    if (!device || !rootSignature) {
        return Result<void>::Err("TemporalRejectionMask requires a D3D12 device and compute root signature");
    }

    auto shaderResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/TemporalRejectionMask.hlsl",
        "BuildTemporalRejectionMaskCS",
        "cs_5_1");
    if (shaderResult.IsErr()) {
        return Result<void>::Err("failed to compile temporal rejection mask shader: " + shaderResult.Error());
    }

    auto pipeline = std::make_unique<DX12ComputePipeline>();
    auto pipelineResult = pipeline->Initialize(device, rootSignature, shaderResult.Value());
    if (pipelineResult.IsErr()) {
        return Result<void>::Err("failed to create temporal rejection mask pipeline: " + pipelineResult.Error());
    }

    auto statsClearShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/TemporalRejectionMask.hlsl",
        "ClearTemporalMaskStatsCS",
        "cs_5_1");
    if (statsClearShader.IsErr()) {
        return Result<void>::Err("failed to compile temporal mask stats clear shader: " + statsClearShader.Error());
    }

    auto statsReduceShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/TemporalRejectionMask.hlsl",
        "ReduceTemporalMaskStatsCS",
        "cs_5_1");
    if (statsReduceShader.IsErr()) {
        return Result<void>::Err("failed to compile temporal mask stats reduce shader: " + statsReduceShader.Error());
    }

    auto statsClearPipeline = std::make_unique<DX12ComputePipeline>();
    auto statsClearResult = statsClearPipeline->Initialize(device, rootSignature, statsClearShader.Value());
    if (statsClearResult.IsErr()) {
        return Result<void>::Err("failed to create temporal mask stats clear pipeline: " + statsClearResult.Error());
    }

    auto statsReducePipeline = std::make_unique<DX12ComputePipeline>();
    auto statsReduceResult = statsReducePipeline->Initialize(device, rootSignature, statsReduceShader.Value());
    if (statsReduceResult.IsErr()) {
        return Result<void>::Err("failed to create temporal mask stats reduce pipeline: " + statsReduceResult.Error());
    }

    m_rootSignature = rootSignature;
    m_pipeline = std::move(pipeline);
    m_statsClearPipeline = std::move(statsClearPipeline);
    m_statsReducePipeline = std::move(statsReducePipeline);
    spdlog::info("Temporal rejection mask compute pipeline created successfully");
    return Result<void>::Ok();
}

bool TemporalRejectionMask::IsReady() const {
    return m_rootSignature && m_pipeline && m_statsClearPipeline && m_statsReducePipeline;
}

bool TemporalRejectionMask::Dispatch(ID3D12GraphicsCommandList* cmdList,
                                     ID3D12Device* device,
                                     DescriptorHeapManager* descriptorManager,
                                     const DispatchDesc& desc) const {
    if (!IsReady() || !cmdList || !device || !descriptorManager || !IsValidDispatch(desc)) {
        return false;
    }

    const DescriptorHandle srvBase = desc.srvTable;
    const DescriptorHandle uavBase = desc.uavTable;

    std::array<DescriptorHandle, kSrvSlots> srvs{};
    srvs.fill(desc.depthSRV);
    srvs[0] = desc.depthSRV;
    srvs[1] = desc.normalRoughnessSRV;
    srvs[2] = desc.velocitySRV;

    for (uint32_t i = 0; i < kSrvSlots; ++i) {
        const DescriptorHandle dst = descriptorManager->GetCBV_SRV_UAVHandle(srvBase.index + i);
        ID3D12Resource* source = desc.depthResource;
        if (i == 1) source = desc.normalRoughnessResource;
        if (i == 2) source = desc.velocityResource;
        if (source) {
            WriteTexture2DSRV(device, source, dst.cpu, i == 2 ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM);
        } else {
            device->CopyDescriptorsSimple(1, dst.cpu, srvs[i].cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    for (uint32_t i = 0; i < kUavSlots; ++i) {
        const DescriptorHandle dst = descriptorManager->GetCBV_SRV_UAVHandle(uavBase.index + i);
        if (desc.outputResource) {
            WriteTexture2DUAV(device, desc.outputResource, dst.cpu);
        } else {
            device->CopyDescriptorsSimple(1, dst.cpu, desc.outputUAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    ID3D12DescriptorHeap* heaps[] = { descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(m_rootSignature);
    cmdList->SetPipelineState(m_pipeline->GetPipelineState());
    cmdList->SetComputeRootConstantBufferView(kFrameConstantsRoot, desc.frameConstants);
    cmdList->SetComputeRootDescriptorTable(kSrvTableRoot, srvBase.gpu);
    cmdList->SetComputeRootDescriptorTable(kUavTableRoot, uavBase.gpu);

    cmdList->Dispatch((desc.width + 7u) / 8u, (desc.height + 7u) / 8u, 1);
    return true;
}

bool TemporalRejectionMask::DispatchStats(ID3D12GraphicsCommandList* cmdList,
                                          ID3D12Device* device,
                                          DescriptorHeapManager* descriptorManager,
                                          const StatsDispatchDesc& desc) const {
    if (!IsReady() || !cmdList || !device || !descriptorManager || !IsValidStatsDispatch(desc)) {
        return false;
    }

    const DescriptorHandle srvBase = desc.srvTable;
    const DescriptorHandle uavBase = desc.uavTable;

    for (uint32_t i = 0; i < kSrvSlots; ++i) {
        const DescriptorHandle dst = descriptorManager->GetCBV_SRV_UAVHandle(srvBase.index + i);
        if (desc.maskResource) {
            WriteTexture2DSRV(device, desc.maskResource, dst.cpu);
        } else {
            device->CopyDescriptorsSimple(1, dst.cpu, desc.maskSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    for (uint32_t i = 0; i < kUavSlots; ++i) {
        const DescriptorHandle dst = descriptorManager->GetCBV_SRV_UAVHandle(uavBase.index + i);
        if (desc.statsResource) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = kTemporalMaskStatsWords;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            device->CreateUnorderedAccessView(desc.statsResource, nullptr, &uavDesc, dst.cpu);
        } else {
            device->CopyDescriptorsSimple(1, dst.cpu, desc.statsUAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    ID3D12DescriptorHeap* heaps[] = { descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(m_rootSignature);
    cmdList->SetComputeRootDescriptorTable(kSrvTableRoot, srvBase.gpu);
    cmdList->SetComputeRootDescriptorTable(kUavTableRoot, uavBase.gpu);

    cmdList->SetPipelineState(m_statsClearPipeline->GetPipelineState());
    cmdList->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->SetPipelineState(m_statsReducePipeline->GetPipelineState());
    cmdList->Dispatch((desc.width + 7u) / 8u, (desc.height + 7u) / 8u, 1);
    return true;
}

RGResourceHandle TemporalRejectionMask::AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsValidGraphContext(context)) {
        Fail(context, "temporal_mask_graph_contract");
        return {};
    }

    graph.AddPass(
        "TemporalRejectionMask",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Compute);
            builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource);
            builder.Read(context.velocity, RGResourceUsage::ShaderResource);
            builder.Write(context.mask, RGResourceUsage::UnorderedAccess);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.dispatch || !context.dispatch()) {
                Fail(context, "temporal_mask_dispatch");
            }
        });

    return context.mask;
}

} // namespace Cortex::Graphics
