#include "DX12Pipeline.h"
#include "Utils/FileUtils.h"
#include <spdlog/spdlog.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#include <filesystem>
#include <unordered_set>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

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
    // Use topology type from desc (defaults to TRIANGLE, but debug lines use LINE)
    psoDesc.PrimitiveTopologyType = desc.primitiveTopologyType;
    psoDesc.NumRenderTargets = desc.numRenderTargets;
    for (UINT i = 0; i < 8; ++i) {
        psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }
    // Use the same RTV format for all active render targets in this pipeline.
    if (desc.numRenderTargets > 0) {
        for (UINT i = 0; i < desc.numRenderTargets && i < 8; ++i) {
            psoDesc.RTVFormats[i] = desc.rtvFormat;
        }
    }
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
    // We need: 4 CBVs (b0, b1, b2, b3) +
    //          descriptor table for material textures (t0-t3) +
    //          descriptor table for shadow/IBL/RT textures (space1) +
    //          descriptor table for UAVs (u0-u3) for async compute
    D3D12_ROOT_PARAMETER rootParameters[7] = {};

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

    // Parameter 3: Descriptor table for textures (t0 - t9 in space0)
    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange.NumDescriptors = 10;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 4: Shadow + IBL SRVs in a separate descriptor table/space
    //   space1, t0 = shadow map array
    //   space1, t1 = IBL diffuse irradiance
    //   space1, t2 = IBL specular prefiltered environment
    //   space1, t3 = RT sun shadow mask (optional)
    //   space1, t4 = RT sun shadow mask history (optional)
    //   space1, t5 = RT diffuse GI buffer (optional)
    //   space1, t6 = RT diffuse GI history buffer (optional)
    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 7;
    shadowRange.BaseShaderRegister = 0;
    shadowRange.RegisterSpace = 1;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &shadowRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Parameter 5: Shadow constants (b3)
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].Descriptor.ShaderRegister = 3;
    rootParameters[5].Descriptor.RegisterSpace = 0;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 6: UAV table for compute shaders (u0-u3 in space0)
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[6].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[6].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;  // Both pixel and compute

    // Static sampler (s0)
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
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
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;  // Both pixel and compute shaders

    // Create root signature
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

#ifdef ENABLE_BINDLESS
    // Enable bindless resources (SM6.6 ResourceDescriptorHeap[] access)
    rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

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
    namespace fs = std::filesystem;

    // Resolve shader path relative to common roots so running from either
    // the repo root or build/bin works without manual asset copying.
    fs::path requestedPath(filepath);
    fs::path resolvedPath = requestedPath;

    if (!Utils::FileExists(resolvedPath)) {
        fs::path cwd = fs::current_path();

        // Try current working directory + relative path
        fs::path candidate = cwd / requestedPath;
        if (Utils::FileExists(candidate)) {
            resolvedPath = candidate;
        } else {
            // Try one level up (e.g., running from build/)
            candidate = cwd.parent_path() / requestedPath;
            if (Utils::FileExists(candidate)) {
                resolvedPath = candidate;
            } else {
                // Try two levels up (e.g., running from build/bin)
                candidate = cwd.parent_path().parent_path() / requestedPath;
                if (Utils::FileExists(candidate)) {
                    resolvedPath = candidate;
                }
            }
        }
    }

    // Read file
    auto fileResult = Utils::ReadTextFile(resolvedPath);
    if (fileResult.IsErr()) {
        return Result<ShaderBytecode>::Err(fileResult.Error());
    }

    return CompileFromSource(fileResult.Value(), entryPoint, target);
}

