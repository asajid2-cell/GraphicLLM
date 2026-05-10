#include "Graphics/RTDenoiser.h"

#include <array>
#include <string>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr uint32_t kSrvSlots = 11;
constexpr uint32_t kUavSlots = 4;
constexpr UINT kFrameConstantsRoot = 1;
constexpr UINT kSrvTableRoot = 3;
constexpr UINT kUavTableRoot = 6;

Result<std::unique_ptr<DX12ComputePipeline>> CreatePipeline(ID3D12Device* device,
                                                            ID3D12RootSignature* rootSignature,
                                                            const char* entryPoint) {
    auto shaderResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/RTDenoise.hlsl",
        entryPoint,
        "cs_5_1");
    if (shaderResult.IsErr()) {
        return Result<std::unique_ptr<DX12ComputePipeline>>::Err(
            std::string("failed to compile ") + entryPoint + ": " + shaderResult.Error());
    }

    auto pipeline = std::make_unique<DX12ComputePipeline>();
    auto pipelineResult = pipeline->Initialize(device, rootSignature, shaderResult.Value());
    if (pipelineResult.IsErr()) {
        return Result<std::unique_ptr<DX12ComputePipeline>>::Err(
            std::string("failed to create ") + entryPoint + " pipeline: " + pipelineResult.Error());
    }

    return Result<std::unique_ptr<DX12ComputePipeline>>::Ok(std::move(pipeline));
}

bool IsValidDispatch(const RTDenoiser::DispatchDesc& desc) {
    return desc.width > 0 &&
           desc.height > 0 &&
           desc.currentSRV.IsValid() &&
           desc.historySRV.IsValid() &&
           desc.depthSRV.IsValid() &&
           desc.normalRoughnessSRV.IsValid() &&
           desc.velocitySRV.IsValid() &&
           desc.temporalMaskSRV.IsValid() &&
           desc.historyUAV.IsValid() &&
           desc.srvTable.IsValid() &&
           desc.uavTable.IsValid() &&
           desc.frameConstants != 0;
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

void WriteTexture2DUAV(ID3D12Device* device, ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE dst) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = resource ? resource->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, dst);
}

} // namespace

Result<void> RTDenoiser::Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature) {
    if (!device || !rootSignature) {
        return Result<void>::Err("RTDenoiser requires a D3D12 device and compute root signature");
    }

    m_rootSignature = rootSignature;

    auto create = [&](std::unique_ptr<DX12ComputePipeline>& target, const char* entry) -> Result<void> {
        auto result = CreatePipeline(device, rootSignature, entry);
        if (result.IsErr()) {
            return Result<void>::Err(result.Error());
        }
        target = std::move(result.Value());
        return Result<void>::Ok();
    };

    if (auto result = create(m_shadowSeedPipeline, "ShadowSeedCS"); result.IsErr()) return result;
    if (auto result = create(m_shadowTemporalPipeline, "ShadowTemporalCS"); result.IsErr()) return result;
    if (auto result = create(m_reflectionSeedPipeline, "ReflectionSeedCS"); result.IsErr()) return result;
    if (auto result = create(m_reflectionTemporalPipeline, "ReflectionTemporalCS"); result.IsErr()) return result;
    if (auto result = create(m_giSeedPipeline, "GISeedCS"); result.IsErr()) return result;
    if (auto result = create(m_giTemporalPipeline, "GITemporalCS"); result.IsErr()) return result;

    spdlog::info("RT denoiser compute pipelines created successfully");
    return Result<void>::Ok();
}

bool RTDenoiser::IsReady() const {
    return m_rootSignature &&
           m_shadowSeedPipeline &&
           m_shadowTemporalPipeline &&
           m_reflectionSeedPipeline &&
           m_reflectionTemporalPipeline &&
           m_giSeedPipeline &&
           m_giTemporalPipeline;
}

ID3D12PipelineState* RTDenoiser::SelectPipeline(Signal signal, bool historyValid) const {
    switch (signal) {
    case Signal::Shadow:
        return historyValid ? m_shadowTemporalPipeline->GetPipelineState()
                            : m_shadowSeedPipeline->GetPipelineState();
    case Signal::Reflection:
        return historyValid ? m_reflectionTemporalPipeline->GetPipelineState()
                            : m_reflectionSeedPipeline->GetPipelineState();
    case Signal::GI:
        return historyValid ? m_giTemporalPipeline->GetPipelineState()
                            : m_giSeedPipeline->GetPipelineState();
    }
    return nullptr;
}

