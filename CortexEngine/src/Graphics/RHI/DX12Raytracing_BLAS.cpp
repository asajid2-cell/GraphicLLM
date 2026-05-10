#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "../MeshBuffers.h"
#include "../ShaderTypes.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr uint64_t kMaxRTTrianglesPerMesh = 1000000ull;
constexpr uint64_t kMaxRTMeshBytes = 64ull * 1024ull * 1024ull;

} // namespace

void DX12RaytracingContext::RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh) {
    if (!m_device5 || !mesh) {
        return;
    }

    // We require GPU buffers to be populated by the renderer before we can
    // describe geometry for DXR. Without them we cannot build a BLAS.
    if (!mesh->gpuBuffers ||
        !mesh->gpuBuffers->vertexBuffer ||
        !mesh->gpuBuffers->indexBuffer ||
        mesh->positions.empty() ||
        mesh->indices.empty()) {
        return;
    }

    const size_t indexCount = mesh->indices.size();
    const size_t vertexCount = mesh->positions.size();
    const uint64_t triCount = static_cast<uint64_t>(indexCount / 3u);

    if (triCount == 0 || vertexCount == 0) {
        return;
    }

    // Guard against extremely large meshes that would require very large BLAS
    // buffers. These meshes will still render in the raster path but will not
    // participate in RT shadows/reflections/GI.
    const uint64_t approxVertexBytes = static_cast<uint64_t>(vertexCount) * sizeof(Vertex);
    const uint64_t approxIndexBytes  = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
    if (triCount > kMaxRTTrianglesPerMesh ||
        approxVertexBytes > kMaxRTMeshBytes ||
        approxIndexBytes > kMaxRTMeshBytes) {
        spdlog::warn(
            "DX12RaytracingContext: skipping BLAS for large mesh (verts={}, indices={}, tris~{})",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount),
            triCount);
        return;
    }

    const Scene::MeshData* key = mesh.get();

    // CRITICAL: Check if an entry already exists at this key.
    // Due to memory reuse, a new MeshData may get the same address as a previously
    // deleted one. If so, the old entry contains stale BLAS/scratch resources that
    // the GPU may still be referencing. We MUST defer their deletion to prevent
    // D3D12 error 921 (OBJECT_DELETED_WHILE_STILL_IN_USE).
    auto existingIt = m_blasCache.find(key);
    if (existingIt != m_blasCache.end()) {
        BLASEntry& oldEntry = existingIt->second;
        if (oldEntry.blas) {
            // Queue old BLAS for deferred deletion (N frames from now)
            DeferredGPUDeletionQueue::Instance().QueueResource(std::move(oldEntry.blas));
            spdlog::debug("DX12RaytracingContext: deferred deletion of stale BLAS at reused address {:p}",
                          static_cast<const void*>(key));
        }
        if (oldEntry.scratch) {
            // Queue old scratch buffer for deferred deletion
            DeferredGPUDeletionQueue::Instance().QueueResource(std::move(oldEntry.scratch));
        }
        // Clear the entry to prevent any stale state
        oldEntry = BLASEntry{};
    }

    auto& entry = m_blasCache[key];

    D3D12_RAYTRACING_GEOMETRY_DESC geom{};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& tri = geom.Triangles;
    tri.Transform3x4 = 0; // No per-geometry transform; instances use world matrices.
    tri.IndexFormat = DXGI_FORMAT_R32_UINT;
    tri.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    tri.IndexCount = static_cast<UINT>(mesh->indices.size());
    tri.VertexCount = static_cast<UINT>(mesh->positions.size());
    tri.IndexBuffer = mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
    tri.VertexBuffer.StartAddress = mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
    tri.VertexBuffer.StrideInBytes = static_cast<UINT>(sizeof(Vertex));

    entry.geometryDesc = geom;
    entry.hasGeometry = (tri.VertexCount > 0 && tri.IndexCount > 0);
    entry.built = false;
    entry.buildRequested = false;
}

void DX12RaytracingContext::BuildSingleBLAS(const Scene::MeshData* meshKey) {
    if (!m_device5 || !meshKey) {
        return;
    }

    auto it = m_blasCache.find(meshKey);
    if (it == m_blasCache.end()) {
        return;
    }
    BLASEntry& blasEntry = it->second;
    if (!blasEntry.hasGeometry || blasEntry.blas) {
        return;
    }

    // Mark this BLAS as requested; BuildTLAS will perform the heavy build
    // work using the current frame's command list and per-frame budgets.
    blasEntry.buildRequested = true;
}

