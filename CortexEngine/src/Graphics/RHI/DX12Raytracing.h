#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex {
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

class DX12Device;
class DescriptorHeapManager;

// DXR context used as a growth point for BLAS/TLAS and ray tracing passes.
// In this pass, BLAS/TLAS are built but no ray tracing shaders are dispatched.
class DX12RaytracingContext {
public:
    DX12RaytracingContext() = default;
    ~DX12RaytracingContext() = default;

    DX12RaytracingContext(const DX12RaytracingContext&) = delete;
    DX12RaytracingContext& operator=(const DX12RaytracingContext&) = delete;
    DX12RaytracingContext(DX12RaytracingContext&&) = default;
    DX12RaytracingContext& operator=(DX12RaytracingContext&&) = default;

    Result<void> Initialize(DX12Device* device, DescriptorHeapManager* descriptors);
    void Shutdown();

    void OnResize(uint32_t width, uint32_t height);

    // Register geometry for a mesh and prepare BLAS build parameters. The
    // actual GPU build is deferred until TLAS construction when we have a
    // command list available.
    void RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh,
                            D3D12_GPU_VIRTUAL_ADDRESS vertexBufferVA,
                            UINT vertexStride,
                            D3D12_GPU_VIRTUAL_ADDRESS indexBufferVA,
                            DXGI_FORMAT indexFormat,
                            UINT indexCount);

    // Build or update the TLAS for the current scene. Uses the provided
    // graphics command list to record BLAS/TLAS build commands.
    void BuildTLAS(Scene::ECS_Registry* registry, ID3D12GraphicsCommandList4* cmdList);

    void DispatchRayTracing(ID3D12GraphicsCommandList4* cmdList);

private:
    struct BLASEntry {
        ComPtr<ID3D12Resource> blas;
        ComPtr<ID3D12Resource> scratch;
        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{};
        bool hasGeometry = false;
    };

    void BuildBLASIfNeeded(BLASEntry& entry, ID3D12GraphicsCommandList4* cmdList);

    ComPtr<ID3D12Device5> m_device5;
    uint32_t m_rtxWidth = 0;
    uint32_t m_rtxHeight = 0;

    // Per-mesh BLAS cache keyed by MeshData pointer
    std::unordered_map<const Scene::MeshData*, BLASEntry> m_blasCache;

    // TLAS and associated resources
    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    ComPtr<ID3D12Resource> m_instanceBuffer;
    uint64_t m_tlasSize = 0;
    uint64_t m_tlasScratchSize = 0;
    uint64_t m_instanceBufferSize = 0;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instanceDescs;
};

} // namespace Cortex::Graphics