float RTDenoiser::AlphaForSignal(Signal signal, bool historyValid) const {
    if (!historyValid) {
        return 1.0f;
    }
    switch (signal) {
    case Signal::Shadow: return 0.20f;
    case Signal::Reflection: return 0.28f;
    case Signal::GI: return 0.12f;
    }
    return 1.0f;
}

RTDenoiser::DispatchResult RTDenoiser::Dispatch(ID3D12GraphicsCommandList* cmdList,
                                                ID3D12Device* device,
                                                DescriptorHeapManager* descriptorManager,
                                                const DispatchDesc& desc) const {
    DispatchResult result{};
    result.seededHistory = !desc.historyValid;
    result.usedHistory = desc.historyValid;
    result.usedDepthNormalRejection = true;
    result.usedVelocityReprojection = desc.historyValid;
    result.usedDisocclusionRejection = desc.historyValid;
    result.accumulationAlpha = AlphaForSignal(desc.signal, desc.historyValid);

    if (!IsReady() || !cmdList || !device || !descriptorManager || !IsValidDispatch(desc)) {
        return result;
    }

    ID3D12PipelineState* pipeline = SelectPipeline(desc.signal, desc.historyValid);
    if (!pipeline) {
        return result;
    }

    const DescriptorHandle srvBase = desc.srvTable;
    const DescriptorHandle uavBase = desc.uavTable;

    uint32_t currentSlot = 0;
    uint32_t historySlot = 1;
    uint32_t outputSlot = 0;
    switch (desc.signal) {
    case Signal::Shadow:
        currentSlot = 0;
        historySlot = 1;
        outputSlot = 0;
        break;
    case Signal::Reflection:
        currentSlot = 2;
        historySlot = 3;
        outputSlot = 1;
        break;
    case Signal::GI:
        currentSlot = 4;
        historySlot = 5;
        outputSlot = 2;
        break;
    }

    struct SrvWrite {
        uint32_t slot = 0;
        DescriptorHandle fallback;
        ID3D12Resource* resource = nullptr;
        DXGI_FORMAT fallbackFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    };
    const std::array<SrvWrite, 6> srvWrites{{
        {currentSlot, desc.currentSRV, desc.currentResource, DXGI_FORMAT_R8G8B8A8_UNORM},
        {historySlot,
         desc.historyValid ? desc.historySRV : desc.currentSRV,
         desc.historyValid ? desc.historyResource : desc.currentResource,
         DXGI_FORMAT_R8G8B8A8_UNORM},
        {6, desc.depthSRV, desc.depthResource, DXGI_FORMAT_R32_FLOAT},
        {7, desc.normalRoughnessSRV, desc.normalRoughnessResource, DXGI_FORMAT_R16G16B16A16_FLOAT},
        {8, desc.velocitySRV, desc.velocityResource, DXGI_FORMAT_R16G16_FLOAT},
        {9, desc.temporalMaskSRV, desc.temporalMaskResource, DXGI_FORMAT_R16G16B16A16_FLOAT},
    }};

    for (const SrvWrite& write : srvWrites) {
        if (write.slot >= kSrvSlots) {
            return result;
        }
        const DescriptorHandle dst = descriptorManager->GetCBV_SRV_UAVHandle(srvBase.index + write.slot);
        if (write.resource) {
            WriteTexture2DSRV(device, write.resource, dst.cpu, write.fallbackFormat);
        } else {
            device->CopyDescriptorsSimple(
                1,
                dst.cpu,
                write.fallback.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    if (outputSlot >= kUavSlots) {
        return result;
    }
    const DescriptorHandle uavDst = descriptorManager->GetCBV_SRV_UAVHandle(uavBase.index + outputSlot);
    if (desc.historyResource) {
        WriteTexture2DUAV(device, desc.historyResource, uavDst.cpu);
    } else {
        device->CopyDescriptorsSimple(
            1,
            uavDst.cpu,
            desc.historyUAV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12DescriptorHeap* heaps[] = { descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(m_rootSignature);
    cmdList->SetPipelineState(pipeline);
    cmdList->SetComputeRootConstantBufferView(kFrameConstantsRoot, desc.frameConstants);
    cmdList->SetComputeRootDescriptorTable(kSrvTableRoot, srvBase.gpu);
    cmdList->SetComputeRootDescriptorTable(kUavTableRoot, uavBase.gpu);

    const UINT dispatchX = (desc.width + 7u) / 8u;
    const UINT dispatchY = (desc.height + 7u) / 8u;
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    result.executed = true;
    return result;
}

} // namespace Cortex::Graphics
