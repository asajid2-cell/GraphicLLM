#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::CreateVisibilityPassPipelines() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;


    // ========================================================================
    // Phase 1: Visibility Pass Pipeline (Graphics)
    // ========================================================================

    // Compile VisibilityPass vertex shader
    auto vsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/VisibilityPass.hlsl",
        "VSMain",
        "vs_6_6"
    );
    if (vsResult.IsErr()) {
        return Result<void>::Err("Failed to compile VisibilityPass VS: " + vsResult.Error());
    }

    // Compile VisibilityPass pixel shader
    auto psResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/VisibilityPass.hlsl",
        "PSMain",
        "ps_6_6"
    );
    if (psResult.IsErr()) {
        return Result<void>::Err("Failed to compile VisibilityPass PS: " + psResult.Error());
    }

    // Input layout MUST match the actual Vertex structure. Do not hardcode
    // offsets/stride here; GLM type alignment can change field packing.
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, static_cast<UINT>(offsetof(Vertex, normal)),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, tangent)),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, static_cast<UINT>(offsetof(Vertex, texCoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Graphics pipeline state for visibility pass
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_visibilityRootSignature.Get();
    psoDesc.VS = { vsResult.Value().data.data(), vsResult.Value().data.size() };
    psoDesc.PS = { psResult.Value().data.data(), psResult.Value().data.size() };
    psoDesc.BlendState.RenderTarget[0] = {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    // Mesh generator creates CW-wound triangles for front faces (when viewed from front).
    // CW front-facing = FrontCounterClockwise FALSE.
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32_UINT;  // Visibility buffer format
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_visibilityPipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visibility pass PSO");
    }

    spdlog::info("VisibilityBuffer: Visibility pass pipeline created");

    // Double-sided visibility PSO (cull none) for glTF doubleSided materials.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dsDesc = psoDesc;
        dsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        hr = device->CreateGraphicsPipelineState(&dsDesc, IID_PPV_ARGS(&m_visibilityPipelineDoubleSided));
        if (FAILED(hr)) {
            spdlog::warn("VisibilityBuffer: Failed to create double-sided visibility PSO (falling back to cull-back)");
            m_visibilityPipelineDoubleSided.Reset();
        } else {
            spdlog::info("VisibilityBuffer: Double-sided visibility pipeline created");
        }
    }

    // Alpha-tested visibility pipeline (same VS, alpha-discard PS)
    {
        auto alphaPS = ShaderCompiler::CompileFromFile(
            "assets/shaders/VisibilityPass.hlsl",
            "PSMainAlphaTest",
            "ps_6_6"
        );
        if (alphaPS.IsErr()) {
            return Result<void>::Err("Failed to compile VisibilityPass PSMainAlphaTest: " + alphaPS.Error());
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaDesc = psoDesc;
        alphaDesc.pRootSignature = m_visibilityAlphaRootSignature.Get();
        alphaDesc.PS = { alphaPS.Value().data.data(), alphaPS.Value().data.size() };

        hr = device->CreateGraphicsPipelineState(&alphaDesc, IID_PPV_ARGS(&m_visibilityAlphaPipeline));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create alpha-tested visibility PSO");
        }

        spdlog::info("VisibilityBuffer: Alpha-tested visibility pipeline created");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaDsDesc = alphaDesc;
        alphaDsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        hr = device->CreateGraphicsPipelineState(&alphaDsDesc, IID_PPV_ARGS(&m_visibilityAlphaPipelineDoubleSided));
        if (FAILED(hr)) {
            spdlog::warn("VisibilityBuffer: Failed to create double-sided alpha-tested visibility PSO (falling back to cull-back)");
            m_visibilityAlphaPipelineDoubleSided.Reset();
        } else {
            spdlog::info("VisibilityBuffer: Double-sided alpha-tested visibility pipeline created");
        }
    }

    // ========================================================================
    // Phase 2: Material Resolve Pipeline (Compute)

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

