#include "GPUCulling.h"
#include <algorithm>
#include <cstring>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
Result<void> GPUCullingPipeline::PrepareAllCommandsForExecuteIndirect(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList) {
        return Result<void>::Err("PrepareAllCommandsForExecuteIndirect requires a valid command list");
    }
    if (!m_allCommandBuffer[m_frameIndex]) {
        return Result<void>::Err("All-commands buffer not initialized");
    }

    if (m_allCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_allCommandBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_allCommandState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_allCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::UpdateInstances(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<GPUInstanceData>& instances)
{
    if (instances.empty()) {
        m_totalInstances = 0;
        m_visibleCount = 0;
        m_debugStats = {};
        m_debugStats.enabled = m_debugEnabled;
        return Result<void>::Ok();
    }
    if (!cmdList) {
        return Result<void>::Err("UpdateInstances requires a valid command list");
    }
    if (!m_instanceBuffer[m_frameIndex] || !m_instanceUploadBuffer[m_frameIndex]) {
        return Result<void>::Err("Instance buffer not initialized");
    }

    if (instances.size() > m_maxInstances) {
        spdlog::warn("GPU Culling: Instance count {} exceeds max {}, truncating",
                     instances.size(), m_maxInstances);
    }

    m_totalInstances = static_cast<uint32_t>(std::min(instances.size(), static_cast<size_t>(m_maxInstances)));

    // Map and upload instance data to frame-indexed buffer
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_instanceUploadBuffer[m_frameIndex]->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map instance upload buffer");
    }

    memcpy(mappedData, instances.data(), m_totalInstances * sizeof(GPUInstanceData));
    m_instanceUploadBuffer[m_frameIndex]->Unmap(0, nullptr);

    const UINT64 copyBytes = m_totalInstances * sizeof(GPUInstanceData);
    if (copyBytes == 0) {
        return Result<void>::Ok();
    }

    if (m_instanceState[m_frameIndex] != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_instanceState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState[m_frameIndex] = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    cmdList->CopyBufferRegion(m_instanceBuffer[m_frameIndex].Get(), 0, m_instanceUploadBuffer[m_frameIndex].Get(), 0, copyBytes);

    if (m_instanceState[m_frameIndex] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_instanceBuffer[m_frameIndex].Get();
        barrier.Transition.StateBefore = m_instanceState[m_frameIndex];
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_instanceState[m_frameIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    return Result<void>::Ok();
}

Result<void> GPUCullingPipeline::UpdateIndirectCommands(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<IndirectCommand>& commands)
{
    if (!cmdList) {
        return Result<void>::Err("UpdateIndirectCommands requires a valid command list");
    }
    if (!m_allCommandBuffer[m_frameIndex] || !m_allCommandUploadBuffer[m_frameIndex]) {
        return Result<void>::Err("Indirect command buffer not initialized");
    }
    if (commands.empty()) {
        return Result<void>::Ok();
    }

    size_t commandCount = commands.size();
    if (commandCount > m_maxInstances) {
        spdlog::warn("GPU Culling: Command count {} exceeds max {}, truncating",
                     commandCount, m_maxInstances);
        commandCount = m_maxInstances;
    }

    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    HRESULT hr = m_allCommandUploadBuffer[m_frameIndex]->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map indirect command upload buffer");
    }

    memcpy(mappedData, commands.data(), commandCount * sizeof(IndirectCommand));
    m_allCommandUploadBuffer[m_frameIndex]->Unmap(0, nullptr);

    const UINT64 copyBytes = commandCount * sizeof(IndirectCommand);
    if (copyBytes > 0) {
        if (m_allCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_allCommandBuffer[m_frameIndex].Get();
            barrier.Transition.StateBefore = m_allCommandState[m_frameIndex];
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
            m_allCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        cmdList->CopyBufferRegion(m_allCommandBuffer[m_frameIndex].Get(), 0, m_allCommandUploadBuffer[m_frameIndex].Get(), 0, copyBytes);

        if (m_allCommandState[m_frameIndex] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_allCommandBuffer[m_frameIndex].Get();
            barrier.Transition.StateBefore = m_allCommandState[m_frameIndex];
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
            m_allCommandState[m_frameIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }

    const uint32_t cmdCount = static_cast<uint32_t>(commandCount);
    if (m_totalInstances != cmdCount) {
        m_totalInstances = std::min(m_totalInstances, cmdCount);
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

