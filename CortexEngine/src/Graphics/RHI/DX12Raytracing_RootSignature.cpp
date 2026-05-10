#include "DX12Raytracing.h"

namespace Cortex::Graphics {

Result<void> DX12RaytracingContext::CreateGlobalRootSignature() {
    HRESULT hr = S_OK;

    // Build global root signature for the RT pipelines (shadows + reflections + GI).
    {
        D3D12_ROOT_PARAMETER rootParams[7] = {};

        // b0, space0: FrameConstants
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[0].Descriptor.ShaderRegister = 0;
        rootParams[0].Descriptor.RegisterSpace = 0;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0, space2: TLAS SRV
        D3D12_DESCRIPTOR_RANGE tlasRange{};
        tlasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        tlasRange.NumDescriptors = 1;
        tlasRange.BaseShaderRegister = 0;
        tlasRange.RegisterSpace = 2;
        tlasRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &tlasRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1, space2: depth SRV
        D3D12_DESCRIPTOR_RANGE depthRange{};
        depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors = 1;
        depthRange.BaseShaderRegister = 1;
        depthRange.RegisterSpace = 2;
        depthRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &depthRange;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0, space2: shadow mask / reflection / GI UAV
        D3D12_DESCRIPTOR_RANGE maskRange{};
        maskRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        maskRange.NumDescriptors = 1;
        maskRange.BaseShaderRegister = 0;
        maskRange.RegisterSpace = 2;
        maskRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[3].DescriptorTable.pDescriptorRanges = &maskRange;
        rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0-t6, space1: shadow map + IBL + RT buffers (matches Basic.hlsl)
        D3D12_DESCRIPTOR_RANGE shadowEnvRange{};
        shadowEnvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadowEnvRange.NumDescriptors = 7;
        shadowEnvRange.BaseShaderRegister = 0;
        shadowEnvRange.RegisterSpace = 1;
        shadowEnvRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[4].DescriptorTable.pDescriptorRanges = &shadowEnvRange;
        rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t2, space2: G-buffer normal/roughness SRV for proper reflection bounces
        D3D12_DESCRIPTOR_RANGE gbufferRanges[2]{};
        gbufferRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[0].NumDescriptors = 1;
        gbufferRanges[0].BaseShaderRegister = 2;
        gbufferRanges[0].RegisterSpace = 2;
        gbufferRanges[0].OffsetInDescriptorsFromTableStart = 0;
        gbufferRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[1].NumDescriptors = 1;
        gbufferRanges[1].BaseShaderRegister = 4;
        gbufferRanges[1].RegisterSpace = 2;
        gbufferRanges[1].OffsetInDescriptorsFromTableStart = 1;

        rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[5].DescriptorTable.NumDescriptorRanges = _countof(gbufferRanges);
        rootParams[5].DescriptorTable.pDescriptorRanges = gbufferRanges;
        rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t3, space2: compact material buffer keyed by TLAS InstanceID().
        rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        rootParams[6].Descriptor.ShaderRegister = 3;
        rootParams[6].Descriptor.RegisterSpace = 2;
        rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Static sampler (s0) for environment/IBL sampling.
        D3D12_STATIC_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 8;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.ShaderRegister = 0;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = _countof(rootParams);
        rsDesc.pParameters = rootParams;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &samplerDesc;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errorBlob;
        hr = D3D12SerializeRootSignature(
            &rsDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob,
            &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                spdlog::warn("DXR global root signature serialization failed: {}",
                             static_cast<const char*>(errorBlob->GetBufferPointer()));
            } else {
                spdlog::warn("DXR global root signature serialization failed (hr=0x{:08X})",
                             static_cast<unsigned int>(hr));
            }
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        hr = m_device5->CreateRootSignature(
            0,
            sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_rtGlobalRootSignature));
        if (FAILED(hr)) {
            spdlog::warn("DXR global root signature creation failed (hr=0x{:08X})",
                         static_cast<unsigned int>(hr));
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }
    }


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics