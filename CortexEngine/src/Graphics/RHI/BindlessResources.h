#pragma once

#define CORTEX_BINDLESS_RESOURCES_H

#include "D3D12Includes.h"
#include <wrl/client.h>
#include <cstdint>
#include <vector>
#include <mutex>
#include <functional>

#include "Utils/Result.h"
#include "DescriptorHeap.h"
#include "BindlessConstants.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;

// Bindless Resource Manager
// Publishes persistent SRV/UAV descriptors into the renderer's global
// shader-visible CBV/SRV/UAV heap for SM6.6 bindless access.
// All textures, buffers, and UAVs are registered here and accessed by index.
//
// Thread-safe: allocations can happen from any thread (e.g., async texture loading)
//
// Usage pattern:
//   1. Initialize with device + renderer descriptor manager at startup
//   2. Register textures via AllocateTextureIndex() -> returns uint32_t index
//   3. Pass indices to shaders via constant buffers
//   4. Shaders use ResourceDescriptorHeap[index] to access textures
//   5. Release indices via ReleaseIndex() when textures are destroyed
//
class BindlessResourceManager {
public:
    BindlessResourceManager() = default;
    ~BindlessResourceManager() = default;

    BindlessResourceManager(const BindlessResourceManager&) = delete;
    BindlessResourceManager& operator=(const BindlessResourceManager&) = delete;
    BindlessResourceManager(BindlessResourceManager&&) = default;
    BindlessResourceManager& operator=(BindlessResourceManager&&) = default;

    // Initialize bindless publishing. Must be called before any allocations.
    // Uses the same shader-visible CBV/SRV/UAV heap that command lists bind.
    Result<void> Initialize(ID3D12Device* device,
                            DescriptorHeapManager* descriptorManager,
                            uint32_t maxTextures = 16384,
                            uint32_t maxBuffers = 8192);

    // Shutdown and release all resources. Call WaitForGPU before this.
    void Shutdown();

    // Allocate a slot for a texture SRV and create the view.
    // Returns the bindless index to use in shaders.
    // Thread-safe.
    Result<uint32_t> AllocateTextureIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc = nullptr);

    // Allocate a slot for a buffer SRV (structured buffer, raw buffer, etc.)
    // Thread-safe.
    Result<uint32_t> AllocateBufferIndex(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc);

    // Allocate a slot for a UAV (for compute shaders, RT outputs, etc.)
    // Thread-safe.
    Result<uint32_t> AllocateUAVIndex(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc);

    // Release a previously allocated index back to the free list.
    // IMPORTANT: Ensure the GPU is not using the descriptor before releasing!
    // Thread-safe.
    void ReleaseIndex(uint32_t index);

    // Get the CPU handle for a bindless index (for copying descriptors)
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const;

    // Get the GPU handle for a bindless index
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const;

    // Get the renderer's shader-visible heap for binding to command lists
    [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const;

    // Get the base GPU handle for the entire heap (for root signature binding)
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetHeapGPUStart() const { return GetGPUHandle(0); }

    // Statistics
    [[nodiscard]] uint32_t GetAllocatedCount() const { return m_allocatedCount; }
    [[nodiscard]] uint32_t GetCapacity() const { return m_totalCapacity; }
    [[nodiscard]] uint32_t GetTextureCapacity() const { return m_textureCapacity; }
    [[nodiscard]] uint32_t GetBufferCapacity() const { return m_bufferCapacity; }

    // Callback for deferred release (set by Renderer to call WaitForGPU)
    using FlushCallback = std::function<void()>;
    void SetFlushCallback(FlushCallback callback) { m_flushCallback = std::move(callback); }

    // Reserved indices for placeholder textures (always valid)
    static constexpr uint32_t kPlaceholderAlbedoIndex = 0;
    static constexpr uint32_t kPlaceholderNormalIndex = 1;
    static constexpr uint32_t kPlaceholderMetallicIndex = 2;
    static constexpr uint32_t kPlaceholderRoughnessIndex = 3;
    static constexpr uint32_t kReservedSlots = 4;

private:
    ID3D12Device* m_device = nullptr;
    DescriptorHeapManager* m_descriptorManager = nullptr;

    uint32_t m_textureCapacity = 0;     // Slots 0..textureCapacity-1
    uint32_t m_bufferCapacity = 0;      // Slots textureCapacity..totalCapacity-1
    uint32_t m_totalCapacity = 0;
    uint32_t m_textureAllocated = 0;
    uint32_t m_bufferAllocated = 0;

    // Free lists for texture and buffer regions
    // Kept only for future leak diagnostics; released descriptors are not
    // reused because the global shader-visible heap has in-flight users.
    std::vector<uint32_t> m_textureFreeList;
    std::vector<uint32_t> m_bufferFreeList;

    uint32_t m_nextTextureSlot = kReservedSlots;  // Start after reserved placeholders
    uint32_t m_nextBufferSlot = 0;

    uint32_t m_allocatedCount = 0;

    mutable std::mutex m_mutex;
    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
