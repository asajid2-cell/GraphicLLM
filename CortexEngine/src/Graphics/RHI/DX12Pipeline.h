#pragma once

#include "D3D12Includes.h"
#include <wrl/client.h>
#include <string>
#include <vector>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

// Shader bytecode container
struct ShaderBytecode {
    std::vector<uint8_t> data;

    D3D12_SHADER_BYTECODE GetBytecode() const {
        if (data.empty()) {
            return { nullptr, 0 };
        }
        return { data.data(), data.size() };
    }
};

// Pipeline configuration
struct PipelineDesc {
    ShaderBytecode vertexShader;
    ShaderBytecode pixelShader;

    // Input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

    // Render target formats
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_D32_FLOAT;
    UINT numRenderTargets = 1;

    // Rasterizer state
    D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE; // draw both sides to avoid accidental culling
    bool wireframe = false;

    // Depth state
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS;

    // Blend state
    bool blendEnabled = false;

    // Primitive topology type (TRIANGLE, LINE, POINT, etc.)
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
};

// Pipeline State Object wrapper
class DX12Pipeline {
public:
    DX12Pipeline() = default;
    ~DX12Pipeline() = default;

    DX12Pipeline(const DX12Pipeline&) = delete;
    DX12Pipeline& operator=(const DX12Pipeline&) = delete;
    DX12Pipeline(DX12Pipeline&&) = default;
    DX12Pipeline& operator=(DX12Pipeline&&) = default;

    // Create pipeline with root signature
    Result<void> Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        const PipelineDesc& desc
    );

    // Accessors
    [[nodiscard]] ID3D12PipelineState* GetPipelineState() const { return m_pipelineState.Get(); }

private:
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

// Root Signature wrapper (defines shader parameter layout)
class DX12RootSignature {
public:
    DX12RootSignature() = default;
    ~DX12RootSignature() = default;

    DX12RootSignature(const DX12RootSignature&) = delete;
    DX12RootSignature& operator=(const DX12RootSignature&) = delete;
    DX12RootSignature(DX12RootSignature&&) = default;
    DX12RootSignature& operator=(DX12RootSignature&&) = default;

    // Create a simple root signature for our basic shader
    // Layout: [CBV b0, CBV b1, CBV b2,
    //          DescriptorTable(SRV t0-t3),
    //          DescriptorTable(SRV t4-t6),
    //          CBV b3, StaticSampler s0]
    Result<void> Initialize(ID3D12Device* device);

    [[nodiscard]] ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
};

// Compute Root Signature wrapper (no input assembler, compatible with compute pipelines)
class DX12ComputeRootSignature {
public:
    DX12ComputeRootSignature() = default;
    ~DX12ComputeRootSignature() = default;

    DX12ComputeRootSignature(const DX12ComputeRootSignature&) = delete;
    DX12ComputeRootSignature& operator=(const DX12ComputeRootSignature&) = delete;
    DX12ComputeRootSignature(DX12ComputeRootSignature&&) = default;
    DX12ComputeRootSignature& operator=(DX12ComputeRootSignature&&) = default;

    // Create compute-compatible root signature (same layout as graphics but without IA flag)
    Result<void> Initialize(ID3D12Device* device);

    [[nodiscard]] ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
};

// Compute Pipeline State Object wrapper
class DX12ComputePipeline {
public:
    DX12ComputePipeline() = default;
    ~DX12ComputePipeline() = default;

    DX12ComputePipeline(const DX12ComputePipeline&) = delete;
    DX12ComputePipeline& operator=(const DX12ComputePipeline&) = delete;
    DX12ComputePipeline(DX12ComputePipeline&&) = default;
    DX12ComputePipeline& operator=(DX12ComputePipeline&&) = default;

    // Create compute pipeline with root signature and compute shader
    Result<void> Initialize(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        const ShaderBytecode& computeShader
    );

    // Accessors
    [[nodiscard]] ID3D12PipelineState* GetPipelineState() const { return m_pipelineState.Get(); }

private:
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

// Shader compiler helper
class ShaderCompiler {
public:
    // Compile HLSL from file
    static Result<ShaderBytecode> CompileFromFile(
        const std::string& filepath,
        const std::string& entryPoint,
        const std::string& target
    );

    // Compile HLSL from string
    static Result<ShaderBytecode> CompileFromSource(
        const std::string& source,
        const std::string& entryPoint,
        const std::string& target
    );
};

} // namespace Cortex::Graphics
