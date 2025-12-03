#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>
#include <glm/glm.hpp>

#include "Utils/Result.h"
#include "Graphics/RHI/DescriptorHeap.h"

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

// DXR context responsible for:
//  - Caching BLAS per unique MeshData
//  - Building a TLAS over the current ECS scene each frame
//  - Hosting a simple ray tracing pipeline that writes a sun-shadow mask
//    into a UAV texture, to be sampled by the main PBR shader.
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

    // Notify RT context of render-target resolution changes so DispatchRays
    // can match the main color/depth buffers.
    void OnResize(uint32_t width, uint32_t height);

    // Register geometry for BLAS building. The mesh is identified by the
    // MeshData pointer; UploadMesh is expected to populate gpuBuffers with
    // valid vertex and index buffers before calling this.
    void RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh);

    // Build or update TLAS for the current frame using the provided command
    // list. This also lazily builds BLAS instances as needed.
    void BuildTLAS(Scene::ECS_Registry* registry, ID3D12GraphicsCommandList4* cmdList);

    // Dispatch the ray-traced sun-shadow pass. The caller is responsible for:
    //  - Ensuring the TLAS has been built for this frame.
    //  - Providing a persistent depth SRV descriptor (matching g_Depth in HLSL).
    //  - Providing a persistent UAV descriptor for the RT shadow mask target.
    //  - Passing the GPU virtual address of the FrameConstants constant buffer.
    //  - Providing the SRV table for shadow/IBL/RT textures (space1, t0-t6).
    void DispatchRayTracing(
        ID3D12GraphicsCommandList4* cmdList,
        const DescriptorHandle& depthSrv,
        const DescriptorHandle& shadowMaskUav,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
        const DescriptorHandle& shadowEnvTable);

    // Dispatch the ray-traced reflections pass. The caller is responsible for:
    //  - Ensuring the TLAS has been built for this frame.
    //  - Providing a persistent depth SRV descriptor (matching g_Depth in HLSL).
    //  - Providing a persistent UAV descriptor for the RT reflection color target.
    //  - Passing the GPU virtual address of the FrameConstants constant buffer.
    //  - Providing the SRV table for shadow/IBL/RT textures (space1, t0-t6).
    //  - Providing the G-buffer normal/roughness SRV for proper surface normals.
    void DispatchReflections(
        ID3D12GraphicsCommandList4* cmdList,
        const DescriptorHandle& depthSrv,
        const DescriptorHandle& reflectionUav,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
        const DescriptorHandle& shadowEnvTable,
        const DescriptorHandle& normalRoughnessSrv);

    // Dispatch a simple RT diffuse GI pass. The caller is responsible for:
    //  - Ensuring the TLAS has been built for this frame.
    //  - Providing a persistent depth SRV descriptor (matching g_Depth in HLSL).
    //  - Providing a persistent UAV descriptor for the RT GI buffer.
    //  - Passing the GPU virtual address of the FrameConstants constant buffer.
    //  - Providing the SRV table for shadow/IBL/RT textures (space1, t0-t6).
    void DispatchGI(
        ID3D12GraphicsCommandList4* cmdList,
        const DescriptorHandle& depthSrv,
        const DescriptorHandle& giUav,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
        const DescriptorHandle& shadowEnvTable);

    // Cache camera parameters for TLAS culling and distance-based RT tuning.
    void SetCameraParams(const glm::vec3& positionWS,
                         const glm::vec3& forwardWS,
                         float nearPlane,
                         float farPlane);

    // Approximate GPU memory footprint of all ray tracing acceleration
    // structures (BLAS + TLAS + scratch). This value is updated when BLAS/TLAS
    // buffers are allocated or resized so the renderer can incorporate it into
    // VRAM budgeting and HUD estimates without walking all resources.
    [[nodiscard]] uint64_t GetAccelerationStructureBytes() const {
        return m_totalBLASBytes + m_totalTLASBytes;
    }

    [[nodiscard]] bool HasPipeline() const {
        // A usable pipeline requires a built state object, shader table, and
        // the persistent descriptor slots used for TLAS/depth/mask binding.
        return m_rtStateObject && m_rtStateProps && m_rtShaderTable &&
               m_rtTlasSrv.IsValid() && m_rtDepthSrv.IsValid() && m_rtMaskUav.IsValid();
    }

    [[nodiscard]] bool HasReflectionPipeline() const {
        return m_rtReflStateObject && m_rtReflStateProps && m_rtReflShaderTable;
    }

    [[nodiscard]] bool HasGIPipeline() const {
        return m_rtGIStateObject && m_rtGIStateProps && m_rtGIShaderTable;
    }

    // Incremental BLAS helpers used by the renderer's GPU job queue so BLAS
    // builds can be spread across multiple frames.
    void BuildSingleBLAS(const Scene::MeshData* meshKey);
    [[nodiscard]] uint32_t GetPendingBLASCount() const;
    // Explicitly release BLAS memory for a mesh that is no longer referenced
    // by any renderables, so RT acceleration structures do not accumulate
    // across scene rebuilds.
    void ReleaseBLASForMesh(const Scene::MeshData* meshKey);