// Helper function to compile with DXC (SM6.6+)
static Result<ShaderBytecode> CompileWithDXC(
    const std::string& source,
    const std::string& entryPoint,
    const std::string& target)
{
    // Create DXC compiler instance
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (FAILED(hr)) {
        return Result<ShaderBytecode>::Err("Failed to create DXC utils");
    }

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr)) {
        return Result<ShaderBytecode>::Err("Failed to create DXC compiler");
    }

    // Default include handler so shaders can use #include "foo.hlsli".
    ComPtr<IDxcIncludeHandler> includeHandler;
    hr = utils->CreateDefaultIncludeHandler(&includeHandler);
    if (FAILED(hr)) {
        return Result<ShaderBytecode>::Err("Failed to create DXC include handler");
    }

    // Convert source to wide string for DXC
    std::wstring wEntryPoint(entryPoint.begin(), entryPoint.end());
    std::wstring wTarget(target.begin(), target.end());

    // Create source buffer
    ComPtr<IDxcBlobEncoding> sourceBlob;
    hr = utils->CreateBlob(source.c_str(), static_cast<UINT32>(source.size()), CP_UTF8, &sourceBlob);
    if (FAILED(hr)) {
        return Result<ShaderBytecode>::Err("Failed to create DXC source blob");
    }

    // Build compiler arguments
    std::vector<LPCWSTR> arguments;
    arguments.push_back(L"-E");
    arguments.push_back(wEntryPoint.c_str());
    arguments.push_back(L"-T");
    arguments.push_back(wTarget.c_str());

    // Enable bindless resources for SM6.6
    arguments.push_back(L"-D");
    arguments.push_back(L"ENABLE_BINDLESS=1");

#ifdef _DEBUG
    arguments.push_back(L"-Zi");              // Debug info
    arguments.push_back(L"-Od");              // Disable optimizations
    arguments.push_back(L"-Qembed_debug");    // Embed debug info in shader
#else
    arguments.push_back(L"-O3");              // Maximum optimizations
#endif

    // Column-major matrices (matches GLM)
    arguments.push_back(L"-Zpc");

    // Include search paths. We support running from both the repo root and
    // build/bin, so probe a few likely locations relative to the current
    // working directory.
    std::vector<std::wstring> includeDirsStorage;
    {
        namespace fs = std::filesystem;
        fs::path cwd = fs::current_path();

        std::unordered_set<std::wstring> seen;
        includeDirsStorage.reserve(8);

        auto addIncludeDir = [&](const fs::path& dir) {
            if (dir.empty()) {
                return;
            }
            std::error_code ec;
            if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
                return;
            }
            std::wstring w = dir.wstring();
            if (seen.insert(w).second) {
                includeDirsStorage.push_back(std::move(w));
            }
        };

        const fs::path relA = fs::path("assets") / "shaders";
        const fs::path relB = fs::path("CortexEngine") / "assets" / "shaders";

        addIncludeDir(cwd / relA);
        addIncludeDir(cwd / relB);
        addIncludeDir(cwd.parent_path() / relA);
        addIncludeDir(cwd.parent_path() / relB);
        addIncludeDir(cwd.parent_path().parent_path() / relA);
        addIncludeDir(cwd.parent_path().parent_path() / relB);
    }

    for (const auto& dir : includeDirsStorage) {
        arguments.push_back(L"-I");
        arguments.push_back(dir.c_str());
    }

    // Create DXC buffer for compilation
    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = CP_UTF8;

    // Compile shader
    ComPtr<IDxcResult> result;
    hr = compiler->Compile(
        &sourceBuffer,
        arguments.data(),
        static_cast<UINT32>(arguments.size()),
        includeHandler.Get(),
        IID_PPV_ARGS(&result)
    );

    if (FAILED(hr)) {
        return Result<ShaderBytecode>::Err("DXC compilation failed");
    }

    // Get compilation status
    HRESULT compileStatus;
    result->GetStatus(&compileStatus);

    if (FAILED(compileStatus)) {
        // Get error messages
        ComPtr<IDxcBlobUtf8> errors;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            std::string errorMsg(errors->GetStringPointer(), errors->GetStringLength());
            return Result<ShaderBytecode>::Err("DXC shader compilation failed: " + errorMsg);
        }
        return Result<ShaderBytecode>::Err("DXC shader compilation failed with unknown error");
    }

    // Get compiled shader bytecode
    ComPtr<IDxcBlob> shaderBlob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (!shaderBlob) {
        return Result<ShaderBytecode>::Err("Failed to retrieve DXC shader bytecode");
    }

    ShaderBytecode bytecode;
    bytecode.data.resize(shaderBlob->GetBufferSize());
    memcpy(bytecode.data.data(), shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

    spdlog::info("Shader compiled with DXC: {} ({}) - SM6.6 bindless enabled", entryPoint, target);
    return Result<ShaderBytecode>::Ok(std::move(bytecode));
}

