#pragma once

#include "D3D12Includes.h"
#include <wrl/client.h>

#include <cstdint>
#include <functional>
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
        struct RenderableComponent;
    }
}

namespace Cortex::Graphics {

class DX12Device;
class DescriptorHeapManager;
struct MaterialModel;

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
    struct TLASBuildInput {
        uint32_t stableId = 0;
        const Scene::RenderableComponent* renderable = nullptr;
        glm::mat4 worldMatrix{1.0f};
        float maxWorldScale = 1.0f;
    };
    void BuildTLAS(const std::vector<TLASBuildInput>& inputs, ID3D12GraphicsCommandList4* cmdList);

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
        const DescriptorHandle& normalRoughnessSrv,
        const DescriptorHandle& materialExt2Srv,
        ID3D12Resource* normalRoughnessResource,
        ID3D12Resource* materialExt2Resource,
        uint32_t dispatchWidth,
        uint32_t dispatchHeight);

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
        const DescriptorHandle& shadowEnvTable,
        uint32_t dispatchWidth,
        uint32_t dispatchHeight);

    // Cache camera parameters for TLAS culling and distance-based RT tuning.
    void SetCameraParams(const glm::vec3& positionWS,
                         const glm::vec3& forwardWS,
                         float nearPlane,
                         float farPlane);

    // Set the current frame index for tracking BLAS build timing. This must be
    // called each frame before BuildTLAS so we know which frame's scratch
    // buffers are safe to release.
    void SetCurrentFrameIndex(uint64_t frameIndex) { m_currentFrameIndex = frameIndex; }
    void SetAccelerationStructureBudgets(uint64_t maxBLASBytesTotal,
                                         uint64_t maxBLASBuildBytesPerFrame);

    // Set a callback function that forces a GPU sync. This is used to safely
    // resize buffers when the GPU might still be using the old buffer.
    using FlushCallback = std::function<void()>;
    void SetFlushCallback(FlushCallback callback) { m_flushCallback = std::move(callback); }

    // Approximate GPU memory footprint of all ray tracing acceleration
    // structures (BLAS + TLAS + scratch). This value is updated when BLAS/TLAS
    // buffers are allocated or resized so the renderer can incorporate it into
    // VRAM budgeting and HUD estimates without walking all resources.
    [[nodiscard]] uint64_t GetAccelerationStructureBytes() const {
        return m_totalBLASBytes + m_totalTLASBytes;
    }

    struct TLASBuildStats {
        uint32_t candidates = 0;
        uint32_t skippedInvisibleOrInvalid = 0;
        uint32_t missingGeometry = 0;
        uint32_t distanceCulled = 0;
        uint32_t blasBuildRequested = 0;
        uint32_t blasBuildBudgetDeferred = 0;
        uint32_t blasTotalBudgetSkipped = 0;
        uint32_t blasBuildFailed = 0;
        uint32_t emittedInstances = 0;
        uint32_t materialRecords = 0;
        uint64_t materialBufferBytes = 0;
        uint32_t surfaceDefault = 0;
        uint32_t surfaceGlass = 0;
        uint32_t surfaceMirror = 0;
        uint32_t surfacePlastic = 0;
        uint32_t surfaceMasonry = 0;
        uint32_t surfaceEmissive = 0;
        uint32_t surfaceBrushedMetal = 0;
        uint32_t surfaceWood = 0;
        uint32_t surfaceWater = 0;
    };

    [[nodiscard]] const TLASBuildStats& GetLastTLASBuildStats() const {
        return m_lastTLASStats;
    }

    [[nodiscard]] uint32_t GetLastTLASInstanceCount() const {
        return static_cast<uint32_t>(m_instanceDescs.size());
    }

    [[nodiscard]] uint32_t GetLastRTMaterialCount() const {
        return static_cast<uint32_t>(m_rtMaterials.size());
    }

    [[nodiscard]] uint64_t GetRTMaterialBufferBytes() const {
        return m_rtMaterialBuffer ? static_cast<uint64_t>(m_rtMaterialBufferSize) : 0ull;
    }

    [[nodiscard]] bool HasPipeline() const { 
        // A usable pipeline requires a built state object + shader table.
        // Descriptor binding for TLAS/depth/output uses persistent
        // per-frame/per-pass tables to avoid cross-frame aliasing.
        return m_rtStateObject && m_rtStateProps && m_rtShaderTable; 
    } 

    [[nodiscard]] bool HasReflectionPipeline() const {
        return m_rtReflStateObject && m_rtReflStateProps && m_rtReflShaderTable;
    }

    [[nodiscard]] bool HasTLAS() const {
        return m_tlas != nullptr;
    }

    [[nodiscard]] bool HasRTMaterialBuffer() const {
        return m_rtMaterialBuffer != nullptr;
    }

    [[nodiscard]] bool HasDispatchDescriptorTables() const {
        return m_dispatchDescriptorTablesValid;
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
    // Clear all BLAS cache entries. Call this during scene switches to prevent
    // dangling pointer issues when MeshData addresses are reused.
    void ClearAllBLAS();

    // Release scratch buffers for BLAS entries that have finished building.
    // Call this after the GPU has completed the frame's work (e.g., after
    // WaitForFenceValue in BeginFrame) to safely reclaim scratch memory.
    // The completedFrameIndex parameter should be the frame index that has
    // definitely completed on the GPU (typically currentFrameIndex - BUFFER_COUNT).
    void ReleaseScratchBuffers(uint64_t completedFrameIndex);

private:
    struct BLASEntry {
        bool hasGeometry = false;
        bool built = false;
        bool buildRequested = false;

        // Frame index when the BLAS build command was recorded. The scratch
        // buffer must not be released until this frame has completed on the GPU.
        uint64_t buildFrameIndex = 0;

        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};

        ComPtr<ID3D12Resource> blas;
        ComPtr<ID3D12Resource> scratch;

        UINT64 blasSize = 0;
        UINT64 scratchSize = 0;
    };

    struct RTMaterialGPU {
        glm::vec4 albedoMetallic{1.0f, 1.0f, 1.0f, 0.0f};
        glm::vec4 emissiveRoughness{0.0f, 0.0f, 0.0f, 0.5f};
        glm::vec4 params{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4 classification{0.0f, 0.0f, 1.0f, 0.0f};
    };
    static_assert(sizeof(RTMaterialGPU) == sizeof(glm::vec4) * 4,
                  "RTMaterialGPU must stay layout-compatible with RaytracingMaterials.hlsli::RTMaterial");

    [[nodiscard]] static RTMaterialGPU BuildRTMaterialGPU(const MaterialModel& material,
                                                          uint32_t surfaceClassId);

    Result<void> AllocateDispatchDescriptorTables();
    Result<void> CreateGlobalRootSignature();
    Result<void> InitializeShadowPipeline();
    Result<void> InitializeReflectionPipeline();
    Result<void> InitializeGIPipeline();
    [[nodiscard]] uint32_t GetDescriptorFrameIndex() const;

    static constexpr uint32_t kRTFrameCount = 3;
    static constexpr uint32_t kShadowDescriptorCount = 3;
    static constexpr uint32_t kReflectionDescriptorCount = 5;
    static constexpr uint32_t kGIDescriptorCount = 3;

    ComPtr<ID3D12Device5> m_device5;
    DescriptorHeapManager* m_descriptors = nullptr;

    uint32_t m_rtxWidth = 0;
    uint32_t m_rtxHeight = 0;

    // Cached BLAS per unique mesh.
    std::unordered_map<const Scene::MeshData*, BLASEntry> m_blasCache;
    // Approximate total GPU memory consumed by all BLAS buffers (result +
    // scratch). Updated as BLAS resources are allocated.
    uint64_t m_totalBLASBytes = 0;
    uint64_t m_maxBLASBytesTotal = 1ull * 1024ull * 1024ull * 1024ull;
    uint64_t m_maxBLASBuildBytesPerFrame = 256ull * 1024ull * 1024ull;

    // Current frame index set by the renderer. Used to track which frame a
    // BLAS build was recorded in so scratch buffers aren't released too early.
    uint64_t m_currentFrameIndex = 0;

    // TLAS and instance data.
    ComPtr<ID3D12Resource> m_instanceBuffer;
    UINT64 m_instanceBufferSize = 0;
    UINT64 m_instanceBufferPendingSize = 0;  // Size to resize to next frame
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> m_instanceDescs;
    ComPtr<ID3D12Resource> m_rtMaterialBuffer;
    UINT64 m_rtMaterialBufferSize = 0;
    std::vector<RTMaterialGPU> m_rtMaterials;
    TLASBuildStats m_lastTLASStats;

    // Cached camera information used for broad distance culling when building
    // the TLAS. Ray tracing visibility is intentionally wider than raster
    // visibility because reflections, shadows, and GI can sample off-screen
    // contributors.
    glm::vec3 m_cameraPosWS{0.0f};
    glm::vec3 m_cameraForwardWS{0.0f, 0.0f, 1.0f};
    float     m_cameraNearPlane = 0.1f;
    float     m_cameraFarPlane  = 1000.0f;
    bool      m_hasCamera = false;

    ComPtr<ID3D12Resource> m_tlas;
    ComPtr<ID3D12Resource> m_tlasScratch;
    UINT64 m_tlasSize = 0;
    UINT64 m_tlasScratchSize = 0;
    UINT64 m_tlasPendingSize = 0;         // Size to resize to next frame
    UINT64 m_tlasScratchPendingSize = 0;  // Size to resize to next frame
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

    // Persistent descriptors used to bind TLAS/depth/output resources for
    // DispatchRays. Tables are split by frame and by RT pass so descriptor
    // rewrites never alias another in-flight pass or frame.
    DescriptorHandle m_shadowDispatchDescriptors[kRTFrameCount][kShadowDescriptorCount]{};
    DescriptorHandle m_reflectionDispatchDescriptors[kRTFrameCount][kReflectionDescriptorCount]{};
    DescriptorHandle m_giDispatchDescriptors[kRTFrameCount][kGIDescriptorCount]{};
    bool m_dispatchDescriptorTablesValid = false;

    // Callback to force GPU sync before destroying buffers. Set by Renderer.
    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
