#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_DESCRIPTOR_RANGE_FLAGS kDynamicDescriptorRangeFlags =
    static_cast<D3D12_DESCRIPTOR_RANGE_FLAGS>(
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

} // namespace
Result<void> VisibilityBufferRenderer::CreateRootSignatures() {
    // ========================================================================
    // Visibility Pass Root Signature
    // Matches VisibilityPass.hlsl:
    //   b0: ViewProjection matrix + mesh index (16 + 4 = 20 dwords)
    //   t0: Instance data (StructuredBuffer<VBInstanceData>)
    //   t2: Optional culling mask (ByteAddressBuffer)
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[3] = {};

        // b0: View-projection matrix (16 floats) + mesh index (1 uint) + padding (3 uints)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 20;  // 16 for matrix + 4 for (meshIdx + baseInstance + materialCount + cullMaskCount)
        // Pixel shader reads g_MaterialCount / g_CullMaskCount and may sample instance
        // data for primitive-ID normalization, so these must be visible to ALL stages.
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0: Instance buffer SRV (via descriptor)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t2: Optional culling mask SRV (raw buffer)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 2;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize visibility root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_visibilityRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create visibility root signature");
        }
    }

    // ========================================================================
    // Alpha-Tested Visibility Pass Root Signature
    // Matches VisibilityPass.hlsl PSMainAlphaTest:
    //   b0: ViewProjection matrix + mesh index + material count (20 dwords)
    //   t0: Instance data (StructuredBuffer<VBInstanceData>)
    //   t1: Material constants (StructuredBuffer<VBMaterialConstants>)
    //   t2: Optional culling mask (ByteAddressBuffer)
    //   s0: Linear wrap sampler for baseColor alpha test
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[4] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 20;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 2;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 1;
        rootDesc.Desc_1_1.pStaticSamplers = &sampler;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#ifdef ENABLE_BINDLESS
        rootDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize alpha-tested visibility root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_visibilityAlphaRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create alpha-tested visibility root signature");
        }
    }

    // ========================================================================
    // Material Resolve Root Signature (Compute)
    // Matches MaterialResolve.hlsl:
    //   b0: Resolution constants (width, height, rcpWidth, rcpHeight)
    //   t0: Visibility buffer SRV (Texture2D - needs descriptor table)
    //   t1: Instance data SRV (StructuredBuffer - can use root descriptor)
    //   t2: Depth buffer SRV (Texture2D - needs descriptor table)
    //   t3: Mesh table SRV (StructuredBuffer - can use root descriptor)
    //   t5: Material constants SRV (StructuredBuffer - can use root descriptor)
    //   u0-u5: G-buffer UAVs (RWTexture2D - need descriptor tables)
    //   s0: Linear wrap sampler for material textures
    // ========================================================================
    {
        // Descriptor ranges for texture SRVs and UAVs
        D3D12_DESCRIPTOR_RANGE1 srvRanges[2] = {};
        // t0: Visibility buffer
        srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[0].NumDescriptors = 1;
        srvRanges[0].BaseShaderRegister = 0;
        srvRanges[0].RegisterSpace = 0;
        srvRanges[0].Flags = kDynamicDescriptorRangeFlags;
        srvRanges[0].OffsetInDescriptorsFromTableStart = 0;

        // t2: Depth buffer
        srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[1].NumDescriptors = 1;
        srvRanges[1].BaseShaderRegister = 2;
        srvRanges[1].RegisterSpace = 0;
        srvRanges[1].Flags = kDynamicDescriptorRangeFlags;
        srvRanges[1].OffsetInDescriptorsFromTableStart = 1;

        // u0-u5: G-buffer UAVs (6 consecutive UAVs)
        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = kVBResolveGBufferUavSlots;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = kDynamicDescriptorRangeFlags;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[7] = {};

        // b0: Resolution constants + view-projection matrix + mesh index (4 + 16 + 4 = 24 dwords)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 24;  // 4 for resolution + 16 for mat4x4 + 4 for mesh index + padding
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1: Instance data SRV (StructuredBuffer - can use root descriptor)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 1;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0 + t2: Visibility buffer + depth buffer SRVs (descriptor table)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 2;
        params[2].DescriptorTable.pDescriptorRanges = srvRanges;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0-u5: G-buffer UAVs (descriptor table)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &uavRange;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t3: Mesh table (StructuredBuffer - root descriptor)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor.ShaderRegister = 3;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t5: Material constants buffer (StructuredBuffer - root descriptor)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[5].Descriptor.ShaderRegister = 5;
        params[5].Descriptor.RegisterSpace = 0;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // b4: Biome materials constants (CBV - root descriptor)
        params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[6].Descriptor.ShaderRegister = 4;  // b4
        params[6].Descriptor.RegisterSpace = 0;
        params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 7;
        rootDesc.Desc_1_1.pParameters = params;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootDesc.Desc_1_1.NumStaticSamplers = 1;
        rootDesc.Desc_1_1.pStaticSamplers = &sampler;
        // Required for SM6.6 bindless access via ResourceDescriptorHeap[].
        rootDesc.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize resolve root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_resolveRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create resolve root signature");
        }
    }

    // ========================================================================
    // VB Motion Vectors Root Signature (Compute)
    // Matches VBMotionVectors.hlsl:
    //   b0: Dispatch constants (width/height/rcp + meshCount)
    //   b1: FrameConstants (current + previous camera matrices)
    //   t0: Visibility buffer SRV (descriptor table)
    //   t1: Instance data SRV (root descriptor)
    //   t3: Mesh table SRV (root descriptor)
    //   u0: Velocity UAV (descriptor table)
    // ========================================================================
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = kDynamicDescriptorRangeFlags;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = kDynamicDescriptorRangeFlags;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[6] = {};

        // b0: small per-dispatch constants
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 8; // width/height/rcpW/rcpH + meshCount + padding
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // b1: FrameConstants CBV (root CBV)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].Descriptor.ShaderRegister = 1;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1: Instance data SRV (root SRV)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t3: Mesh table SRV (root SRV)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 3;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0: Visibility buffer SRV (descriptor table)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges = &srvRange;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0: Velocity UAV (descriptor table)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[5].DescriptorTable.NumDescriptorRanges = 1;
        params[5].DescriptorTable.pDescriptorRanges = &uavRange;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootDesc.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize motion vectors root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_motionVectorsRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create motion vectors root signature");
        }
    }

    // ========================================================================
    // Clustered Light Culling Root Signature (Compute)
    // Matches ClusteredLightCulling.hlsl:
    //   b0: view matrix + projection/screen/cluster params (root constants)
    //   t0: local lights (StructuredBuffer<Light>) as root SRV
    //   u0: cluster ranges (RWStructuredBuffer<uint2>) as root UAV
    //   u1: cluster indices (RWStructuredBuffer<uint>) as root UAV
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[4] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 28; // mat4 (16) + 3 vec4/uvec4 (12)
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].Descriptor.ShaderRegister = 1;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize clustered light culling root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_clusterRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create clustered light culling root signature");
        }
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
