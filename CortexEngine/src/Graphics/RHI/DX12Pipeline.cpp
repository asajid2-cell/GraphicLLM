#include "DX12Pipeline.h"
#include "Utils/FileUtils.h"
#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace Cortex::Graphics {

// ========== DX12Pipeline Implementation ==========

Result<void> DX12Pipeline::Initialize(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const PipelineDesc& desc)
{
    if (!device || !rootSignature) {
        return Result<void>::Err("Invalid device or root signature");
    }

    // Create rasterizer state
    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = desc.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = desc.cullMode;
    // Our meshes use counter-clockwise winding for front faces
    rasterizerDesc.FrontCounterClockwise = TRUE;
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.DepthBiasClamp = 0.0f;
    rasterizerDesc.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Create blend state
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = desc.blendEnabled;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Create depth-stencil state
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = desc.depthTestEnabled;
    depthStencilDesc.DepthWriteMask = desc.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = desc.depthFunc;
    depthStencilDesc.StencilEnable = FALSE;

    // Create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.VS = desc.vertexShader.GetBytecode();
    psoDesc.PS = desc.pixelShader.GetBytecode();
    psoDesc.BlendState = blendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.InputLayout = { desc.inputLayout.data(), static_cast<UINT>(desc.inputLayout.size()) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = desc.rtvFormat;
    psoDesc.DSVFormat = desc.dsvFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create graphics pipeline state");
    }

    spdlog::info("Pipeline state created successfully");
    return Result<void>::Ok();
}

// ========== DX12RootSignature Implementation ==========

Result<void> DX12RootSignature::Initialize(ID3D12Device* device) {
    if (!device) {
        return Result<void>::Err("Invalid device pointer");
    }

    // Define root parameters
    // We need: 3 CBVs (b0, b1, b2) + 1 descriptor table for SRV (t0)
    D3D12_ROOT_PARAMETER rootParameters[4] = {};

    // Parameter 0: Object constants (b0)
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 1: Frame constants (b1)
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].Descriptor.RegisterSpace = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 2: Material constants (b2)
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 3: Descriptor table for texture (t0)
    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange.NumDescriptors = 1;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static sampler (s0)
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 16;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Create root signature
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize and create
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (FAILED(hr)) {
        if (error) {
            const char* errorMsg = static_cast<const char*>(error->GetBufferPointer());
            return Result<void>::Err("Failed to serialize root signature: " + std::string(errorMsg));
        }
        return Result<void>::Err("Failed to serialize root signature");
    }

    hr = device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create root signature");
    }

    spdlog::info("Root signature created successfully");
    return Result<void>::Ok();
}

// ========== ShaderCompiler Implementation ==========

Result<ShaderBytecode> ShaderCompiler::CompileFromFile(
    const std::string& filepath,
    const std::string& entryPoint,
    const std::string& target)
{
    // Read file
    auto fileResult = Utils::ReadTextFile(filepath);
    if (fileResult.IsErr()) {
        return Result<ShaderBytecode>::Err(fileResult.Error());
    }

    return CompileFromSource(fileResult.Value(), entryPoint, target);
}

Result<ShaderBytecode> ShaderCompiler::CompileFromSource(
    const std::string& source,
    const std::string& entryPoint,
    const std::string& target)
{
    // Use default column-major packing so CPU-side GLM matrices map directly
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        source.c_str(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        entryPoint.c_str(),
        target.c_str(),
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            const char* errorMsg = static_cast<const char*>(errorBlob->GetBufferPointer());
            return Result<ShaderBytecode>::Err("Shader compilation failed: " + std::string(errorMsg));
        }
        return Result<ShaderBytecode>::Err("Shader compilation failed with unknown error");
    }

    ShaderBytecode bytecode;
    bytecode.data.resize(shaderBlob->GetBufferSize());
    memcpy(bytecode.data.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

    spdlog::info("Shader compiled: {} ({})", entryPoint, target);
    return Result<ShaderBytecode>::Ok(std::move(bytecode));
}

} // namespace Cortex::Graphics
