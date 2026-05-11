#include "Renderer.h"

#include "Graphics/Passes/MeshUploadCopyPass.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

namespace {
void ReplaceMeshWithBoundsProxy(Scene::MeshData& mesh) {
    if (!mesh.hasBounds) {
        mesh.UpdateBounds();
    }

    glm::vec3 minP = mesh.hasBounds ? mesh.boundsMin : glm::vec3(-0.5f);
    glm::vec3 maxP = mesh.hasBounds ? mesh.boundsMax : glm::vec3(0.5f);
    if (glm::any(glm::lessThanEqual(maxP - minP, glm::vec3(0.0f)))) {
        minP = glm::vec3(-0.5f);
        maxP = glm::vec3(0.5f);
    }

    mesh.ResetGPUResources();
    mesh.positions = {
        {minP.x, minP.y, minP.z}, {maxP.x, minP.y, minP.z},
        {maxP.x, maxP.y, minP.z}, {minP.x, maxP.y, minP.z},
        {minP.x, minP.y, maxP.z}, {maxP.x, minP.y, maxP.z},
        {maxP.x, maxP.y, maxP.z}, {minP.x, maxP.y, maxP.z}
    };
    mesh.normals.assign(mesh.positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
    mesh.texCoords.assign(mesh.positions.size(), glm::vec2(0.0f));
    mesh.colors.clear();
    mesh.indices = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 1, 5, 0, 5, 4,
        3, 6, 2, 3, 7, 6,
        1, 2, 6, 1, 6, 5,
        0, 4, 7, 0, 7, 3
    };
    mesh.UpdateBounds();
}

} // namespace

