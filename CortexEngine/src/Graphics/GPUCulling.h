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

// Indirect command record for GPU-driven draws (root params 0,2 + IA + draw)
struct IndirectCommand {
    D3D12_GPU_VIRTUAL_ADDRESS objectCBV;
    D3D12_GPU_VIRTUAL_ADDRESS materialCBV;
    D3D12_VERTEX_BUFFER_VIEW vertexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBuffer;
    DrawIndexedArguments draw;
    uint32_t padding = 0;
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

    // Upload per-instance indirect commands for the current frame
    Result<void> UpdateIndirectCommands(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<IndirectCommand>& commands
    );

    // Execute GPU culling compute shader
    Result<void> DispatchCulling(
        ID3D12GraphicsCommandList* cmdList,
        const glm::mat4& viewProj,
        const glm::vec3& cameraPos
    );

    // Get the visible command buffer for ExecuteIndirect
    [[nodiscard]] ID3D12Resource* GetVisibleCommandBuffer() const { return m_visibleCommandBuffer.Get(); }
    [[nodiscard]] ID3D12Resource* GetCommandCountBuffer() const { return m_commandCountBuffer.Get(); }
    [[nodiscard]] ID3D12Resource* GetAllCommandBuffer() const { return m_allCommandBuffer.Get(); }

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

    // Configure the graphics root signature used for indirect commands
    Result<void> SetGraphicsRootSignature(ID3D12RootSignature* rootSignature);

    // Transition the full command buffer for ExecuteIndirect (no compaction).
    Result<void> PrepareAllCommandsForExecuteIndirect(ID3D12GraphicsCommandList* cmdList);

    // Debug controls
    void SetForceVisible(bool forceVisible) { m_forceVisible = forceVisible; }
    void RequestCommandReadback(uint32_t commandCount);

    // Update visible count from the readback buffer (call after GPU fence)
    void UpdateVisibleCountFromReadback();

private:
    Result<void> CreateRootSignature();
    Result<void> CreateComputePipeline();
    Result<void> CreateBuffers();
    Result<void> CreateCommandSignature(ID3D12RootSignature* rootSignature);

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
    ComPtr<ID3D12Resource> m_instanceBuffer;           // All instances (default heap)
    ComPtr<ID3D12Resource> m_instanceUploadBuffer;     // Upload staging for instances
    ComPtr<ID3D12Resource> m_allCommandBuffer;         // All indirect commands (default heap)
    ComPtr<ID3D12Resource> m_allCommandUploadBuffer;   // Upload staging for commands
    ComPtr<ID3D12Resource> m_visibleCommandBuffer;     // Compacted visible commands (UAV)
    ComPtr<ID3D12Resource> m_commandCountBuffer;       // Atomic counter for visible commands (UAV)
    ComPtr<ID3D12Resource> m_commandCountReadback;     // CPU-readable counter
    ComPtr<ID3D12Resource> m_visibleCommandReadback;   // CPU-readable command snapshot

    // Descriptors (shader-visible for ClearUnorderedAccessViewUint)
    DescriptorHandle m_counterUAV;           // GPU descriptor for counter buffer
    DescriptorHandle m_counterUAVStaging;    // CPU-only descriptor for ClearUAV

    // Constants
    ComPtr<ID3D12Resource> m_cullConstantBuffer;

    uint32_t m_maxInstances = 65536;
    uint32_t m_totalInstances = 0;
    uint32_t m_visibleCount = 0;
    D3D12_RESOURCE_STATES m_visibleCommandState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_commandCountState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES m_instanceState = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12_RESOURCE_STATES m_allCommandState = D3D12_RESOURCE_STATE_COPY_DEST;

    FlushCallback m_flushCallback;

    bool m_forceVisible = false;
    bool m_commandReadbackRequested = false;
    bool m_commandReadbackPending = false;
    uint32_t m_commandReadbackCount = 0;
};

static_assert(sizeof(IndirectCommand) == 72, "IndirectCommand must be 72 bytes");

} // namespace Cortex::Graphics
