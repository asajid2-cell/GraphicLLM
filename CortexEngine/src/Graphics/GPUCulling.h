#pragma once

#include "RHI/D3D12Includes.h"
#include <wrl/client.h>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

#include "Utils/Result.h"
#include "RHI/DescriptorHeap.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

// Triple-buffering frame count (must match Renderer.h kFrameCount)
static constexpr uint32_t kGPUCullingFrameCount = 3;

class DX12Device;
class DX12CommandQueue;
class DescriptorHeapManager;

// Instance data for GPU culling (matches shader struct)
struct alignas(16) GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 boundingSphere;  // xyz = center (object space), w = radius
    glm::vec4 prevCenterWS;     // xyz = previous frame center (world space)
    uint32_t meshIndex;
    uint32_t materialIndex;
    uint32_t flags;            // visibility flags, etc.
    // Packed stable ID for occlusion history indexing:
    //   bits[15:0]  = slot (<= 65535)
    //   bits[31:16] = generation (increments when a slot is recycled)
    uint32_t cullingId;
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
    struct DebugStats {
        bool enabled = false;
        bool valid = false;
        uint32_t tested = 0;
        uint32_t frustumCulled = 0;
        uint32_t occluded = 0;
        uint32_t visible = 0;
        uint32_t sampleMip = 0;
        float sampleNearDepth = 0.0f;
        float sampleHzbDepth = 0.0f;
        uint32_t sampleFlags = 0;
    };

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

    // Set current frame index for triple-buffered resources (call at start of frame)
    void SetFrameIndex(uint32_t frameIndex) { m_frameIndex = frameIndex % kGPUCullingFrameCount; }

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

    // Optional HZB occlusion inputs (built from the main depth buffer).
    // When enabled, the compute shader uses the depth pyramid to reject
    // instances hidden behind near-depth occluders, with a small hysteresis
    // history to reduce popping.
    void SetHZBForOcclusion(
        ID3D12Resource* hzbTexture,
        uint32_t hzbWidth,
        uint32_t hzbHeight,
        uint32_t hzbMipCount,
        const glm::mat4& hzbViewMatrix,
        const glm::mat4& hzbViewProjMatrix,
        const glm::vec3& hzbCameraPosWS,
        float cameraNearPlane,
        float cameraFarPlane,
        bool enabled);

    // Get the visible command buffer for ExecuteIndirect (triple-buffered)
    // CRITICAL FIX: Read from PREVIOUS frame's buffer to prevent GPU-GPU race condition.
    // The compute shader writes to m_frameIndex, graphics pipeline reads from (m_frameIndex+2)%3.
    // This implements the standard triple-buffered producer-consumer pattern.
    [[nodiscard]] ID3D12Resource* GetVisibleCommandBuffer() const {
        uint32_t readIndex = (m_frameIndex + 2) % kGPUCullingFrameCount;
        return m_visibleCommandBuffer[readIndex].Get();
    }
    [[nodiscard]] ID3D12Resource* GetCommandCountBuffer() const {
        uint32_t readIndex = (m_frameIndex + 2) % kGPUCullingFrameCount;
        return m_commandCountBuffer[readIndex].Get();
    }
    [[nodiscard]] ID3D12Resource* GetAllCommandBuffer() const {
        uint32_t readIndex = (m_frameIndex + 2) % kGPUCullingFrameCount;
        return m_allCommandBuffer[readIndex].Get();
    }
    [[nodiscard]] ID3D12Resource* GetVisibilityMaskBuffer() const {
        uint32_t readIndex = (m_frameIndex + 2) % kGPUCullingFrameCount;
        return m_visibilityMaskBuffer[readIndex].Get();
    }

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
    void SetDebugEnabled(bool enabled) { m_debugEnabled = enabled; }
    [[nodiscard]] DebugStats GetDebugStats() const { return m_debugStats; }
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

    // Buffers - triple-buffered to prevent CPU/GPU race conditions
    ComPtr<ID3D12Resource> m_instanceBuffer[kGPUCullingFrameCount];           // All instances (default heap)
    ComPtr<ID3D12Resource> m_instanceUploadBuffer[kGPUCullingFrameCount];     // Upload staging for instances
    ComPtr<ID3D12Resource> m_allCommandBuffer[kGPUCullingFrameCount];         // All indirect commands (default heap)
    ComPtr<ID3D12Resource> m_allCommandUploadBuffer[kGPUCullingFrameCount];   // Upload staging for commands
    ComPtr<ID3D12Resource> m_visibleCommandBuffer[kGPUCullingFrameCount];  // Compacted visible commands (UAV) - triple-buffered
    ComPtr<ID3D12Resource> m_commandCountBuffer[kGPUCullingFrameCount];    // Atomic counter for visible commands (UAV) - triple-buffered
    ComPtr<ID3D12Resource> m_commandCountReadback;     // CPU-readable counter
    ComPtr<ID3D12Resource> m_visibleCommandReadback;   // CPU-readable command snapshot
    ComPtr<ID3D12Resource> m_debugBuffer;              // Debug counters/sample (UAV)
    ComPtr<ID3D12Resource> m_debugReadback;            // CPU-readable debug snapshot
    // Triple-buffered visibility mask to prevent race conditions between GPU culling write and visibility pass read
    ComPtr<ID3D12Resource> m_visibilityMaskBuffer[kGPUCullingFrameCount];     // Per-instance visibility mask bits (UAV/SRV)

    // Descriptors (shader-visible for ClearUnorderedAccessViewUint) - triple-buffered
    DescriptorHandle m_counterUAV[kGPUCullingFrameCount];           // GPU descriptor for counter buffer
    DescriptorHandle m_counterUAVStaging[kGPUCullingFrameCount];    // CPU-only descriptor for ClearUAV
    DescriptorHandle m_historyAUAV;
    DescriptorHandle m_historyAUAVStaging;
    DescriptorHandle m_historyBUAV;
    DescriptorHandle m_historyBUAVStaging;
    // HZB SRV binding:
    // - m_hzbSrv: shader-visible fallback SRV that always points to the dummy HZB (never rewritten while GPU is in-flight)
    // - m_hzbSrvStaging: CPU-only SRV updated to either the real HZB or dummy; copied into a per-frame transient slot at dispatch time
    DescriptorHandle m_hzbSrv;
    DescriptorHandle m_hzbSrvStaging;
    DescriptorHandle m_debugUAV;
    DescriptorHandle m_debugUAVStaging;

    // Constants
    ComPtr<ID3D12Resource> m_cullConstantBuffer;

    uint32_t m_maxInstances = 65536;
    uint32_t m_totalInstances = 0;
    uint32_t m_visibleCount = 0;
    D3D12_RESOURCE_STATES m_visibleCommandState[kGPUCullingFrameCount] = {D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES m_commandCountState[kGPUCullingFrameCount] = {D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES m_instanceState[kGPUCullingFrameCount] = {D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST};
    D3D12_RESOURCE_STATES m_allCommandState[kGPUCullingFrameCount] = {D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST};
    D3D12_RESOURCE_STATES m_historyAState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_historyBState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_debugState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_visibilityMaskState[kGPUCullingFrameCount] = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};

    FlushCallback m_flushCallback;

    // Frame index for triple-buffered resources
    uint32_t m_frameIndex = 0;

    bool m_forceVisible = false;
    bool m_debugEnabled = false;
    bool m_debugReadbackPending = false;
    DebugStats m_debugStats{};

    // Occlusion culling inputs (owned by renderer; pointer is valid for the duration
    // of the dispatch). SRV for sampling is created in the descriptor manager heap.
    ID3D12Resource* m_hzbTexture = nullptr;
    uint32_t m_hzbWidth = 0;
    uint32_t m_hzbHeight = 0;
    uint32_t m_hzbMipCount = 0;
    glm::mat4 m_hzbViewMatrix{1.0f};
    glm::mat4 m_hzbViewProjMatrix{1.0f};
    glm::vec3 m_hzbCameraPosWS{0.0f};
    float m_hzbNearPlane = 0.1f;
    float m_hzbFarPlane = 1000.0f;
    bool m_hzbEnabled = false;

    // Per-instance occlusion hysteresis history (ping-pong).
    ComPtr<ID3D12Resource> m_occlusionHistoryA;
    ComPtr<ID3D12Resource> m_occlusionHistoryB;
    ComPtr<ID3D12Resource> m_dummyHzbTexture;
    bool m_historyPingPong = false;
    bool m_historyInitialized = false;

    bool m_commandReadbackRequested = false;
    bool m_commandReadbackPending = false;
    uint32_t m_commandReadbackCount = 0;
};

static_assert(sizeof(IndirectCommand) == 72, "IndirectCommand must be 72 bytes");

} // namespace Cortex::Graphics
