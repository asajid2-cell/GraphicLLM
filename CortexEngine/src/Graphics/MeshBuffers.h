#pragma once

#include "RHI/D3D12Includes.h"
#include <wrl/client.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>

namespace Cortex::Graphics {

// Forward declaration
struct MeshBuffers;

// ============================================================================
// Deferred GPU Resource Deletion Queue
// ============================================================================
//
// Academic Background:
// In D3D12 (and other explicit graphics APIs), GPU resources must not be
// released while still referenced by in-flight command lists. The standard
// pattern is to defer resource deletion by N+1 frames, where N is the number
// of frames in flight (typically 2-3 for triple buffering).
//
// How it works:
// 1. When a MeshBuffers is destroyed, instead of immediately releasing the
//    D3D12 resources, we move the ComPtrs into this queue with a frame counter.
// 2. Each frame at the start (before recording new commands), we decrement
//    the counter on all queued resources.
// 3. Resources whose counter reaches 0 are released (their ComPtrs go out
//    of scope, calling Release() on the underlying D3D12 objects).
//
// This ensures GPU resources stay alive until all command lists that may
// reference them have completed execution.
// ============================================================================

class DeferredGPUDeletionQueue {
public:
    // Number of frames to defer deletion. Must be >= number of frames in flight + 1
    // to ensure the resource is no longer referenced by any queued command list.
    static constexpr uint32_t kDeferFrames = 4; // Safe for triple buffering

    static DeferredGPUDeletionQueue& Instance() {
        static DeferredGPUDeletionQueue s_instance;
        return s_instance;
    }

    // Queue a D3D12 resource for deferred deletion
    void QueueResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource) {
        if (!resource) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingResources.push_back({std::move(resource), kDeferFrames});
    }

    // Queue mesh buffers for deferred deletion (moves vertex and index buffers)
    void QueueMeshBuffers(std::shared_ptr<MeshBuffers> buffers);

    // Process the queue: decrement counters and release expired resources.
    // Called once per frame at the start of BeginFrame.
    void ProcessFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Process individual resources
        auto it = m_pendingResources.begin();
        while (it != m_pendingResources.end()) {
            if (--it->framesRemaining == 0) {
                // ComPtr goes out of scope, releasing the D3D12 resource
                it = m_pendingResources.erase(it);
            } else {
                ++it;
            }
        }

        // Process mesh buffer holders
        auto bufIt = m_pendingMeshBuffers.begin();
        while (bufIt != m_pendingMeshBuffers.end()) {
            if (--bufIt->framesRemaining == 0) {
                bufIt = m_pendingMeshBuffers.erase(bufIt);
            } else {
                ++bufIt;
            }
        }
    }

    // Get statistics for debugging
    size_t GetPendingResourceCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pendingResources.size() + m_pendingMeshBuffers.size();
    }

private:
    DeferredGPUDeletionQueue() = default;
    ~DeferredGPUDeletionQueue() = default;
    DeferredGPUDeletionQueue(const DeferredGPUDeletionQueue&) = delete;
    DeferredGPUDeletionQueue& operator=(const DeferredGPUDeletionQueue&) = delete;

    struct PendingResource {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t framesRemaining;
    };

    struct PendingMeshBuffers {
        std::shared_ptr<MeshBuffers> buffers;
        uint32_t framesRemaining;
    };

    mutable std::mutex m_mutex;
    std::deque<PendingResource> m_pendingResources;
    std::deque<PendingMeshBuffers> m_pendingMeshBuffers;
};

// Simple wrapper around the vertex and index buffers used for mesh draws.
// Shared between the raster renderer and the DXR context.
//
// IMPORTANT: When destroying mesh buffers while GPU may still be using them,
// call DeferMeshBuffersDeletion() instead of directly destroying the shared_ptr.
struct MeshBuffers {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    // Indices into the renderer's shader-visible CBV/SRV/UAV heap used for SM6.6 ResourceDescriptorHeap[] access.
    // These are created once when the mesh buffers are uploaded (no per-frame mesh SRV churn).
    static constexpr uint32_t kInvalidDescriptorIndex = 0xFFFFFFFFu;
    uint32_t vbRawSRVIndex = kInvalidDescriptorIndex;
    uint32_t ibRawSRVIndex = kInvalidDescriptorIndex;
    uint32_t vertexStrideBytes = 64u; // sizeof(Vertex) - must match ShaderTypes.h
    uint32_t indexFormat = 0u;        // 0 = R32_UINT, 1 = R16_UINT
};

// Helper function to safely destroy mesh buffers when the GPU may still be using them.
// This moves the buffers to the deferred deletion queue instead of immediately releasing.
inline void DeferMeshBuffersDeletion(std::shared_ptr<MeshBuffers>& buffers) {
    if (buffers) {
        DeferredGPUDeletionQueue::Instance().QueueMeshBuffers(std::move(buffers));
        buffers.reset(); // Ensure the original shared_ptr is cleared
    }
}

// Inline implementation of QueueMeshBuffers (after MeshBuffers is fully defined)
inline void DeferredGPUDeletionQueue::QueueMeshBuffers(std::shared_ptr<MeshBuffers> buffers) {
    if (!buffers) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingMeshBuffers.push_back({std::move(buffers), kDeferFrames});
}

} // namespace Cortex::Graphics
