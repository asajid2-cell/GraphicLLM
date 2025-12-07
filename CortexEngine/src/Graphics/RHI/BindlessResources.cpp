#include "BindlessResources.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> BindlessResourceManager::Initialize(ID3D12Device* device, uint32_t maxTextures, uint32_t maxBuffers) {
    if (!device) {
        return Result<void>::Err("BindlessResourceManager::Initialize: device is null");
    }

    m_device = device;
    m_textureCapacity = maxTextures;
    m_bufferCapacity = maxBuffers;
    m_totalCapacity = maxTextures + maxBuffers;

    // Create the bindless descriptor heap (shader-visible)
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = m_totalCapacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_bindlessHeap));
    if (FAILED(hr)) {
        return Result<void>::Err("BindlessResourceManager: Failed to create bindless descriptor heap");
    }

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_cpuStart = m_bindlessHeap->GetCPUDescriptorHandleForHeapStart();
    m_gpuStart = m_bindlessHeap->GetGPUDescriptorHandleForHeapStart();

    // Initialize texture free list (skip reserved slots)
    m_nextTextureSlot = kReservedSlots;
    m_textureFreeList.clear();

    // Initialize buffer free list (starts after texture region)
    m_nextBufferSlot = m_textureCapacity;
    m_bufferFreeList.clear();

    m_allocatedCount = kReservedSlots;  // Reserved slots count as allocated

    spdlog::info("BindlessResourceManager: Initialized with {} texture slots, {} buffer slots ({} total)",
                 m_textureCapacity, m_bufferCapacity, m_totalCapacity);

    return Result<void>::Ok();
}

void BindlessResourceManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_bindlessHeap.Reset();
    m_textureFreeList.clear();
    m_bufferFreeList.clear();
    m_device = nullptr;
    m_allocatedCount = 0;

    spdlog::info("BindlessResourceManager: Shutdown complete");
}

Result<uint32_t> BindlessResourceManager::AllocateTextureIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) {
    if (!m_device || !m_bindlessHeap) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t index = kInvalidBindlessIndex;

    // Try to reuse from free list first
    if (!m_textureFreeList.empty()) {
        index = m_textureFreeList.back();
        m_textureFreeList.pop_back();
    } else if (m_nextTextureSlot < m_textureCapacity) {
        index = m_nextTextureSlot++;
    } else {
        return Result<uint32_t>::Err("BindlessResourceManager: Texture slots exhausted");
    }

    // Create the SRV at the allocated index
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cpuStart;
    cpuHandle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;

    m_device->CreateShaderResourceView(resource, srvDesc, cpuHandle);
    ++m_allocatedCount;

    // Log milestone allocations
    if (m_allocatedCount % 100 == 0 || m_allocatedCount > m_textureCapacity * 0.8f) {
        spdlog::debug("BindlessResourceManager: {} textures allocated ({:.1f}% of texture capacity)",
                      m_allocatedCount, 100.0f * m_allocatedCount / m_textureCapacity);
    }

    return Result<uint32_t>::Ok(index);
}

Result<uint32_t> BindlessResourceManager::AllocateBufferIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) {
    if (!m_device || !m_bindlessHeap) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t index = kInvalidBindlessIndex;

    // Try to reuse from free list first
    if (!m_bufferFreeList.empty()) {
        index = m_bufferFreeList.back();
        m_bufferFreeList.pop_back();
    } else if (m_nextBufferSlot < m_totalCapacity) {
        index = m_nextBufferSlot++;
    } else {
        return Result<uint32_t>::Err("BindlessResourceManager: Buffer slots exhausted");
    }

    // Create the SRV at the allocated index
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cpuStart;
    cpuHandle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;

    m_device->CreateShaderResourceView(resource, srvDesc, cpuHandle);
    ++m_allocatedCount;

    return Result<uint32_t>::Ok(index);
}

Result<uint32_t> BindlessResourceManager::AllocateUAVIndex(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) {
    if (!m_device || !m_bindlessHeap) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // UAVs share the buffer region
    uint32_t index = kInvalidBindlessIndex;

    if (!m_bufferFreeList.empty()) {
        index = m_bufferFreeList.back();
        m_bufferFreeList.pop_back();
    } else if (m_nextBufferSlot < m_totalCapacity) {
        index = m_nextBufferSlot++;
    } else {
        return Result<uint32_t>::Err("BindlessResourceManager: Buffer/UAV slots exhausted");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cpuStart;
    cpuHandle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;

    m_device->CreateUnorderedAccessView(resource, nullptr, uavDesc, cpuHandle);
    ++m_allocatedCount;

    return Result<uint32_t>::Ok(index);
}

void BindlessResourceManager::ReleaseIndex(uint32_t index) {
    if (index == kInvalidBindlessIndex || index >= m_totalCapacity) {
        return;
    }

    // Don't release reserved placeholder slots
    if (index < kReservedSlots) {
        spdlog::warn("BindlessResourceManager: Attempted to release reserved slot {}", index);
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Return to appropriate free list
    if (index < m_textureCapacity) {
        m_textureFreeList.push_back(index);
    } else {
        m_bufferFreeList.push_back(index);
    }

    if (m_allocatedCount > 0) {
        --m_allocatedCount;
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessResourceManager::GetCPUHandle(uint32_t index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessResourceManager::GetGPUHandle(uint32_t index) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_gpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_descriptorSize;
    return handle;
}

} // namespace Cortex::Graphics
