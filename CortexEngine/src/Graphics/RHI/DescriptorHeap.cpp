#include "DescriptorHeap.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

// ========== DescriptorHeap Implementation ==========

Result<void> DescriptorHeap::Initialize(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t numDescriptors,
    bool shaderVisible)
{
    if (!device) {
        return Result<void>::Err("Invalid device pointer");
    }

    m_type = type;
    m_numDescriptors = numDescriptors;
    m_shaderVisible = shaderVisible;

    // Create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = numDescriptors;
    heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create descriptor heap");
    }

    // Cache descriptor size (varies by type and GPU)
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);

    // Get starting handles
    m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible) {
        m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();
    }

    m_currentOffset = 0;

    const char* typeName =
        (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) ? "RTV" :
        (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) ? "DSV" :
        (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ? "CBV_SRV_UAV" : "SAMPLER";

    spdlog::info("Descriptor Heap ({}) created: {} descriptors", typeName, numDescriptors);

    return Result<void>::Ok();
}

Result<DescriptorHandle> DescriptorHeap::Allocate() {
    if (m_currentOffset >= m_numDescriptors) {
        return Result<DescriptorHandle>::Err("Descriptor heap is full");
    }

    DescriptorHandle handle = GetHandle(m_currentOffset);
    m_currentOffset++;

    return Result<DescriptorHandle>::Ok(handle);
}

void DescriptorHeap::Free(uint32_t index) {
    // For a ring buffer, we typically don't free individual descriptors
    // This would be implemented for a more sophisticated allocator
    // For now, we just reset the entire heap per frame
}

void DescriptorHeap::Reset() {
    m_currentOffset = 0;
}

void DescriptorHeap::ResetFrom(uint32_t offset) {
    if (offset > m_numDescriptors) {
        offset = m_numDescriptors;
    }
    m_currentOffset = offset;
}

DescriptorHandle DescriptorHeap::GetHandle(uint32_t index) const {
    if (index >= m_numDescriptors) {
        return {}; // Invalid handle
    }

    DescriptorHandle handle;
    handle.index = index;
    handle.cpu.ptr = m_cpuStart.ptr + (index * m_descriptorSize);

    if (m_shaderVisible) {
        handle.gpu.ptr = m_gpuStart.ptr + (index * m_descriptorSize);
    }

    return handle;
}

// ========== DescriptorHeapManager Implementation ==========

