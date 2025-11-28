#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;

// Command Queue wrapper - manages command submission and GPU synchronization
class DX12CommandQueue {
public:
    DX12CommandQueue() = default;
    ~DX12CommandQueue();

    DX12CommandQueue(const DX12CommandQueue&) = delete;
    DX12CommandQueue& operator=(const DX12CommandQueue&) = delete;
    DX12CommandQueue(DX12CommandQueue&&) = default;
    DX12CommandQueue& operator=(DX12CommandQueue&&) = default;

    // Initialize with a device
    Result<void> Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);

    // Cleanup
    void Shutdown();

    // Execute a command list
    void ExecuteCommandList(ID3D12CommandList* commandList);

    // Signal the fence from the GPU side
    uint64_t Signal();

    // Wait for a specific fence value (CPU blocks)
    void WaitForFenceValue(uint64_t fenceValue);

    // Flush all pending GPU work (CPU blocks until GPU is idle)
    void Flush();

    // Check if a fence value has been reached
    [[nodiscard]] bool IsFenceComplete(uint64_t fenceValue) const;

    // Accessors
    [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const { return m_commandQueue.Get(); }
    [[nodiscard]] ID3D12Fence* GetFence() const { return m_fence.Get(); }
    [[nodiscard]] uint64_t GetLastCompletedFenceValue() const;
    [[nodiscard]] uint64_t GetNextFenceValue() const { return m_nextFenceValue; }

private:
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12Fence> m_fence;

    HANDLE m_fenceEvent = nullptr;
    uint64_t m_nextFenceValue = 1;
};

} // namespace Cortex::Graphics
