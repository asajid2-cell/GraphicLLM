#include "GPUCulling.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
Result<void> GPUCullingPipeline::Initialize(
    DX12Device* device,
    DescriptorHeapManager* descriptorManager,
    DX12CommandQueue* commandQueue,
    uint32_t maxInstances)
{
    if (!device || !descriptorManager || !commandQueue) {
        return Result<void>::Err("Invalid parameters for GPU culling initialization");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_commandQueue = commandQueue;
    m_maxInstances = maxInstances;

    auto rootResult = CreateRootSignature();
    if (rootResult.IsErr()) {
        return rootResult;
    }

    auto pipelineResult = CreateComputePipeline();
    if (pipelineResult.IsErr()) {
        return pipelineResult;
    }

    auto bufferResult = CreateBuffers();
    if (bufferResult.IsErr()) {
        return bufferResult;
    }

    spdlog::info("GPU Culling Pipeline initialized (max {} instances)", m_maxInstances);
    return Result<void>::Ok();
}

void GPUCullingPipeline::Shutdown() {
    if (m_flushCallback) {
        m_flushCallback();
    }

    m_cullPipeline.Reset();
    m_rootSignature.Reset();
    m_commandSignature.Reset();
    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        m_instanceBuffer[i].Reset();
        m_instanceUploadBuffer[i].Reset();
        m_allCommandBuffer[i].Reset();
        m_allCommandUploadBuffer[i].Reset();
        m_visibleCommandBuffer[i].Reset();
        m_commandCountBuffer[i].Reset();
    }
    m_commandCountReadback.Reset();
    m_visibleCommandReadback.Reset();
    m_cullConstantBuffer.Reset();
    m_occlusionHistoryA.Reset();
    m_occlusionHistoryB.Reset();
    for (uint32_t i = 0; i < kGPUCullingFrameCount; ++i) {
        m_visibilityMaskBuffer[i].Reset();
    }
    m_dummyHzbTexture.Reset();

    spdlog::info("GPU Culling Pipeline shutdown");
}

} // namespace Cortex::Graphics

