#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

#include "Utils/Result.h"
#include "RHI/DescriptorHeap.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;
class DX12CommandQueue;
class DescriptorHeapManager;

// Instance data for GPU culling (matches shader struct)
struct alignas(16) GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 boundingSphere;  // xyz = center (object space), w = radius
    uint32_t meshIndex;
    uint32_t materialIndex;
    uint32_t flags;            // visibility flags, etc.
    uint32_t _pad;
};

// Draw argument for ExecuteIndirect (matches D3D12_DRAW_INDEXED_ARGUMENTS)
struct DrawIndexedArguments {
    uint32_t indexCountPerInstance;
    uint32_t instanceCount;
    uint32_t startIndexLocation;
    int32_t  baseVertexLocation;
    uint32_t startInstanceLocation;
};

// Per-mesh info for indirect draws
struct MeshInfo {
    uint32_t indexCount;
    uint32_t startIndex;
    int32_t  baseVertex;
    uint32_t materialIndex;
};

// Frustum planes for culling
struct FrustumPlanes {
    glm::vec4 planes[6];  // left, right, bottom, top, near, far
};

// GPU Culling Pipeline
// Performs frustum culling on the GPU using compute shaders.
// Outputs a compacted list of visible instances and indirect draw arguments.
class GPUCullingPipeline {
public:
    GPUCullingPipeline() = default;
    ~GPUCullingPipeline() = default;

    GPUCullingPipeline(const GPUCullingPipeline&) = delete;
    GPUCullingPipeline& operator=(const GPUCullingPipeline&) = delete;

    // Initialize the culling pipeline (shaders, buffers, root signature)
    Result<void> Initialize(
        DX12Device* device,
        DescriptorHeapManager* descriptorManager,
        DX12CommandQueue* commandQueue,
        uint32_t maxInstances = 65536
    );

    // Shutdown and release resources
    void Shutdown();

    // Upload instance data for the current frame
    Result<void> UpdateInstances(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<GPUInstanceData>& instances
    );

    // Execute GPU culling compute shader
    Result<void> DispatchCulling(
        ID3D12GraphicsCommandList* cmdList,
        const glm::mat4& viewProj,
        const glm::vec3& cameraPos
    );

    // Get the visible instance buffer for rendering
    [[nodiscard]] ID3D12Resource* GetVisibleInstanceBuffer() const { return m_visibleInstanceBuffer.Get(); }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetVisibleInstanceBufferGPU() const {
        return m_visibleInstanceBuffer ? m_visibleInstanceBuffer->GetGPUVirtualAddress() : 0;
    }

    // Get the indirect argument buffer for ExecuteIndirect
    [[nodiscard]] ID3D12Resource* GetIndirectArgBuffer() const { return m_indirectArgBuffer.Get(); }

    // Get the command signature for ExecuteIndirect
    [[nodiscard]] ID3D12CommandSignature* GetCommandSignature() const { return m_commandSignature.Get(); }

    // Get visible count (read back from GPU counter buffer)
    [[nodiscard]] uint32_t GetVisibleCount() const { return m_visibleCount; }

    // Statistics
    [[nodiscard]] uint32_t GetTotalInstances() const { return m_totalInstances; }
    [[nodiscard]] uint32_t GetMaxInstances() const { return m_maxInstances; }

    // Set flush callback for safe buffer resizes
    using FlushCallback = std::function<void()>;
    void SetFlushCallback(FlushCallback callback) { m_flushCallback = std::move(callback); }

private:
    Result<void> CreateRootSignature();
    Result<void> CreateComputePipeline();
    Result<void> CreateBuffers();
    Result<void> CreateCommandSignature();

    void ExtractFrustumPlanes(const glm::mat4& viewProj, FrustumPlanes& planes);

    DX12Device* m_device = nullptr;
    DescriptorHeapManager* m_descriptorManager = nullptr;
    DX12CommandQueue* m_commandQueue = nullptr;

    // Compute pipeline
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_cullPipeline;

    // Command signature for ExecuteIndirect
    ComPtr<ID3D12CommandSignature> m_commandSignature;

    // Buffers
    ComPtr<ID3D12Resource> m_instanceBuffer;           // All instances (upload)
    ComPtr<ID3D12Resource> m_visibleInstanceBuffer;    // Compacted visible instances (UAV)
    ComPtr<ID3D12Resource> m_indirectArgBuffer;        // Draw arguments for ExecuteIndirect (UAV)
    ComPtr<ID3D12Resource> m_counterBuffer;            // Atomic counter for visible instances (UAV)
    ComPtr<ID3D12Resource> m_counterReadback;          // CPU-readable counter

    // Descriptors (shader-visible for ClearUnorderedAccessViewUint)
    DescriptorHandle m_instanceSRV;
    DescriptorHandle m_visibleInstanceUAV;
    DescriptorHandle m_indirectArgUAV;
    DescriptorHandle m_counterUAV;           // GPU descriptor for counter buffer
    DescriptorHandle m_counterUAVStaging;    // CPU-only descriptor for ClearUAV

    // Constants
    ComPtr<ID3D12Resource> m_cullConstantBuffer;

    uint32_t m_maxInstances = 65536;
    uint32_t m_totalInstances = 0;
    uint32_t m_visibleCount = 0;

    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
