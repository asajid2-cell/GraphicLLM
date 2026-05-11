#include "Graphics/RTReflectionSignalStats.h"

#include "Graphics/Passes/DescriptorTable.h"

#include <array>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr uint32_t kSrvSlots = 11;
constexpr uint32_t kUavSlots = 4;
constexpr UINT kSrvTableRoot = 3;
constexpr UINT kUavTableRoot = 6;

bool IsValidDispatch(const RTReflectionSignalStats::DispatchDesc& desc) {
    return desc.width > 0 &&
           desc.height > 0 &&
           desc.reflectionSRV.IsValid() &&
           desc.statsUAV.IsValid() &&
           desc.srvTable.IsValid() &&
           desc.uavTable.IsValid();
}

void WriteRawBufferUAV(ID3D12Device* device,
                       ID3D12Resource* resource,
                       D3D12_CPU_DESCRIPTOR_HANDLE dst) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = RTReflectionSignalStats::kStatsWords;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, dst);
}

} // namespace

Result<void> RTReflectionSignalStats::Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature) {
    if (!device || !rootSignature) {
        return Result<void>::Err("RTReflectionSignalStats requires a D3D12 device and compute root signature");
    }

    auto clearShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/RTReflectionSignalStats.hlsl",
        "ClearRTReflectionSignalStatsCS",
        "cs_5_1");
    if (clearShader.IsErr()) {
        return Result<void>::Err("failed to compile RT reflection signal stats clear shader: " +
                                 clearShader.Error());
    }

    auto reduceShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/RTReflectionSignalStats.hlsl",
        "ReduceRTReflectionSignalStatsCS",
        "cs_5_1");
    if (reduceShader.IsErr()) {
        return Result<void>::Err("failed to compile RT reflection signal stats reduce shader: " +
                                 reduceShader.Error());
    }

    auto clearHistoryShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/RTReflectionSignalStats.hlsl",
        "ClearRTReflectionHistorySignalStatsCS",
        "cs_5_1");
    if (clearHistoryShader.IsErr()) {
        return Result<void>::Err("failed to compile RT reflection history signal stats clear shader: " +
                                 clearHistoryShader.Error());
    }

    auto reduceHistoryShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/RTReflectionSignalStats.hlsl",
        "ReduceRTReflectionHistorySignalStatsCS",
        "cs_5_1");
    if (reduceHistoryShader.IsErr()) {
        return Result<void>::Err("failed to compile RT reflection history signal stats reduce shader: " +
                                 reduceHistoryShader.Error());
    }

    auto clearPipeline = std::make_unique<DX12ComputePipeline>();
    auto clearResult = clearPipeline->Initialize(device, rootSignature, clearShader.Value());
    if (clearResult.IsErr()) {
        return Result<void>::Err("failed to create RT reflection signal stats clear pipeline: " +
                                 clearResult.Error());
    }

    auto reducePipeline = std::make_unique<DX12ComputePipeline>();
    auto reduceResult = reducePipeline->Initialize(device, rootSignature, reduceShader.Value());
    if (reduceResult.IsErr()) {
        return Result<void>::Err("failed to create RT reflection signal stats reduce pipeline: " +
                                 reduceResult.Error());
    }

    auto clearHistoryPipeline = std::make_unique<DX12ComputePipeline>();
    auto clearHistoryResult = clearHistoryPipeline->Initialize(device, rootSignature, clearHistoryShader.Value());
    if (clearHistoryResult.IsErr()) {
        return Result<void>::Err("failed to create RT reflection history signal stats clear pipeline: " +
                                 clearHistoryResult.Error());
    }

    auto reduceHistoryPipeline = std::make_unique<DX12ComputePipeline>();
    auto reduceHistoryResult = reduceHistoryPipeline->Initialize(device, rootSignature, reduceHistoryShader.Value());
    if (reduceHistoryResult.IsErr()) {
        return Result<void>::Err("failed to create RT reflection history signal stats reduce pipeline: " +
                                 reduceHistoryResult.Error());
    }

    m_rootSignature = rootSignature;
    m_clearPipeline = std::move(clearPipeline);
    m_reducePipeline = std::move(reducePipeline);
    m_clearHistoryPipeline = std::move(clearHistoryPipeline);
    m_reduceHistoryPipeline = std::move(reduceHistoryPipeline);
    spdlog::info("RT reflection signal stats compute pipelines created successfully");
    return Result<void>::Ok();
}

