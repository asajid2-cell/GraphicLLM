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

    spdlog::info("Descriptor Heap Manager initialized");
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
        return result;
    }

    auto handle = result.Value();
    if (handle.index + 1 > m_cbvSrvUavPersistentCount) {
        m_cbvSrvUavPersistentCount = handle.index + 1;
    }

    return Result<DescriptorHandle>::Ok(handle);
}

Result<DescriptorHandle> DescriptorHeapManager::AllocateTransientCBV_SRV_UAV() {
    return m_cbvSrvUavHeap.Allocate();
}

void DescriptorHeapManager::ResetFrameHeaps() {
    // Only reset the transient region of the shader-visible heap each frame.
    // Persistent descriptors (textures, shadow maps, HDR targets, etc.) live
    // in [0, m_cbvSrvUavPersistentCount) and are never overwritten.
    m_cbvSrvUavHeap.ResetFrom(m_cbvSrvUavPersistentCount);
}

} // namespace Cortex::Graphics
