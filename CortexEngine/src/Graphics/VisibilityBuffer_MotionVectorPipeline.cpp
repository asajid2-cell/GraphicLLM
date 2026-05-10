#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {
Result<void> VisibilityBufferRenderer::CreateMotionVectorPipeline() {
    ID3D12Device* device = m_device->GetDevice();
    HRESULT hr = S_OK;

    // Optional: VB Motion Vectors Pipeline (Compute)
    // ========================================================================
    {
        auto mvCs = ShaderCompiler::CompileFromFile(
            "assets/shaders/VBMotionVectors.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (mvCs.IsErr()) {
            spdlog::warn("Failed to compile VBMotionVectors CS: {}", mvCs.Error());
        } else {
            D3D12_COMPUTE_PIPELINE_STATE_DESC mvPso{};
            mvPso.pRootSignature = m_motionVectorsRootSignature.Get();
            mvPso.CS = { mvCs.Value().data.data(), mvCs.Value().data.size() };

            hr = device->CreateComputePipelineState(&mvPso, IID_PPV_ARGS(&m_motionVectorsPipeline));
            if (FAILED(hr)) {
                spdlog::warn("Failed to create VB motion vectors PSO");
                m_motionVectorsPipeline.Reset();
            } else {
                spdlog::info("VisibilityBuffer: Motion vectors pipeline created");
            }
        }
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

