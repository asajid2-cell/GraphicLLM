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
    // Ensure all GPU work is complete before cleanup
    Flush();

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_fence.Reset();
    m_commandQueue.Reset();
}

void DX12CommandQueue::ExecuteCommandList(ID3D12CommandList* commandList) {
    ID3D12CommandList* const commandLists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
}

uint64_t DX12CommandQueue::Signal() {
    uint64_t fenceValue = m_nextFenceValue++;
    m_commandQueue->Signal(m_fence.Get(), fenceValue);
    return fenceValue;
}

void DX12CommandQueue::WaitForFenceValue(uint64_t fenceValue) {
    if (IsFenceComplete(fenceValue)) {
        return;
    }

    // Schedule an event when the fence reaches the specified value
    m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);

    // Wait for the event to be signaled
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

void DX12CommandQueue::Flush() {
    uint64_t fenceValue = Signal();
    WaitForFenceValue(fenceValue);
}

bool DX12CommandQueue::IsFenceComplete(uint64_t fenceValue) const {
    return m_fence->GetCompletedValue() >= fenceValue;
}

uint64_t DX12CommandQueue::GetLastCompletedFenceValue() const {
    return m_fence->GetCompletedValue();
}

} // namespace Cortex::Graphics