Result<void> Renderer::UploadMesh(std::shared_ptr<Scene::MeshData> mesh) {
    if (m_frameLifecycle.deviceRemoved) {
        return Result<void>::Err("DX12 device has been removed; cannot upload mesh");
    }

    if (!mesh) {
        return Result<void>::Err("Invalid mesh pointer");
    }

    if (mesh->positions.empty() || mesh->indices.empty()) {
        return Result<void>::Err("Mesh has no vertex or index data");
    }

    // Ensure bounds exist for CPU/GPU culling paths (loaders may not compute them).
    if (!mesh->hasBounds) {
        mesh->UpdateBounds();
    }

    // Interleave vertex data (position, normal, tangent, texcoord)
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->positions.size());

    // Generate tangents for normal mapping
    std::vector<glm::vec3> tangents(mesh->positions.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(mesh->positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const uint32_t i0 = mesh->indices[i + 0];
        const uint32_t i1 = mesh->indices[i + 1];
        const uint32_t i2 = mesh->indices[i + 2];

        const glm::vec3& p0 = mesh->positions[i0];
        const glm::vec3& p1 = mesh->positions[i1];
        const glm::vec3& p2 = mesh->positions[i2];

        const glm::vec2 uv0 = i0 < mesh->texCoords.size() ? mesh->texCoords[i0] : glm::vec2(0.0f);
        const glm::vec2 uv1 = i1 < mesh->texCoords.size() ? mesh->texCoords[i1] : glm::vec2(0.0f);
        const glm::vec2 uv2 = i2 < mesh->texCoords.size() ? mesh->texCoords[i2] : glm::vec2(0.0f);

        const glm::vec3 edge1 = p1 - p0;
        const glm::vec3 edge2 = p2 - p0;
        const glm::vec2 dUV1 = uv1 - uv0;
        const glm::vec2 dUV2 = uv2 - uv0;

        const float denom = (dUV1.x * dUV2.y - dUV1.y * dUV2.x);
        if (std::abs(denom) < 1e-6f) {
            continue;
        }
        const float f = 1.0f / denom;
        // Standard tangent/bitangent from partial derivatives to preserve handedness for mirrored UVs
        glm::vec3 tangent = f * (edge1 * dUV2.y - edge2 * dUV1.y);
        glm::vec3 bitangent = f * (edge2 * dUV1.x - edge1 * dUV2.x);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;

        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    for (size_t i = 0; i < mesh->positions.size(); ++i) {
        Vertex v;
        v.position = mesh->positions[i];
        v.normal = i < mesh->normals.size() ? mesh->normals[i] : glm::vec3(0, 1, 0);
        glm::vec3 tangent = tangents[i];
        glm::vec3 bitangent = bitangents[i];
        if (glm::length2(tangent) < 1e-6f) {
            // Build an arbitrary orthogonal tangent if UVs were degenerate
            glm::vec3 up = std::abs(v.normal.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            tangent = glm::normalize(glm::cross(up, v.normal));
            bitangent = glm::cross(v.normal, tangent);
        } else {
            tangent = glm::normalize(tangent - v.normal * glm::dot(v.normal, tangent));
        }
        float sign = 1.0f;
        if (glm::length2(bitangent) > 1e-6f) {
            sign = glm::dot(glm::cross(v.normal, tangent), glm::normalize(bitangent)) < 0.0f ? -1.0f : 1.0f;
        }
        v.tangent = glm::vec4(tangent, sign);
        v.texCoord = i < mesh->texCoords.size() ? mesh->texCoords[i] : glm::vec2(0, 0);
        v.color = i < mesh->colors.size() ? mesh->colors[i] : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        vertices.push_back(v);
    }

    auto* device = m_services.device ? m_services.device->GetDevice() : nullptr;
    if (!device || !m_services.commandQueue) {
        return Result<void>::Err("Renderer is not initialized");
    }

    const UINT64 vertexCount = static_cast<UINT64>(vertices.size());
    const UINT64 indexCount  = static_cast<UINT64>(mesh->indices.size());
    const UINT64 vbSize = vertexCount * static_cast<UINT64>(sizeof(Vertex));
    const UINT64 ibSize = indexCount  * static_cast<UINT64>(sizeof(uint32_t));

    if (vbSize == 0 || ibSize == 0) {
        spdlog::error(
            "UploadMesh called with empty geometry: vertices={} indices={}",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount));
        return Result<void>::Err("Mesh has no vertices or indices");
    }

    // Log per-mesh GPU buffer footprint to help diagnose large assets.
    const double vbMB = static_cast<double>(vbSize) / (1024.0 * 1024.0);
    const double ibMB = static_cast<double>(ibSize) / (1024.0 * 1024.0);
    spdlog::debug(
        "UploadMesh: vertices={} indices={} (VB~{:.2f} MB, IB~{:.2f} MB)",
        static_cast<uint64_t>(vertexCount),
        static_cast<uint64_t>(indexCount),
        vbMB,
        ibMB);

    // Hard guardrails for pathological meshes so a single glTF cannot
    // allocate multi-GB vertex/index buffers and trigger device-removed.
    constexpr UINT64 kMaxMeshVertices = 10'000'000ull;  // ~10M verts
    constexpr UINT64 kMaxMeshIndices  = 30'000'000ull;  // ~10M tris
    constexpr UINT64 kMaxMeshVBBytes  = 512ull * 1024ull * 1024ull; // ~512 MB
    constexpr UINT64 kMaxMeshIBBytes  = 512ull * 1024ull * 1024ull; // ~512 MB

    if (vertexCount > kMaxMeshVertices ||
        indexCount  > kMaxMeshIndices  ||
        vbSize      > kMaxMeshVBBytes  ||
        ibSize      > kMaxMeshIBBytes) {
        spdlog::warn(
            "UploadMesh: mesh exceeds single-buffer upload budget; using bounds proxy "
            "until streaming LOD upload is available (verts={} indices={} VB~{:.2f} MB IB~{:.2f} MB)",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount),
            vbMB,
            ibMB);
        ReplaceMeshWithBoundsProxy(*mesh);
        return UploadMesh(mesh);
    }

    if (!m_services.uploadQueue) {
        return Result<void>::Err("Upload queue is unavailable; cannot upload mesh to GPU-local memory");
    }

    // Store final geometry in DEFAULT heap memory. A short-lived upload heap
    // stages the CPU data, then the copy queue transfers it into GPU-local
    // buffers. Resource allocation, staging writes, and raw SRV creation live
    // in the upload resource state; this function keeps mesh policy and queueing.
    auto resourceResult = MeshUploadResourceState::CreateResources(
        device,
        m_services.descriptorManager.get(),
        vertices,
        mesh->indices);
    if (resourceResult.IsErr()) {
        const auto& error = resourceResult.Error();
        if (FAILED(error.hr) && error.deviceRemovedContext) {
            CORTEX_REPORT_DEVICE_REMOVED(error.deviceRemovedContext, error.hr);
        }
        if (FAILED(error.hr)) {
            spdlog::error("{}: hr=0x{:08X}, vbSize={}, vertices={}",
                          error.message,
                          static_cast<unsigned int>(error.hr),
                          vbSize,
                          vertices.size());
        }
        return Result<void>::Err(error.message);
    }
    auto uploadResources = std::move(resourceResult).Value();
    if (!uploadResources.descriptorWarning.empty()) {
        spdlog::warn("{}", uploadResources.descriptorWarning);
    }

    const uint32_t uploadIndex = m_uploadCommands.allocatorIndex++ % UploadCommandPoolState::kPoolSize;
    if (!m_uploadCommands.commandAllocators[uploadIndex] || !m_uploadCommands.commandLists[uploadIndex]) {
        return Result<void>::Err("Upload command list pool is not initialized");
    }
    if (m_uploadCommands.fences[uploadIndex] != 0 && !m_services.uploadQueue->IsFenceComplete(m_uploadCommands.fences[uploadIndex])) {
        if (!m_services.uploadQueue->WaitForFenceValue(m_uploadCommands.fences[uploadIndex])) {
            return Result<void>::Err("Timed out waiting for reusable mesh upload command allocator");
        }
    }

    HRESULT hr = MeshUploadCopyPass::RecordBufferCopies({
        m_uploadCommands.commandAllocators[uploadIndex].Get(),
        m_uploadCommands.commandLists[uploadIndex].Get(),
        uploadResources.gpuBuffers->vertexBuffer.Get(),
        uploadResources.vertexUpload.Get(),
        vbSize,
        uploadResources.gpuBuffers->indexBuffer.Get(),
        uploadResources.indexUpload.Get(),
        ibSize,
    });
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to record mesh upload command list");
    }

    m_services.uploadQueue->ExecuteCommandList(m_uploadCommands.commandLists[uploadIndex].Get());
    const uint64_t uploadFence = m_services.uploadQueue->Signal();
    m_uploadCommands.fences[uploadIndex] = uploadFence;
    if (!m_services.uploadQueue->WaitForFenceValue(uploadFence)) {
        return Result<void>::Err("Timed out waiting for mesh upload copy queue");
    }
    m_uploadCommands.fences[uploadIndex] = 0;

    // CRITICAL: If mesh already has GPU buffers (e.g., re-upload), defer deletion
    // of old buffers to prevent D3D12 Error 921 (OBJECT_DELETED_WHILE_STILL_IN_USE).
    // Simple assignment would immediately release old buffers which may still be
    // referenced by in-flight GPU commands.
    if (mesh->gpuBuffers) {
        DeferMeshBuffersDeletion(mesh->gpuBuffers);
    }
    mesh->gpuBuffers = uploadResources.gpuBuffers;

    // Register approximate geometry footprint in the asset registry so the
    // memory inspector can surface heavy meshes, and cache the mapping from
    // MeshData pointer to asset key for later ref-count rebuild / BLAS pruning.
    {
        char keyBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "mesh@%p", static_cast<const void*>(mesh.get()));
        const std::string key(keyBuf);
        m_assetRuntime.registry.RegisterMesh(key, vbSize, ibSize);
        m_assetRuntime.meshAssetKeys[mesh.get()] = key;
    }

    // Register geometry with the ray tracing context and enqueue a BLAS build
    // job so RT acceleration structures can converge incrementally. When ray
    // tracing is disabled at runtime we skip BLAS work entirely to avoid
    // consuming acceleration-structure memory on 8 GB-class GPUs.
    if (m_rtRuntimeState.supported && m_services.rayTracingContext && m_rtRuntimeState.enabled) {
        m_services.rayTracingContext->RebuildBLASForMesh(mesh);

        GpuJob job{};
        job.type = GpuJobType::BuildBLAS;
        job.blasMeshKey = mesh.get();
        job.label = "BLAS";
        m_assetRuntime.gpuJobs.pendingJobs.push_back(job);
        ++m_assetRuntime.gpuJobs.pendingBLASJobs;
    }

    spdlog::info("Mesh uploaded to GPU-local buffers: {} vertices, {} indices", vertices.size(), mesh->indices.size());
    return Result<void>::Ok();
}

Result<void> Renderer::EnqueueMeshUpload(const std::shared_ptr<Scene::MeshData>& mesh,
                                         const char* label)
{
    if (m_frameLifecycle.deviceRemoved) {
        return Result<void>::Err("DX12 device has been removed; cannot enqueue mesh upload");
    }

    if (!mesh) {
        return Result<void>::Err("EnqueueMeshUpload called with null mesh");
    }

    GpuJob job;
    job.type = GpuJobType::MeshUpload;
    job.mesh = mesh;
    job.label = label ? label : "MeshUpload";
    m_assetRuntime.gpuJobs.pendingJobs.push_back(std::move(job));
    ++m_assetRuntime.gpuJobs.pendingMeshJobs;

    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics

