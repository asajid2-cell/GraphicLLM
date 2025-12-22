#include "DescriptorHeap.h"
#include <algorithm>
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

Result<void> DescriptorHeapManager::Initialize(ID3D12Device* device, uint32_t frameCount) {
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

    m_frameCount = std::max(frameCount, 1u);
    m_activeFrameIndex = 0;
    m_frameActive = false;
    m_transientActive = false;
    UpdateTransientSegment();

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
    uint32_t capacity = m_cbvSrvUavHeap.GetCapacity();
    if (m_cbvSrvUavPersistentCount >= capacity) {
        spdlog::error("CBV_SRV_UAV persistent allocation FAILED: heap exhausted at {}/{} (persistent={})",
                      m_cbvSrvUavPersistentCount, capacity, m_cbvSrvUavPersistentCount);
        return Result<DescriptorHandle>::Err("Descriptor heap exhausted");
    }

    if (m_frameActive && m_transientActive) {
        spdlog::warn("Persistent descriptor allocation requested after transient use; retry next frame to avoid aliasing");
        return Result<DescriptorHandle>::Err("Persistent allocation unsafe after transient descriptors were allocated");
    }

    if (m_frameActive && m_flushCallback) {
        m_flushCallback();
    }

    DescriptorHandle handle = m_cbvSrvUavHeap.GetHandle(m_cbvSrvUavPersistentCount);
    if (!handle.IsValid()) {
        spdlog::error("CBV_SRV_UAV persistent allocation FAILED: invalid handle at index {}", m_cbvSrvUavPersistentCount);
        return Result<DescriptorHandle>::Err("Invalid descriptor handle");
    }

    if (handle.index + 1 > m_cbvSrvUavPersistentCount) {
        m_cbvSrvUavPersistentCount = handle.index + 1;

        // Log persistent descriptor growth (useful for tracking texture loads)
        if (m_cbvSrvUavPersistentCount % 50 == 0 || m_cbvSrvUavPersistentCount > m_cbvSrvUavHeap.GetCapacity() * 0.8f) {
            spdlog::info("CBV_SRV_UAV persistent descriptors: {} / {} capacity ({:.1f}% persistent)",
                         m_cbvSrvUavPersistentCount, m_cbvSrvUavHeap.GetCapacity(),
                         100.0f * m_cbvSrvUavPersistentCount / m_cbvSrvUavHeap.GetCapacity());
        }
    }

    if (m_frameActive) {
        UpdateTransientSegment();
        m_cbvSrvUavHeap.ResetFrom(m_transientSegmentStart);
    } else {
        m_cbvSrvUavHeap.ResetFrom(m_cbvSrvUavPersistentCount);
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
    if (!m_frameActive) {
        spdlog::warn("Transient descriptor allocation before BeginFrame; defaulting to frame 0 segment");
        BeginFrame(0);
    }

    if (m_transientSegmentStart >= m_transientSegmentEnd) {
        spdlog::error("Transient descriptor segment is empty (persistent={}, capacity={})",
                      m_cbvSrvUavPersistentCount, m_cbvSrvUavHeap.GetCapacity());
        return Result<DescriptorHandle>::Err("Transient descriptor segment is empty");
    }

    uint32_t used = m_cbvSrvUavHeap.GetUsedCount();
    if (used < m_transientSegmentStart) {
        m_cbvSrvUavHeap.ResetFrom(m_transientSegmentStart);
        used = m_transientSegmentStart;
    }

    uint32_t segmentCapacity = m_transientSegmentEnd - m_transientSegmentStart;
    uint32_t usedInSegment = used - m_transientSegmentStart;

    if (used >= m_transientSegmentEnd) {
        spdlog::error("CBV_SRV_UAV transient segment EXHAUSTED: {}/{} descriptors (persistent={})",
                      usedInSegment, segmentCapacity, m_cbvSrvUavPersistentCount);
        return Result<DescriptorHandle>::Err("Transient descriptor segment exhausted");
    }

    if (segmentCapacity > 0 && usedInSegment >= static_cast<uint32_t>(segmentCapacity * 0.9f)) {
        spdlog::warn("CBV_SRV_UAV transient segment nearly full: {}/{} descriptors (persistent={}, frame={})",
                     usedInSegment, segmentCapacity, m_cbvSrvUavPersistentCount, m_activeFrameIndex);
    }

    auto result = m_cbvSrvUavHeap.Allocate();
    if (result.IsErr()) {
        spdlog::error("CBV_SRV_UAV transient allocation failed: {}/{} descriptors",
                      usedInSegment, segmentCapacity);
    }

    m_transientActive = true;

    return result;
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateTransientCBV_SRV_UAVRange(uint32_t count) {
    if (count == 0) {
        return Result<DescriptorHandle>::Err("Transient descriptor range allocation requires count > 0");
    }

    if (!m_frameActive) {
        spdlog::warn("Transient descriptor range allocation before BeginFrame; defaulting to frame 0 segment");
        BeginFrame(0);
    }

    if (m_transientSegmentStart >= m_transientSegmentEnd) {
        spdlog::error("Transient descriptor segment is empty (persistent={}, capacity={})",
                      m_cbvSrvUavPersistentCount, m_cbvSrvUavHeap.GetCapacity());
        return Result<DescriptorHandle>::Err("Transient descriptor segment is empty");
    }

    uint32_t used = m_cbvSrvUavHeap.GetUsedCount();
    if (used < m_transientSegmentStart) {
        m_cbvSrvUavHeap.ResetFrom(m_transientSegmentStart);
        used = m_transientSegmentStart;
    }

    const uint32_t segmentCapacity = m_transientSegmentEnd - m_transientSegmentStart;
    const uint32_t usedInSegment = used - m_transientSegmentStart;

    if (used + count > m_transientSegmentEnd) {
        spdlog::error("CBV_SRV_UAV transient segment cannot fit range: used {}/{} need {} (persistent={}, frame={})",
                      usedInSegment, segmentCapacity, count, m_cbvSrvUavPersistentCount, m_activeFrameIndex);
        return Result<DescriptorHandle>::Err("Transient descriptor segment range exhausted");
    }

    if (segmentCapacity > 0) {
        const uint32_t after = usedInSegment + count;
        if (after >= static_cast<uint32_t>(segmentCapacity * 0.9f)) {
            spdlog::warn("CBV_SRV_UAV transient segment nearly full after range alloc: {}/{} descriptors (persistent={}, frame={})",
                         after, segmentCapacity, m_cbvSrvUavPersistentCount, m_activeFrameIndex);
        }
    }

    DescriptorHandle base = m_cbvSrvUavHeap.GetHandle(used);
    if (!base.IsValid()) {
        spdlog::error("CBV_SRV_UAV transient range base handle invalid (used={}, capacity={}, persistent={}, frame={})",
                      used, m_cbvSrvUavHeap.GetCapacity(), m_cbvSrvUavPersistentCount, m_activeFrameIndex);
        return Result<DescriptorHandle>::Err("Transient descriptor range base handle invalid");
    }
    // Reserve the range by advancing the heap cursor.
    m_cbvSrvUavHeap.ResetFrom(used + count);
    m_transientActive = true;
    return Result<DescriptorHandle>::Ok(base);
}

void DescriptorHeapManager::BeginFrame(uint32_t frameIndex) {
    m_frameActive = true;
    m_transientActive = false;
    m_activeFrameIndex = (m_frameCount > 0) ? (frameIndex % m_frameCount) : 0;

    UpdateTransientSegment();
    m_cbvSrvUavHeap.ResetFrom(m_transientSegmentStart);

    uint32_t segmentCapacity = (m_transientSegmentEnd > m_transientSegmentStart)
        ? (m_transientSegmentEnd - m_transientSegmentStart)
        : 0;
    if (segmentCapacity == 0) {
        spdlog::warn("Transient descriptor segment empty for frame {} (persistent={}, capacity={})",
                     m_activeFrameIndex, m_cbvSrvUavPersistentCount, m_cbvSrvUavHeap.GetCapacity());
    }
}

void DescriptorHeapManager::ResetFrameHeaps() {
    BeginFrame(0);
}

void DescriptorHeapManager::UpdateTransientSegment() {
    const uint32_t capacity = m_cbvSrvUavHeap.GetCapacity();
    uint32_t persistentCount = std::min(m_cbvSrvUavPersistentCount, capacity);

    if (persistentCount >= capacity) {
        m_transientSegmentStart = capacity;
        m_transientSegmentEnd = capacity;
        return;
    }

    uint32_t transientCapacity = capacity - persistentCount;
    if (m_frameCount <= 1) {
        m_transientSegmentStart = persistentCount;
        m_transientSegmentEnd = capacity;
        return;
    }

    uint32_t perFrame = transientCapacity / m_frameCount;
    uint32_t remainder = transientCapacity % m_frameCount;
    uint32_t frameIdx = std::min(m_activeFrameIndex, m_frameCount - 1);
    uint32_t extra = (frameIdx < remainder) ? 1u : 0u;
    uint32_t offset = perFrame * frameIdx + std::min(frameIdx, remainder);

    m_transientSegmentStart = persistentCount + offset;
    m_transientSegmentEnd = m_transientSegmentStart + perFrame + extra;
}

} // namespace Cortex::Graphics
