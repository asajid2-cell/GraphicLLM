#include "DX12CommandQueue.h"
#include "DX12Device.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

DX12CommandQueue::~DX12CommandQueue() {
    Shutdown();
}

Result<void> DX12CommandQueue::Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type) {
    if (!device) {
        return Result<void>::Err("Invalid device pointer");
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = type;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command queue");
    }

    // Create fence for GPU-CPU synchronization
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create fence");
    }

    // Create event for fence signaling
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        return Result<void>::Err("Failed to create fence event");
    }

    spdlog::info("Command Queue initialized");
    return Result<void>::Ok();
}

void DX12CommandQueue::Shutdown() {
    // Ensure all GPU work is complete before cleanup, but only if the queue
    // was successfully created. This makes destruction safe even when
    // Initialize() failed part-way through.
    if (m_commandQueue && m_fence) {
        Flush();
    }

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_fence.Reset();
    m_commandQueue.Reset();
}

void DX12CommandQueue::ExecuteCommandList(ID3D12CommandList* commandList) {
    if (!m_commandQueue || !commandList) {
        return;
    }
    ID3D12CommandList* const commandLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
}

uint64_t DX12CommandQueue::Signal() {
    if (!m_commandQueue || !m_fence) {
        return 0;
    }

    uint64_t fenceValue = m_nextFenceValue++;
    HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr)) {
        spdlog::error("Failed to signal command queue fence: 0x{:08X}",
                      static_cast<unsigned int>(hr));
        return 0;
    }
    return fenceValue;
}

void DX12CommandQueue::WaitForFenceValue(uint64_t fenceValue) {
    if (fenceValue == 0 || !m_fence || !m_fenceEvent) {
        return;
    }

    if (IsFenceComplete(fenceValue)) {
        return;
    }

    // Schedule an event when the fence reaches the specified value
    HRESULT hr = m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
    if (FAILED(hr)) {
        spdlog::error("Failed to set fence completion event: 0x{:08X}",
                      static_cast<unsigned int>(hr));
        return;
    }

    // Wait for the event to be signaled
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void DX12CommandQueue::WaitForQueue(ID3D12Fence* otherFence, uint64_t fenceValue) {
    if (!m_commandQueue || !otherFence || fenceValue == 0) {
        return;
    }

    // GPU-side wait: this queue waits for the other queue's fence to reach the specified value
    // This enables cross-queue synchronization for async compute
    HRESULT hr = m_commandQueue->Wait(otherFence, fenceValue);
    if (FAILED(hr)) {
        spdlog::error("Failed to wait for cross-queue fence: 0x{:08X}",
                      static_cast<unsigned int>(hr));
    }
}

void DX12CommandQueue::Flush() {
    if (!m_commandQueue || !m_fence) {
        return;
    }
    uint64_t fenceValue = Signal();
    WaitForFenceValue(fenceValue);
}

bool DX12CommandQueue::IsFenceComplete(uint64_t fenceValue) const {
    if (!m_fence) {
        return true;
    }
    return m_fence->GetCompletedValue() >= fenceValue;
}

uint64_t DX12CommandQueue::GetLastCompletedFenceValue() const {
    if (!m_fence) {
        return 0;
    }
    return m_fence->GetCompletedValue();
}

} // namespace Cortex::Graphics
