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
Result<void> VisibilityBufferRenderer::CreateDebugBlitPipelines() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

    // Debug Blit Pipeline (Graphics)
    // ========================================================================

    // Compile blit shaders
    auto blitVS = ShaderCompiler::CompileFromFile(
        "assets/shaders/DebugBlitAlbedo.hlsl",
        "VSMain",
        "vs_6_6"
    );
    if (blitVS.IsErr()) {
        spdlog::warn("Failed to compile DebugBlitAlbedo VS: {}", blitVS.Error());
    }

    auto blitPS = ShaderCompiler::CompileFromFile(
        "assets/shaders/DebugBlitAlbedo.hlsl",
        "PSMain",
        "ps_6_6"
    );
    if (blitPS.IsErr()) {
        spdlog::warn("Failed to compile DebugBlitAlbedo PS: {}", blitPS.Error());
    }

    bool blitReady = blitVS.IsOk() && blitPS.IsOk();

    // Create blit root signature: t0 (albedo SRV), s0 (sampler)
    if (blitReady) {
        D3D12_DESCRIPTOR_RANGE1 srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = kDynamicDescriptorRangeFlags;

        D3D12_DESCRIPTOR_RANGE1 samplerRange = {};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.RegisterSpace = 0;
        samplerRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

        D3D12_ROOT_PARAMETER1 params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srvRange;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC blitRootDesc = {};
        blitRootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        blitRootDesc.Desc_1_1.NumParameters = 2;
        blitRootDesc.Desc_1_1.pParameters = params;
        blitRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        hr = D3D12SerializeVersionedRootSignature(&blitRootDesc, &signature, &error);
        if (FAILED(hr)) {
            spdlog::warn("Failed to serialize blit root signature");
            blitReady = false;
        } else {
            hr = device->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(),
                IID_PPV_ARGS(&m_blitRootSignature)
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create blit root signature");
                blitReady = false;
            }
        }
    }

    // Create blit PSO (fullscreen triangle, no depth)
    if (blitReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC blitPsoDesc = {};
        blitPsoDesc.pRootSignature = m_blitRootSignature.Get();
        blitPsoDesc.VS = { blitVS.Value().data.data(), blitVS.Value().data.size() };
        blitPsoDesc.PS = { blitPS.Value().data.data(), blitPS.Value().data.size() };
        blitPsoDesc.BlendState.RenderTarget[0] = {
            FALSE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL
        };
        blitPsoDesc.SampleMask = UINT_MAX;
        blitPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        blitPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        blitPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        blitPsoDesc.RasterizerState.DepthClipEnable = FALSE;
        blitPsoDesc.RasterizerState.MultisampleEnable = FALSE;
        blitPsoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        blitPsoDesc.DepthStencilState.DepthEnable = FALSE;
        blitPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        blitPsoDesc.DepthStencilState.StencilEnable = FALSE;
        blitPsoDesc.InputLayout = { nullptr, 0 }; // No input layout
        blitPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        blitPsoDesc.NumRenderTargets = 1;
        blitPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
        blitPsoDesc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&blitPsoDesc, IID_PPV_ARGS(&m_blitPipeline));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create blit PSO");
            blitReady = false;
        }
    }

    // Create dedicated sampler heap for blit sampler
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (blitReady) {
        hr = device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_blitSamplerHeap));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create blit sampler heap");
            blitReady = false;
        }
    }

    // Create linear sampler for blit
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    if (blitReady) {
        device->CreateSampler(&samplerDesc, m_blitSamplerHeap->GetCPUDescriptorHandleForHeapStart());
    }

    if (!blitReady) {
        m_blitPipeline.Reset();
        m_blitRootSignature.Reset();
        m_blitVisibilityPipeline.Reset();
        m_blitDepthPipeline.Reset();
        m_blitSamplerHeap.Reset();
    } else {
        spdlog::info("VisibilityBuffer: Debug blit pipeline created");

        // Optional: visibility buffer debug blit (uint2 payload -> color).
        {
            auto visVS = ShaderCompiler::CompileFromFile(
                "assets/shaders/DebugBlitVisibility.hlsl",
                "VSMain",
                "vs_6_6"
            );
            auto visPS = ShaderCompiler::CompileFromFile(
                "assets/shaders/DebugBlitVisibility.hlsl",
                "PSMain",
                "ps_6_6"
            );
            if (visVS.IsOk() && visPS.IsOk()) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
                pso.pRootSignature = m_blitRootSignature.Get();
                pso.VS = { visVS.Value().data.data(), visVS.Value().data.size() };
                pso.PS = { visPS.Value().data.data(), visPS.Value().data.size() };
                pso.BlendState.RenderTarget[0] = {
                    FALSE, FALSE,
                    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                    D3D12_LOGIC_OP_NOOP,
                    D3D12_COLOR_WRITE_ENABLE_ALL
                };
                pso.SampleMask = UINT_MAX;
                pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pso.RasterizerState.FrontCounterClockwise = FALSE;
                pso.RasterizerState.DepthClipEnable = FALSE;
                pso.DepthStencilState.DepthEnable = FALSE;
                pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                pso.DepthStencilState.StencilEnable = FALSE;
                pso.InputLayout = { nullptr, 0 };
                pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pso.NumRenderTargets = 1;
                pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                pso.SampleDesc.Count = 1;

                HRESULT hr2 = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_blitVisibilityPipeline));
                if (FAILED(hr2)) {
                    m_blitVisibilityPipeline.Reset();
                    spdlog::warn("VisibilityBuffer: failed to create visibility debug blit PSO");
                }
            } else {
                spdlog::warn("VisibilityBuffer: failed to compile visibility debug blit shader(s)");
            }
        }

        // Optional: depth buffer debug blit (float depth -> grayscale).
        {
            auto depthVS = ShaderCompiler::CompileFromFile(
                "assets/shaders/DebugBlitDepth.hlsl",
                "VSMain",
                "vs_6_6"
            );
            auto depthPS = ShaderCompiler::CompileFromFile(
                "assets/shaders/DebugBlitDepth.hlsl",
                "PSMain",
                "ps_6_6"
            );
            if (depthVS.IsOk() && depthPS.IsOk()) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
                pso.pRootSignature = m_blitRootSignature.Get();
                pso.VS = { depthVS.Value().data.data(), depthVS.Value().data.size() };
                pso.PS = { depthPS.Value().data.data(), depthPS.Value().data.size() };
                pso.BlendState.RenderTarget[0] = {
                    FALSE, FALSE,
                    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                    D3D12_LOGIC_OP_NOOP,
                    D3D12_COLOR_WRITE_ENABLE_ALL
                };
                pso.SampleMask = UINT_MAX;
                pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pso.RasterizerState.FrontCounterClockwise = FALSE;
                pso.RasterizerState.DepthClipEnable = FALSE;
                pso.DepthStencilState.DepthEnable = FALSE;
                pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                pso.DepthStencilState.StencilEnable = FALSE;
                pso.InputLayout = { nullptr, 0 };
                pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pso.NumRenderTargets = 1;
                pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
                pso.SampleDesc.Count = 1;

                HRESULT hr2 = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_blitDepthPipeline));
                if (FAILED(hr2)) {
                    m_blitDepthPipeline.Reset();
                    spdlog::warn("VisibilityBuffer: failed to create depth debug blit PSO");
                }
            } else {
                spdlog::warn("VisibilityBuffer: failed to compile depth debug blit shader(s)");
            }
        }
    }


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