Result<ShaderBytecode> ShaderCompiler::CompileFromSource(
    const std::string& source,
    const std::string& entryPoint,
    const std::string& target)
{
    // Check if target requires SM6.6 (DXC compiler)
    bool useDXC = false;

#ifdef ENABLE_BINDLESS
    // When ENABLE_BINDLESS is defined, force SM6.6 compilation with DXC
    // Convert SM5.1 targets to SM6.6
    std::string dxcTarget = target;
    if (target.find("_5_") != std::string::npos) {
        // Replace _5_1 with _6_6
        size_t pos = target.find("_5_");
        dxcTarget = target.substr(0, pos) + "_6_6";
        useDXC = true;
    } else if (target.find("_6_") != std::string::npos) {
        // Already SM6.x
        useDXC = true;
    }
#endif

    if (useDXC) {
#ifdef ENABLE_BINDLESS
        return CompileWithDXC(source, entryPoint, dxcTarget);
#else
        return Result<ShaderBytecode>::Err("DXC compilation requested but ENABLE_BINDLESS not defined");
#endif
    }

    // Fall back to FXC (D3DCompile) for SM5.1
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

// ============================================================================
// DX12ComputeRootSignature implementation
// ============================================================================

Result<void> DX12ComputeRootSignature::Initialize(ID3D12Device* device) {
    if (!device) {
        return Result<void>::Err("Invalid device pointer");
    }

    // Same layout as graphics root signature but WITHOUT the input assembler flag
    D3D12_ROOT_PARAMETER rootParameters[7] = {};

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
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 3: Descriptor table for textures (t0 - t9 in space0)
    D3D12_DESCRIPTOR_RANGE descriptorRange = {};
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange.NumDescriptors = 10;
    descriptorRange.BaseShaderRegister = 0;
    descriptorRange.RegisterSpace = 0;
    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = &descriptorRange;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 4: Shadow + IBL SRVs (space1)
    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 7;
    shadowRange.BaseShaderRegister = 0;
    shadowRange.RegisterSpace = 1;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &shadowRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 5: Shadow constants (b3)
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[5].Descriptor.ShaderRegister = 3;
    rootParameters[5].Descriptor.RegisterSpace = 0;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Parameter 6: UAV table for compute shaders (u0-u3 in space0)
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[6].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[6].DescriptorTable.pDescriptorRanges = &uavRange;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler (s0)
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
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

    // Create root signature WITHOUT input assembler flag (compute doesn't use IA)
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 1;
    rootSignatureDesc.pStaticSamplers = &samplerDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;  // NO input assembler flag

#ifdef ENABLE_BINDLESS
    // Enable bindless resources (SM6.6 ResourceDescriptorHeap[] access)
    rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

    // Serialize and create
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

    if (FAILED(hr)) {
        if (error) {
            const char* errorMsg = static_cast<const char*>(error->GetBufferPointer());
            return Result<void>::Err("Failed to serialize compute root signature: " + std::string(errorMsg));
        }
        return Result<void>::Err("Failed to serialize compute root signature");
    }

    hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                      IID_PPV_ARGS(&m_rootSignature));

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create compute root signature");
    }

    return Result<void>::Ok();
}

// ============================================================================
// DX12ComputePipeline implementation
// ============================================================================

Result<void> DX12ComputePipeline::Initialize(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const ShaderBytecode& computeShader)
{
    if (!device || !rootSignature) {
        return Result<void>::Err("Invalid device or root signature");
    }

    if (computeShader.data.empty()) {
        return Result<void>::Err("Compute shader bytecode is empty");
    }

    // Create compute pipeline state
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature;
    psoDesc.CS = computeShader.GetBytecode();

    HRESULT hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create compute pipeline state (HRESULT: " + std::to_string(hr) + ")");
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
