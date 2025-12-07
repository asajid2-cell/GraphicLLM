#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
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
class BindlessResourceManager;

// Visibility buffer data layout:
// R32G32_UINT: x = triangleID (24 bits) + drawID (8 bits), y = instanceID
struct VisBufferPayload {
    uint32_t triangleAndDrawID;  // tri[23:0] | draw[31:24]
    uint32_t instanceID;
};

// Per-instance data for visibility buffer rendering
struct alignas(16) VBInstanceData {
    glm::mat4 worldMatrix;
    uint32_t meshIndex;       // Index into mesh buffer array
    uint32_t materialIndex;   // Index into material buffer
    uint32_t firstIndex;      // Start index in global index buffer
    uint32_t indexCount;      // Number of indices
    uint32_t baseVertex;      // Base vertex offset
    uint32_t _pad[3];
};

// Material resolve output (written to G-buffer by compute shader)
struct VBMaterialOutput {
    glm::vec4 albedo;           // RGB + alpha
    glm::vec4 normalRoughness;  // xyz = normal, w = roughness
    glm::vec4 emissiveMetal;    // rgb = emissive, a = metallic
};

// Visibility Buffer Renderer
// Two-phase deferred rendering:
// 1. Visibility Pass: Rasterize triangle IDs to visibility buffer (R32G32_UINT)
// 2. Material Resolve: Compute shader fetches vertices, interpolates, evaluates materials
//
// Benefits:
// - Decouples geometry complexity from shading rate
// - Single draw call for entire scene (with GPU culling)
// - Enables per-pixel LOD and material evaluation
//
class VisibilityBufferRenderer {
public:
    VisibilityBufferRenderer() = default;
    ~VisibilityBufferRenderer() = default;

    VisibilityBufferRenderer(const VisibilityBufferRenderer&) = delete;
    VisibilityBufferRenderer& operator=(const VisibilityBufferRenderer&) = delete;

    // Initialize visibility buffer system
    Result<void> Initialize(
        DX12Device* device,
        DescriptorHeapManager* descriptorManager,
        BindlessResourceManager* bindlessManager,
        uint32_t width,
        uint32_t height
    );

    // Shutdown and release resources
    void Shutdown();

    // Resize visibility buffer (call on window resize)
    Result<void> Resize(uint32_t width, uint32_t height);

    // Update instance data for current frame
    Result<void> UpdateInstances(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<VBInstanceData>& instances
    );

    // Phase 1: Render visibility buffer (triangle IDs)
    Result<void> RenderVisibilityPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
        const glm::mat4& viewProj
    );

    // Phase 2: Material resolve via compute shader
    Result<void> ResolveMaterials(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSRV
    );

    // Get output G-buffer textures
    [[nodiscard]] ID3D12Resource* GetAlbedoBuffer() const { return m_gbufferAlbedo.Get(); }
    [[nodiscard]] ID3D12Resource* GetNormalRoughnessBuffer() const { return m_gbufferNormalRoughness.Get(); }
    [[nodiscard]] ID3D12Resource* GetEmissiveMetallicBuffer() const { return m_gbufferEmissiveMetallic.Get(); }

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetNormalRoughnessSRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetEmissiveMetallicSRV() const;

    // Get visibility buffer for debug visualization
    [[nodiscard]] ID3D12Resource* GetVisibilityBuffer() const { return m_visibilityBuffer.Get(); }

    // Statistics
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] uint32_t GetInstanceCount() const { return m_instanceCount; }

    // Flush callback for safe resizes
    using FlushCallback = std::function<void()>;
    void SetFlushCallback(FlushCallback callback) { m_flushCallback = std::move(callback); }

private:
    Result<void> CreateVisibilityBuffer();
    Result<void> CreateGBuffers();
    Result<void> CreatePipelines();
    Result<void> CreateRootSignatures();

    DX12Device* m_device = nullptr;
    DescriptorHeapManager* m_descriptorManager = nullptr;
    BindlessResourceManager* m_bindlessManager = nullptr;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Visibility buffer (R32G32_UINT)
    ComPtr<ID3D12Resource> m_visibilityBuffer;
    DescriptorHandle m_visibilityRTV;
    DescriptorHandle m_visibilitySRV;
    DescriptorHandle m_visibilityUAV;

    // G-buffer outputs from material resolve
    ComPtr<ID3D12Resource> m_gbufferAlbedo;          // RGBA8_UNORM_SRGB
    ComPtr<ID3D12Resource> m_gbufferNormalRoughness; // RGBA16_FLOAT
    ComPtr<ID3D12Resource> m_gbufferEmissiveMetallic; // RGBA16_FLOAT

    DescriptorHandle m_albedoRTV, m_albedoSRV, m_albedoUAV;
    DescriptorHandle m_normalRoughnessRTV, m_normalRoughnessSRV, m_normalRoughnessUAV;
    DescriptorHandle m_emissiveMetallicRTV, m_emissiveMetallicSRV, m_emissiveMetallicUAV;

    // Instance data buffer
    ComPtr<ID3D12Resource> m_instanceBuffer;
    DescriptorHandle m_instanceSRV;
    uint32_t m_instanceCount = 0;
    uint32_t m_maxInstances = 65536;

    // Pipelines
    ComPtr<ID3D12RootSignature> m_visibilityRootSignature;
    ComPtr<ID3D12PipelineState> m_visibilityPipeline;

    ComPtr<ID3D12RootSignature> m_resolveRootSignature;
    ComPtr<ID3D12PipelineState> m_resolvePipeline;

    // Resource states
    D3D12_RESOURCE_STATES m_visibilityState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_albedoState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_normalRoughnessState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_emissiveMetallicState = D3D12_RESOURCE_STATE_COMMON;

    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
