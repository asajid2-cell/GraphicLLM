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
Result<void> VisibilityBufferRenderer::CreateDeferredLightingPipeline() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

    // Deferred Lighting Root Signature (Graphics - Fullscreen Pass)
    // Matches DeferredLighting.hlsl:
    //   b0: Lighting parameters (view matrices, sun, IBL, etc.)
    //   t0: G-buffer albedo SRV
    //   t1: G-buffer normal+roughness SRV
    //   t2: G-buffer emissive+metallic SRV
    //   t3: Depth buffer SRV
    //   t4: Material extension 0 SRV (clearcoat/IOR/specular)
    //   t5: Material extension 1 SRV (specularColor/transmission)
    //   t6: Material extension 2 SRV (surface class / anisotropy / sheen / SSS wrap)
    //   t7: Diffuse irradiance environment SRV (lat-long)
    //   t8: Specular prefiltered environment SRV (lat-long)
    //   t9: Shadow map array SRV (Texture2DArray)
    //   t10: BRDF LUT SRV (Texture2D<float2>)
    //   t11: RT diffuse GI SRV
    //   t12: Local lights (StructuredBuffer<Light>)
    //   t13: Cluster ranges (StructuredBuffer<uint2>)
    //   t14: Cluster light indices (StructuredBuffer<uint>)
    //   t15: GTAO/bent-normal texture
    //   s0: Linear sampler
    //   s1: Shadow sampler
    // ========================================================================
    {
        // Descriptor ranges for G-buffer SRVs plus GTAO (t0-t6, t15).
        D3D12_DESCRIPTOR_RANGE1 gbufferRanges[2] = {};
        gbufferRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[0].NumDescriptors = 7;
        gbufferRanges[0].BaseShaderRegister = 0;
        gbufferRanges[0].RegisterSpace = 0;
        gbufferRanges[0].Flags = kDynamicDescriptorRangeFlags;
        gbufferRanges[0].OffsetInDescriptorsFromTableStart = 0;
        gbufferRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRanges[1].NumDescriptors = 1;
        gbufferRanges[1].BaseShaderRegister = 15;
        gbufferRanges[1].RegisterSpace = 0;
        gbufferRanges[1].Flags = kDynamicDescriptorRangeFlags;
        gbufferRanges[1].OffsetInDescriptorsFromTableStart = 7;

        // Descriptor ranges for env + shadow + BRDF LUT + RTGI (t7-t11: 5 textures)
        D3D12_DESCRIPTOR_RANGE1 envShadowRange = {};
        envShadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        envShadowRange.NumDescriptors = kVBDeferredEnvShadowSrvSlots;
        envShadowRange.BaseShaderRegister = 7;
        envShadowRange.RegisterSpace = 0;
        envShadowRange.Flags = kDynamicDescriptorRangeFlags;
        envShadowRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[6] = {};

        // b0: Lighting parameters (root CBV descriptor - too large for inline constants)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t0-t6: G-buffer + depth + material extension SRVs
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = _countof(gbufferRanges);
        params[1].DescriptorTable.pDescriptorRanges = gbufferRanges;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t7-t11: Environment + shadow map + BRDF LUT + RTGI SRVs
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &envShadowRange;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t12: Local lights SRV (root descriptor)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 12;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t13: Cluster ranges SRV (root descriptor)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor.ShaderRegister = 13;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t14: Cluster light indices SRV (root descriptor)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[5].Descriptor.ShaderRegister = 14;
        params[5].Descriptor.RegisterSpace = 0;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Static samplers
        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

        // s0: Linear sampler
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;
        samplers[0].RegisterSpace = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // s1: Shadow sampler (manual PCF in shader)
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[1].ShaderRegister = 1;
        samplers[1].RegisterSpace = 0;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 6;
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 2;
        rootDesc.Desc_1_1.pStaticSamplers = samplers;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#ifdef ENABLE_BINDLESS
        rootDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

        ComPtr<ID3DBlob> signature, error;
        hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                spdlog::warn("Failed to serialize deferred lighting root signature: {}",
                    static_cast<const char*>(error->GetBufferPointer()));
            } else {
                spdlog::warn("Failed to serialize deferred lighting root signature (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            }
            return Result<void>::Ok();
        }

        hr = device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_deferredLightingRootSignature)
        );
        if (FAILED(hr)) {
            spdlog::warn("Failed to create deferred lighting root signature (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            return Result<void>::Ok();
        }
    }

    // ========================================================================
    // Deferred Lighting Pipeline (Graphics - Fullscreen)
    // ========================================================================
    {
        // Compile shaders
        auto deferredVS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "VSMain",
            "vs_6_6"
        );
        if (deferredVS.IsErr()) {
            spdlog::warn("Failed to compile deferred lighting VS: {}", deferredVS.Error());
            return Result<void>::Ok();
        }

        auto deferredPS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "PSMain",
            "ps_6_6"
        );
        if (deferredPS.IsErr()) {
            spdlog::warn("Failed to compile deferred lighting PS: {}", deferredPS.Error());
            return Result<void>::Ok();
        }

        // Create PSO
        D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
        deferredPsoDesc.pRootSignature = m_deferredLightingRootSignature.Get();
        deferredPsoDesc.VS = {deferredVS.Value().data.data(), deferredVS.Value().data.size()};
        deferredPsoDesc.PS = {deferredPS.Value().data.data(), deferredPS.Value().data.size()};

        deferredPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        deferredPsoDesc.SampleMask = UINT_MAX;

        deferredPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        deferredPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        deferredPsoDesc.RasterizerState.DepthClipEnable = TRUE;

        deferredPsoDesc.DepthStencilState.DepthEnable = FALSE;
        deferredPsoDesc.DepthStencilState.StencilEnable = FALSE;

        deferredPsoDesc.InputLayout = {nullptr, 0};
        deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        deferredPsoDesc.NumRenderTargets = 1;
        deferredPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
        deferredPsoDesc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&m_deferredLightingPipeline));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create deferred lighting PSO");
            return Result<void>::Ok();
        }

        spdlog::info("VisibilityBuffer: Deferred lighting pipeline created");
    }

    // ========================================================================
    // FullSceneLightingV3 Split Pipeline (Graphics - Fullscreen MRT)
    // ========================================================================
    {
        auto deferredVS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "VSMain",
            "vs_6_6"
        );
        if (deferredVS.IsErr()) {
            spdlog::warn("Failed to compile FullSceneLightingV3 VS: {}", deferredVS.Error());
            return Result<void>::Ok();
        }

        auto splitPS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "PSMainV3LightingSplit",
            "ps_6_6"
        );
        if (splitPS.IsErr()) {
            spdlog::warn("Failed to compile FullSceneLightingV3 PS: {}", splitPS.Error());
            return Result<void>::Ok();
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC splitPsoDesc = {};
        splitPsoDesc.pRootSignature = m_deferredLightingRootSignature.Get();
        splitPsoDesc.VS = {deferredVS.Value().data.data(), deferredVS.Value().data.size()};
        splitPsoDesc.PS = {splitPS.Value().data.data(), splitPS.Value().data.size()};

        for (UINT i = 0; i < 7; ++i) {
            splitPsoDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            splitPsoDesc.RTVFormats[i] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        }
        splitPsoDesc.SampleMask = UINT_MAX;

        splitPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        splitPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        splitPsoDesc.RasterizerState.DepthClipEnable = TRUE;

        splitPsoDesc.DepthStencilState.DepthEnable = FALSE;
        splitPsoDesc.DepthStencilState.StencilEnable = FALSE;

        splitPsoDesc.InputLayout = {nullptr, 0};
        splitPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        splitPsoDesc.NumRenderTargets = 7;
        splitPsoDesc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&splitPsoDesc, IID_PPV_ARGS(&m_fullSceneLightingV3Pipeline));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create FullSceneLightingV3 split PSO");
            return Result<void>::Ok();
        }

        spdlog::info("VisibilityBuffer: FullSceneLightingV3 split pipeline created");
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
