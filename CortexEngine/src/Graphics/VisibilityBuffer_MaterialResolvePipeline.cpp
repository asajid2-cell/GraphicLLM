#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::CreateMaterialResolvePipeline() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;


    // Compile MaterialResolve compute shader
    auto csResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/MaterialResolve.hlsl",
        "CSMain",
        "cs_6_6"
    );
    if (csResult.IsErr()) {
        return Result<void>::Err("Failed to compile MaterialResolve CS: " + csResult.Error());
    }

    // Compute pipeline state for material resolve
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = m_resolveRootSignature.Get();
    computePsoDesc.CS = { csResult.Value().data.data(), csResult.Value().data.size() };

    hr = device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_resolvePipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create material resolve PSO");
    }

    spdlog::info("VisibilityBuffer: Material resolve pipeline created");


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