Result<void> DescriptorHeapManager::Initialize(ID3D12Device* device) {
    // Create RTV heap (Render Target Views)
    auto rtvResult = m_rtvHeap.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_HEAP_SIZE, false);
    if (rtvResult.IsErr()) {
        return Result<void>::Err("Failed to create RTV heap: " + rtvResult.Error());
    }

    // Create DSV heap (Depth Stencil Views)
    auto dsvResult = m_dsvHeap.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_HEAP_SIZE, false);
    if (dsvResult.IsErr()) {
        return Result<void>::Err("Failed to create DSV heap: " + dsvResult.Error());
    }

    // Create CBV/SRV/UAV heap (Constant Buffers / Shader Resources / Unordered Access)
    // This is the CRITICAL heap for texture hot-swapping
    auto cbvResult = m_cbvSrvUavHeap.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CBV_SRV_UAV_HEAP_SIZE, true);
    if (cbvResult.IsErr()) {
        return Result<void>::Err("Failed to create CBV/SRV/UAV heap: " + cbvResult.Error());
    }

    // Create staging CBV/SRV/UAV heap (CPU-only, non-shader-visible)
    // Used for persistent SRVs that will be copied to the shader-visible heap
    auto stagingResult = m_stagingCbvSrvUavHeap.Initialize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, STAGING_CBV_SRV_UAV_HEAP_SIZE, false);
    if (stagingResult.IsErr()) {
        return Result<void>::Err("Failed to create staging CBV/SRV/UAV heap: " + stagingResult.Error());
    }

    spdlog::info("Descriptor Heap Manager initialized (with staging heap)");
    return Result<void>::Ok();
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateRTV() {
    return m_rtvHeap.Allocate();
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateDSV() {
    return m_dsvHeap.Allocate();
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateCBV_SRV_UAV() {
    auto result = m_cbvSrvUavHeap.Allocate();
    if (result.IsErr()) {
        spdlog::error("CBV_SRV_UAV persistent allocation FAILED: heap exhausted at {}/{} (persistent={})",
                      m_cbvSrvUavHeap.GetUsedCount(), m_cbvSrvUavHeap.GetCapacity(), m_cbvSrvUavPersistentCount);
        return result;
    }

    auto handle = result.Value();
    uint32_t oldPersistentCount = m_cbvSrvUavPersistentCount;
    if (handle.index + 1 > m_cbvSrvUavPersistentCount) {
        m_cbvSrvUavPersistentCount = handle.index + 1;

        // Log persistent descriptor growth (useful for tracking texture loads)
        if (m_cbvSrvUavPersistentCount % 50 == 0 || m_cbvSrvUavPersistentCount > m_cbvSrvUavHeap.GetCapacity() * 0.8f) {
            spdlog::info("CBV_SRV_UAV persistent descriptors: {} / {} capacity ({:.1f}% persistent)",
                         m_cbvSrvUavPersistentCount, m_cbvSrvUavHeap.GetCapacity(),
                         100.0f * m_cbvSrvUavPersistentCount / m_cbvSrvUavHeap.GetCapacity());
        }
    }

    return Result<DescriptorHandle>::Ok(handle);
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateStagingCBV_SRV_UAV() {
    // Allocate from the CPU-only staging heap
    // These descriptors can be safely used as SOURCE in CopyDescriptorsSimple
    auto result = m_stagingCbvSrvUavHeap.Allocate();
    if (result.IsErr()) {
        spdlog::error("Staging CBV_SRV_UAV heap EXHAUSTED: {}/{} descriptors",
                      m_stagingCbvSrvUavHeap.GetUsedCount(), m_stagingCbvSrvUavHeap.GetCapacity());
    }
    return result;
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateTransientCBV_SRV_UAV() {
    uint32_t used = m_cbvSrvUavHeap.GetUsedCount();
    uint32_t capacity = m_cbvSrvUavHeap.GetCapacity();

    // Warn when approaching capacity (>90% full)
    if (used >= capacity * 0.9f) {
        spdlog::warn("CBV_SRV_UAV heap nearly full: {}/{} descriptors used (persistent={}, transient={})",
                     used, capacity, m_cbvSrvUavPersistentCount, used - m_cbvSrvUavPersistentCount);
    }

    auto result = m_cbvSrvUavHeap.Allocate();
    if (result.IsErr()) {
        spdlog::error("CBV_SRV_UAV heap EXHAUSTED: {}/{} descriptors (persistent={}, transient region full)",
                      used, capacity, m_cbvSrvUavPersistentCount);
    }

    return result;
}

void DescriptorHeapManager::ResetFrameHeaps() {
    // Only reset the transient region of the shader-visible heap each frame.
    // Persistent descriptors (textures, shadow maps, HDR targets, etc.) live
    // in [0, m_cbvSrvUavPersistentCount) and are never overwritten.

    uint32_t usedBefore = m_cbvSrvUavHeap.GetUsedCount();
    uint32_t transientUsed = usedBefore - m_cbvSrvUavPersistentCount;

    m_cbvSrvUavHeap.ResetFrom(m_cbvSrvUavPersistentCount);

    // Log if transient usage is high (indicates potential inefficiency)
    if (transientUsed > 100) {
        spdlog::debug("Frame heap reset: persistent={}, transient={} (capacity={})",
                      m_cbvSrvUavPersistentCount, transientUsed, m_cbvSrvUavHeap.GetCapacity());
    }
}

} // namespace Cortex::Graphics
