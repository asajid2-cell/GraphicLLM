#include "BindlessResources.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> BindlessResourceManager::Initialize(ID3D12Device* device,
                                                 DescriptorHeapManager* descriptorManager,
                                                 uint32_t maxTextures,
                                                 uint32_t maxBuffers) {
    if (!device) {
        return Result<void>::Err("BindlessResourceManager::Initialize: device is null");
    }
    if (!descriptorManager) {
        return Result<void>::Err("BindlessResourceManager::Initialize: descriptor manager is null");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_textureCapacity = maxTextures;
    m_bufferCapacity = maxBuffers;
    m_totalCapacity = maxTextures + maxBuffers;

    auto reservedResult = m_descriptorManager->AllocateCBV_SRV_UAVRange(kReservedSlots);
    if (reservedResult.IsErr()) {
        return Result<void>::Err("BindlessResourceManager: failed to reserve placeholder descriptors: " +
                                 reservedResult.Error());
    }
    if (reservedResult.Value().index != kPlaceholderAlbedoIndex) {
        return Result<void>::Err("BindlessResourceManager: placeholder descriptors must occupy heap slots 0-3");
    }

    // Initialize texture free list (skip reserved slots)
    m_nextTextureSlot = kReservedSlots;
    m_textureFreeList.clear();

    // Buffer indices also come from the global descriptor heap. The capacity
    // split is a logical budget only; indices do not occupy separate heaps.
    m_nextBufferSlot = 0;
    m_bufferFreeList.clear();

    m_allocatedCount = kReservedSlots;  // Reserved slots count as allocated
    m_textureAllocated = kReservedSlots;
    m_bufferAllocated = 0;

    spdlog::info("BindlessResourceManager: Initialized with {} texture slots, {} buffer slots ({} total)",
                 m_textureCapacity, m_bufferCapacity, m_totalCapacity);

    return Result<void>::Ok();
}

void BindlessResourceManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_textureFreeList.clear();
    m_bufferFreeList.clear();
    m_device = nullptr;
    m_descriptorManager = nullptr;
    m_allocatedCount = 0;
    m_textureAllocated = 0;
    m_bufferAllocated = 0;

    spdlog::info("BindlessResourceManager: Shutdown complete");
}

Result<uint32_t> BindlessResourceManager::AllocateTextureIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) {
    if (!m_device || !m_descriptorManager) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_textureAllocated >= m_textureCapacity) {
        return Result<uint32_t>::Err("BindlessResourceManager: Texture slots exhausted");
    }

    auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (handleResult.IsErr()) {
        return Result<uint32_t>::Err("BindlessResourceManager: failed to allocate texture descriptor: " +
                                     handleResult.Error());
    }
    const DescriptorHandle handle = handleResult.Value();

    m_device->CreateShaderResourceView(resource, srvDesc, handle.cpu);
    ++m_allocatedCount;
    ++m_textureAllocated;

    // Log milestone allocations
    if (m_allocatedCount % 100 == 0 || m_allocatedCount > m_textureCapacity * 0.8f) {
        spdlog::debug("BindlessResourceManager: {} textures allocated ({:.1f}% of texture capacity)",
                      m_allocatedCount, 100.0f * m_allocatedCount / m_textureCapacity);
    }

    return Result<uint32_t>::Ok(handle.index);
}

Result<uint32_t> BindlessResourceManager::AllocateBufferIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) {
    if (!m_device || !m_descriptorManager) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bufferAllocated >= m_bufferCapacity) {
        return Result<uint32_t>::Err("BindlessResourceManager: Buffer slots exhausted");
    }

    auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (handleResult.IsErr()) {
        return Result<uint32_t>::Err("BindlessResourceManager: failed to allocate buffer descriptor: " +
                                     handleResult.Error());
    }
    const DescriptorHandle handle = handleResult.Value();

    m_device->CreateShaderResourceView(resource, srvDesc, handle.cpu);
    ++m_allocatedCount;
    ++m_bufferAllocated;

    return Result<uint32_t>::Ok(handle.index);
}

Result<uint32_t> BindlessResourceManager::AllocateUAVIndex(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) {
    if (!m_device || !m_descriptorManager) {
        return Result<uint32_t>::Err("BindlessResourceManager not initialized");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_bufferAllocated >= m_bufferCapacity) {
        return Result<uint32_t>::Err("BindlessResourceManager: Buffer/UAV slots exhausted");
    }

    auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (handleResult.IsErr()) {
        return Result<uint32_t>::Err("BindlessResourceManager: failed to allocate UAV descriptor: " +
                                     handleResult.Error());
    }
    const DescriptorHandle handle = handleResult.Value();

    m_device->CreateUnorderedAccessView(resource, nullptr, uavDesc, handle.cpu);
    ++m_allocatedCount;
    ++m_bufferAllocated;

    return Result<uint32_t>::Ok(handle.index);
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

    // Do not recycle global shader-visible descriptor slots. The heap is large
    // enough for release builds, and avoiding reuse prevents in-flight shaders
    // from observing a different resource through a stale bindless index.
    if (m_allocatedCount > 0) {
        --m_allocatedCount;
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE BindlessResourceManager::GetCPUHandle(uint32_t index) const {
    return m_descriptorManager ? m_descriptorManager->GetCBV_SRV_UAVHandle(index).cpu : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE BindlessResourceManager::GetGPUHandle(uint32_t index) const {
    return m_descriptorManager ? m_descriptorManager->GetCBV_SRV_UAVHandle(index).gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

ID3D12DescriptorHeap* BindlessResourceManager::GetHeap() const {
    return m_descriptorManager ? m_descriptorManager->GetCBV_SRV_UAV_Heap() : nullptr;
}

} // namespace Cortex::Graphics