uint32_t DX12RaytracingContext::GetPendingBLASCount() const {
    uint32_t count = 0;
    for (const auto& kv : m_blasCache) {
        const BLASEntry& e = kv.second;
        if (e.hasGeometry && !e.blas && e.buildRequested) {
            ++count;
        }
    }
    return count;
}

void DX12RaytracingContext::ReleaseBLASForMesh(const Scene::MeshData* meshKey) {
    if (!meshKey) {
        return;
    }

    auto it = m_blasCache.find(meshKey);
    if (it == m_blasCache.end()) {
        return;
    }

    BLASEntry& entry = it->second;
    if (entry.blas) {
        // Scratch memory is reclaimed separately once its build frame is
        // known complete, so subtract only the persistent BLAS result here.
        const uint64_t blasBytes = entry.blasSize;
        if (blasBytes > 0 && m_totalBLASBytes >= blasBytes) {
            m_totalBLASBytes -= blasBytes;
        }
        // Use deferred deletion to prevent D3D12 error 921
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(entry.blas));
        spdlog::info("DX12RaytracingContext: deferred BLAS release for mesh {} (kept scratch buffer)",
                     static_cast<const void*>(meshKey));
    }

    // Leave the cache entry in place until a scene clear so any scratch buffer
    // still waiting for frame-based reclamation can be released safely.
    entry.hasGeometry = false;
    entry.built = false;
}

void DX12RaytracingContext::ClearAllBLAS() {
    // Release all BLAS resources and clear the cache.
    // Call this during scene switches to ensure no stale entries remain
    // that could reference freed GPU resources.
    // Use deferred deletion to prevent D3D12 error 921 in case GPU is still
    // referencing these resources from in-flight command lists.
    for (auto& kv : m_blasCache) {
        BLASEntry& entry = kv.second;
        uint64_t bytes = 0;
        if (entry.blas) {
            bytes += entry.blasSize;
        }
        if (entry.scratch) {
            bytes += entry.scratchSize;
        }
        if (bytes > 0) {
            m_totalBLASBytes = (m_totalBLASBytes >= bytes) ? (m_totalBLASBytes - bytes) : 0;
        }
        if (entry.blas) {
            DeferredGPUDeletionQueue::Instance().QueueResource(std::move(entry.blas));
        }
        if (entry.scratch) {
            DeferredGPUDeletionQueue::Instance().QueueResource(std::move(entry.scratch));
        }
    }
    m_blasCache.clear();

    // Also clear TLAS and instance buffer since they contain references to
    // the BLAS entries we just cleared. The next BuildTLAS() call will
    // recreate them from scratch with the new scene's geometry.
    // Use deferred deletion for TLAS resources as well.
    if (m_tlas) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(m_tlas));
    }
    if (m_tlasScratch) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(m_tlasScratch));
    }
    if (m_instanceBuffer) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(m_instanceBuffer));
    }
    if (m_rtMaterialBuffer) {
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(m_rtMaterialBuffer));
    }
    m_instanceBufferSize = 0;
    m_rtMaterialBufferSize = 0;
    m_tlasSize = 0;
    m_tlasScratchSize = 0;
    m_totalTLASBytes = 0;
    // Also clear pending resize requests since we just destroyed everything
    m_instanceBufferPendingSize = 0;
    m_tlasPendingSize = 0;
    m_tlasScratchPendingSize = 0;
    m_instanceDescs.clear();
    m_rtMaterials.clear();

    spdlog::info("DX12RaytracingContext: cleared all BLAS/TLAS for scene switch (deferred)");
}

void DX12RaytracingContext::ReleaseScratchBuffers(uint64_t completedFrameIndex) {
    if (completedFrameIndex == 0) {
        return;
    }

    uint64_t releasedBytes = 0;
    uint32_t releasedCount = 0;

    for (auto& kv : m_blasCache) {
        BLASEntry& entry = kv.second;
        if (!entry.scratch || !entry.built || entry.buildFrameIndex > completedFrameIndex) {
            continue;
        }

        releasedBytes += entry.scratchSize;
        if (m_totalBLASBytes >= entry.scratchSize) {
            m_totalBLASBytes -= entry.scratchSize;
        }
        entry.scratchSize = 0;
        DeferredGPUDeletionQueue::Instance().QueueResource(std::move(entry.scratch));
        ++releasedCount;
    }

    if (releasedCount > 0) {
        const double releasedMB = static_cast<double>(releasedBytes) / (1024.0 * 1024.0);
        spdlog::info("DX12RaytracingContext: released {} BLAS scratch buffers (~{:.1f} MB)",
                     releasedCount,
                     releasedMB);
    }
}

} // namespace Cortex::Graphics
