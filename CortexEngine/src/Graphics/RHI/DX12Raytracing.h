#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include "Utils/Result.h"
#include "DescriptorHeap.h"

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
// In this pass, BLAS/TLAS are built on the GPU when ray tracing is enabled;
// ray generation shaders are still stubbed out. The goal is to keep this
// context self-contained and quiet in logs so it can be enabled as an
// optional feature without impacting non-RT builds.
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
    // command list available. Mesh lifetime is owned by the scene/renderer;
    // we key the cache by MeshData* and assume static geometry for now.
    void RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh);

    // Build or update the TLAS for the current scene. Uses the provided
    // graphics command list to record BLAS/TLAS build commands.
    void BuildTLAS(Scene::ECS_Registry* registry, ID3D12GraphicsCommandList4* cmdList);

    // Dispatch the ray-tracing pipeline to write a sun-shadow mask. The
    // renderer provides the depth SRV, RT shadow mask UAV, and the GPU
    // address of the frame-constant buffer so this context remains decoupled
    // from higher-level state.
    void DispatchRayTracing(
        ID3D12GraphicsCommandList4* cmdList,
        const DescriptorHandle& depthSrv,
        const DescriptorHandle& shadowMaskUav,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress);

private:
    struct BLASEntry {
        ComPtr<ID3D12Resource> blas;
        ComPtr<ID3D12Resource> scratch;
        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{};
        bool hasGeometry = false; // true when geomDesc is valid and needs (re)build
    };

    void BuildBLASIfNeeded(BLASEntry& entry, ID3D12GraphicsCommandList4* cmdList);

    ComPtr<ID3D12Device5> m_device5;
    DescriptorHeapManager* m_descriptors = nullptr; // non-owning
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

    // DXR pipeline for sun shadows
    ComPtr<ID3D12RootSignature> m_rtGlobalRootSignature;
    ComPtr<ID3D12StateObject> m_rtStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtStateProps;
    ComPtr<ID3D12Resource> m_rtShaderTable;
    UINT m_rtShaderTableStride = 0;

    // Persistent descriptors for TLAS, depth, and RT shadow mask in space2
    DescriptorHandle m_rtTlasSrv{};   // t0, space2
    DescriptorHandle m_rtDepthSrv{};  // t1, space2
    DescriptorHandle m_rtMaskUav{};   // u0, space2
};

} // namespace Cortex::Graphics