bool RTReflectionSignalStats::IsReady() const {
    return m_rootSignature &&
           m_clearPipeline &&
           m_reducePipeline &&
           m_clearHistoryPipeline &&
           m_reduceHistoryPipeline;
}

namespace {

constexpr D3D12_RESOURCE_STATES kReflectionSignalSrvState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

void TransitionResource(ID3D12GraphicsCommandList* cmdList,
                        ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES& state,
                        D3D12_RESOURCE_STATES desired) {
    if (!cmdList || !resource || state == desired) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = desired;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    state = desired;
}

void InsertUAVBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource) {
    if (!cmdList || !resource) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    cmdList->ResourceBarrier(1, &barrier);
}

} // namespace

bool RTReflectionSignalStats::PrepareCaptureResources(const CaptureResources& resources) {
    if (!resources.commandList ||
        !resources.reflectionResource ||
        !resources.reflectionState ||
        !resources.statsResource ||
        !resources.statsState) {
        return false;
    }

    TransitionResource(resources.commandList,
                       resources.reflectionResource,
                       *resources.reflectionState,
                       kReflectionSignalSrvState);
    TransitionResource(resources.commandList,
                       resources.statsResource,
                       *resources.statsState,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}

bool RTReflectionSignalStats::FinalizeCaptureReadback(const CaptureResources& resources) {
    if (!resources.commandList ||
        !resources.statsResource ||
        !resources.statsState ||
        !resources.readbackResource) {
        return false;
    }

    InsertUAVBarrier(resources.commandList, resources.statsResource);
    TransitionResource(resources.commandList,
                       resources.statsResource,
                       *resources.statsState,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    resources.commandList->CopyBufferRegion(
        resources.readbackResource,
        0,
        resources.statsResource,
        0,
        RTReflectionSignalStats::kStatsBytes);
    return true;
}

bool RTReflectionSignalStats::Dispatch(ID3D12GraphicsCommandList* cmdList,
                                       ID3D12Device* device,
                                       DescriptorHeapManager* descriptorManager,
                                       const DispatchDesc& desc) const {
    if (!IsReady() || !cmdList || !device || !descriptorManager || !IsValidDispatch(desc)) {
        return false;
    }

    const DescriptorHandle srvBase = desc.srvTable;
    const DescriptorHandle uavBase = desc.uavTable;
    const uint32_t slot = (desc.target == SignalTarget::History) ? 1u : 0u;

    if (slot >= kSrvSlots || slot >= kUavSlots) {
        return false;
    }

    const DescriptorHandle srvDst = descriptorManager->GetCBV_SRV_UAVHandle(srvBase.index + slot);
    if (desc.reflectionResource) {
        DescriptorTable::WriteTexture2DSRV(
            device,
            srvDst,
            desc.reflectionResource,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
    } else {
        device->CopyDescriptorsSimple(
            1,
            srvDst.cpu,
            desc.reflectionSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    const DescriptorHandle uavDst = descriptorManager->GetCBV_SRV_UAVHandle(uavBase.index + slot);
    if (desc.statsResource) {
        WriteRawBufferUAV(device, desc.statsResource, uavDst.cpu);
    } else {
        device->CopyDescriptorsSimple(
            1,
            uavDst.cpu,
            desc.statsUAV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12DescriptorHeap* heaps[] = { descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(m_rootSignature);
    cmdList->SetComputeRootDescriptorTable(kSrvTableRoot, srvBase.gpu);
    cmdList->SetComputeRootDescriptorTable(kUavTableRoot, uavBase.gpu);

    const DX12ComputePipeline* clearPipeline =
        (desc.target == SignalTarget::History) ? m_clearHistoryPipeline.get() : m_clearPipeline.get();
    const DX12ComputePipeline* reducePipeline =
        (desc.target == SignalTarget::History) ? m_reduceHistoryPipeline.get() : m_reducePipeline.get();

    cmdList->SetPipelineState(clearPipeline->GetPipelineState());
    cmdList->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER clearBarrier{};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = desc.statsResource;
    cmdList->ResourceBarrier(1, &clearBarrier);

    cmdList->SetPipelineState(reducePipeline->GetPipelineState());
    cmdList->Dispatch((desc.width + 7u) / 8u, (desc.height + 7u) / 8u, 1);
    return true;
}

} // namespace Cortex::Graphics