private:
    struct BLASEntry {
        bool hasGeometry = false;
        bool built = false;
        bool buildRequested = false;

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};

        ComPtr<ID3D12Resource> blas;
        ComPtr<ID3D12Resource> scratch;

        UINT64 blasSize = 0;
        UINT64 scratchSize = 0;
    };

    ComPtr<ID3D12Device5> m_device5;
    DescriptorHeapManager* m_descriptors = nullptr;

    uint32_t m_rtxWidth = 0;
    uint32_t m_rtxHeight = 0;

    // Cached BLAS per unique mesh.
    std::unordered_map<const Scene::MeshData*, BLASEntry> m_blasCache;
    // Approximate total GPU memory consumed by all BLAS buffers (result +
    // scratch). Updated as BLAS resources are allocated.
    uint64_t m_totalBLASBytes = 0;

    // TLAS and instance data.
    ComPtr<ID3D12Resource> m_instanceBuffer;
    UINT64 m_instanceBufferSize = 0;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instanceDescs;

    // Cached camera information used for simple near/far culling when building
    // the TLAS so we do not trace rays against obviously off-screen geometry.
    glm::vec3 m_cameraPosWS{0.0f};
    glm::vec3 m_cameraForwardWS{0.0f, 0.0f, 1.0f};
    float     m_cameraNearPlane = 0.1f;
    float     m_cameraFarPlane  = 1000.0f;
    bool      m_hasCamera = false;

    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    UINT64 m_tlasSize = 0;
    UINT64 m_tlasScratchSize = 0;
    // Approximate total GPU memory consumed by TLAS + scratch buffer.
    uint64_t m_totalTLASBytes = 0;

    // RT pipeline for sun shadows.
    ComPtr<ID3D12RootSignature> m_rtGlobalRootSignature;
    ComPtr<ID3D12StateObject> m_rtStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtStateProps;
    ComPtr<ID3D12Resource> m_rtShaderTable;
    UINT m_rtShaderTableStride = 0;

    // RT pipeline for reflections (hybrid SSR/RT path). Shares the global root
    // signature but uses a separate state object and shader table.
    ComPtr<ID3D12StateObject> m_rtReflStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtReflStateProps;
    ComPtr<ID3D12Resource> m_rtReflShaderTable;
    UINT m_rtReflShaderTableStride = 0;

    // RT pipeline for diffuse global illumination. Shares the global root
    // signature but uses its own state object and shader table.
    ComPtr<ID3D12StateObject> m_rtGIStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtGIStateProps;
    ComPtr<ID3D12Resource> m_rtGIShaderTable;
    UINT m_rtGIShaderTableStride = 0;

    // Persistent descriptors used to bind the TLAS SRV, depth SRV, and
    // RT shadow mask UAV for DispatchRays. The reflections pipeline reuses
    // the same TLAS/depth/UAV slots, with the UAV pointing at the reflection
    // color buffer instead of the sun-shadow mask.
    DescriptorHandle m_rtTlasSrv;
    DescriptorHandle m_rtDepthSrv;
    DescriptorHandle m_rtMaskUav;
    DescriptorHandle m_rtGBufferNormalSrv;  // G-buffer normal/roughness for proper RT reflections
};

} // namespace Cortex::Graphics
