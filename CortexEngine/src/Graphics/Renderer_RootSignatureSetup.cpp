#include "Renderer.h"

#include "Graphics/Passes/IndirectMeshDrawPass.h"

#include <memory>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_DESCRIPTOR_RANGE_FLAGS kDynamicDescriptorRangeFlags =
    static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

} // namespace

Result<void> Renderer::CreateRendererRootSignaturesAndComputePasses() {
    m_pipelineState.rootSignature = std::make_unique<DX12RootSignature>();
    auto rsResult = m_pipelineState.rootSignature->Initialize(m_services.device->GetDevice());
    if (rsResult.IsErr()) {
        return Result<void>::Err("Failed to create root signature: " + rsResult.Error());
    }
    if (m_services.gpuCulling) {
        auto sigResult = IndirectMeshDrawPass::ConfigureCullingRootSignature(
            m_services.gpuCulling.get(),
            m_pipelineState.rootSignature->GetRootSignature());
        if (sigResult.IsErr()) {
            spdlog::warn("GPU Culling command signature setup failed: {}", sigResult.Error());
        }
    }

    m_pipelineState.computeRootSignature = std::make_unique<DX12ComputeRootSignature>();
    auto computeRsResult = m_pipelineState.computeRootSignature->Initialize(m_services.device->GetDevice());
    if (computeRsResult.IsErr()) {
        spdlog::warn("Failed to create compute root signature: {}", computeRsResult.Error());
        m_pipelineState.computeRootSignature.reset();
    } else {
        spdlog::info("Compute root signature created successfully");
    }

    if (m_pipelineState.computeRootSignature) {
        D3D12_ROOT_PARAMETER1 rootParameters[3] = {};

        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[0].Descriptor.ShaderRegister = 1;
        rootParameters[0].Descriptor.RegisterSpace = 0;
        rootParameters[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = kDynamicDescriptorRangeFlags;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = kDynamicDescriptorRangeFlags;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[2].DescriptorTable.pDescriptorRanges = &uavRange;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.ShaderRegister = 0;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = &samplerDesc;
        rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
#ifdef ENABLE_BINDLESS
        rootSignatureDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);
        if (SUCCEEDED(hr)) {
            hr = m_services.device->GetDevice()->CreateRootSignature(
                0,
                signature->GetBufferPointer(),
                signature->GetBufferSize(),
                IID_PPV_ARGS(&m_pipelineState.singleSrvUavComputeRootSignature));
        }
        if (FAILED(hr)) {
            m_pipelineState.singleSrvUavComputeRootSignature.Reset();
            const char* errorMsg = error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown";
            spdlog::warn("Failed to create compact 1-SRV/1-UAV compute root signature: {}", errorMsg);
        }
    }

    if (m_pipelineState.computeRootSignature) {
        m_services.temporalRejectionMask = std::make_unique<TemporalRejectionMask>();
        auto temporalMaskResult = m_services.temporalRejectionMask->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.computeRootSignature->GetRootSignature());
        if (temporalMaskResult.IsErr()) {
            spdlog::warn("Failed to initialize temporal rejection mask: {}", temporalMaskResult.Error());
            m_services.temporalRejectionMask.reset();
        }

        m_services.rtDenoiser = std::make_unique<RTDenoiser>();
        auto denoiserResult = m_services.rtDenoiser->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.computeRootSignature->GetRootSignature());
        if (denoiserResult.IsErr()) {
            spdlog::warn("Failed to initialize RT denoiser: {}", denoiserResult.Error());
            m_services.rtDenoiser.reset();
        }

        m_services.rtReflectionSignalStats = std::make_unique<RTReflectionSignalStats>();
        auto reflectionStatsResult = m_services.rtReflectionSignalStats->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.computeRootSignature->GetRootSignature());
        if (reflectionStatsResult.IsErr()) {
            spdlog::warn("Failed to initialize RT reflection signal stats: {}",
                         reflectionStatsResult.Error());
            m_services.rtReflectionSignalStats.reset();
        }
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
