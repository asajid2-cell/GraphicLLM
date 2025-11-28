#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

// Handle to a descriptor in the heap
struct DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    uint32_t index = 0;

    [[nodiscard]] bool IsValid() const { return cpu.ptr != 0; }
};

// Dynamic Descriptor Heap - Ring buffer style for per-frame resource binding
// This is the "secret sauce" for hot-swapping textures in real-time
class DescriptorHeap {
public:
    DescriptorHeap() = default;
    ~DescriptorHeap() = default;

    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;
    DescriptorHeap(DescriptorHeap&&) = default;
    DescriptorHeap& operator=(DescriptorHeap&&) = default;

    // Initialize heap with specified capacity
    Result<void> Initialize(
        ID3D12Device* device,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t numDescriptors,
        bool shaderVisible = true
    );

    // Allocate a descriptor (returns index)
    Result<DescriptorHandle> Allocate();

    // Free a descriptor (for reuse)
    void Free(uint32_t index);

    // Reset allocation (for ring buffer behavior)
    void Reset();

    // Reset allocation starting from a specific offset (used when reserving
    // a prefix of persistent descriptors and reusing the rest per frame)
    void ResetFrom(uint32_t offset);

    // Get descriptor at specific index
    [[nodiscard]] DescriptorHandle GetHandle(uint32_t index) const;

    // Accessors
    [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }
    [[nodiscard]] uint32_t GetCapacity() const { return m_numDescriptors; }
    [[nodiscard]] uint32_t GetUsedCount() const { return m_currentOffset; }
    [[nodiscard]] bool HasSpace() const { return m_currentOffset < m_numDescriptors; }

private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    uint32_t m_numDescriptors = 0;
    uint32_t m_descriptorSize = 0;
    uint32_t m_currentOffset = 0;
    bool m_shaderVisible = false;

    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
};

// Descriptor Heap Manager - manages multiple heaps for different types
class DescriptorHeapManager {
public:
    DescriptorHeapManager() = default;
    ~DescriptorHeapManager() = default;

    Result<void> Initialize(ID3D12Device* device);

    // Allocate descriptors from appropriate heap
    Result<DescriptorHandle> AllocateRTV();  // Render Target View
    Result<DescriptorHandle> AllocateDSV();  // Depth Stencil View
    Result<DescriptorHandle> AllocateCBV_SRV_UAV();  // Constant Buffer / Shader Resource / Unordered Access View

    // Get heaps for binding
    [[nodiscard]] ID3D12DescriptorHeap* GetCBV_SRV_UAV_Heap() const {
        return m_cbvSrvUavHeap.GetHeap();
    }

    [[nodiscard]] uint32_t GetCBVSrvUavCapacity() const {
        return m_cbvSrvUavHeap.GetCapacity();
    }

    // Allocate transient descriptors (per-frame ring buffer region)
    Result<DescriptorHandle> AllocateTransientCBV_SRV_UAV();

    // Reset allocations (for per-frame ring buffer)
    void ResetFrameHeaps();

private:
    static constexpr uint32_t RTV_HEAP_SIZE = 64;
    static constexpr uint32_t DSV_HEAP_SIZE = 64;
    static constexpr uint32_t CBV_SRV_UAV_HEAP_SIZE = 1024;  // Large for many textures

    DescriptorHeap m_rtvHeap;
    DescriptorHeap m_dsvHeap;
    DescriptorHeap m_cbvSrvUavHeap;

    // Number of CBV/SRV/UAV descriptors reserved for persistent resources
    // (textures, shadow maps, HDR targets, etc.). Transient allocations start
    // after this index and are reset every frame.
    uint32_t m_cbvSrvUavPersistentCount = 0;
};

} // namespace Cortex::Graphics
