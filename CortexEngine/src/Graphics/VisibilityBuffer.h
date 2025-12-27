#pragma once

#include "RHI/D3D12Includes.h"
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <functional>

#include "Utils/Result.h"
#include "RHI/DescriptorHeap.h"
#include "ShaderTypes.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;
class DX12CommandQueue;
class DescriptorHeapManager;
class BindlessResourceManager;

// Visibility buffer data layout:
// R32G32_UINT: x = primitiveID, y = instanceID
struct VisBufferPayload {
    uint32_t primitiveID;
    uint32_t instanceID;
};

// Per-instance data for visibility buffer rendering
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padded due to alignment specifier (intentional for HLSL layout)
#endif
struct alignas(16) VBInstanceData {
    glm::mat4 worldMatrix;
    glm::mat4 prevWorldMatrix;
    glm::mat4 normalMatrix;  // World-space normal transform (inverse-transpose)
    uint32_t meshIndex;       // Index into mesh buffer array
    uint32_t materialIndex;   // Index into material buffer
    uint32_t firstIndex;      // Start index in global index buffer
    uint32_t indexCount;      // Number of indices
    uint32_t baseVertex;      // Base vertex offset
    // Explicit padding to align boundingSphere to 16 bytes (HLSL StructuredBuffer rule).
    // baseVertex ends at offset 212. boundingSphere (float4) requires 16-byte alignment (offset 224).
    uint32_t _padAlign[3];
    // Bounding sphere in object space: xyz = center, w = radius.
    glm::vec4 boundingSphere;
    // Previous frame center in world space: xyz = center, w unused.
    glm::vec4 prevCenterWS;
    // Packed stable occlusion-history ID (gen<<16 | slot).
    uint32_t cullingId;
    uint32_t flags;
    float depthBiasNdc;
    uint32_t _pad0;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Minimal material constants for visibility-buffer material resolve (milestone: constant-only materials).
// Keep this small and stable; later we can extend with texture indices and map flags to match ShaderTypes.h.
struct alignas(16) VBMaterialConstants {
    glm::vec4 albedo;   // rgba
    float metallic;
    float roughness;
    float ao;
    float _pad0;
    alignas(16) glm::uvec4 textureIndices; // bindless indices: albedo, normal, metallic, roughness
    alignas(16) glm::uvec4 textureIndices2; // bindless indices: occlusion, emissive, unused, unused
    glm::vec4 emissiveFactorStrength;       // rgb emissive factor, w emissive strength
    glm::vec4 extraParams;                  // x occlusion strength, y normal scale, z/w reserved
    // x = clear-coat weight, y = clear-coat roughness, z = sheen weight, w = SSS wrap
    glm::vec4 coatParams;
    // Transmission + IOR (KHR_materials_transmission / KHR_materials_ior).
    // x = transmission factor (0..1), y = IOR (>= 1), z/w reserved.
    glm::vec4 transmissionParams;
    // Specular extension (KHR_materials_specular).
    // rgb = specular color factor (linear), w = specular factor.
    glm::vec4 specularParams;
    // Bindless texture indices for extensions:
    // textureIndices3: x=transmission, y=clearcoat, z=clearcoatRoughness, w=specular
    // textureIndices4: x=specularColor, y/z/w unused
    alignas(16) glm::uvec4 textureIndices3;
    alignas(16) glm::uvec4 textureIndices4;
    float alphaCutoff;  // For alpha-masked materials (alphaMode == Mask)
    uint32_t alphaMode; // 0=opaque, 1=mask, 2=blend
    uint32_t doubleSided;
    uint32_t _pad1;
};

// Per-mesh lookup table entry for bindless buffer access in compute shaders.
// Indices refer to the global CBV/SRV/UAV descriptor heap (ResourceDescriptorHeap[]).
struct alignas(16) VBMeshTableEntry {
    uint32_t vertexBufferIndex;   // bindless descriptor index for ByteAddressBuffer SRV
    uint32_t indexBufferIndex;    // bindless descriptor index for ByteAddressBuffer SRV
    uint32_t vertexStrideBytes;   // bytes per vertex (interleaved)
    uint32_t indexFormat;         // 0 = R32_UINT, 1 = R16_UINT
};

// Reflection probe volume used by deferred lighting for local IBL selection.
// Env indices refer to the global CBV/SRV/UAV heap (ResourceDescriptorHeap[]).
struct alignas(16) VBReflectionProbe {
    glm::vec4 centerBlend;   // xyz = center (world), w = blend distance
    glm::vec4 extents;       // xyz = half-extents (world), w unused
    glm::uvec4 envIndices;   // x = diffuse env SRV index, y = specular env SRV index
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

    // Update per-material constant data for current frame.
    Result<void> UpdateMaterials(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<VBMaterialConstants>& materials
    );

    // Update per-frame reflection probe data for local IBL selection.
    Result<void> UpdateReflectionProbes(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<VBReflectionProbe>& probes
    );

    // Upload a per-frame local light buffer for clustered deferred shading (excludes the directional sun).
    Result<void> UpdateLocalLights(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<Light>& localLights
    );

    // Mesh draw info for visibility pass
    struct VBMeshDrawInfo {
        ID3D12Resource* vertexBuffer;
        ID3D12Resource* indexBuffer;
        uint32_t vertexCount;   // Number of vertices (for buffer size)
        uint32_t indexCount;
        uint32_t firstIndex;
        uint32_t baseVertex;
        // Contiguous range of instances in the VB instance buffer that reference this mesh.
        uint32_t startInstance;
        uint32_t instanceCount;
        // Double-sided opaque instances (cull none).
        uint32_t startInstanceDoubleSided;
        uint32_t instanceCountDoubleSided;
        // Alpha-masked instances are drawn in a separate alpha-tested visibility pass.
        uint32_t startInstanceAlpha;
        uint32_t instanceCountAlpha;
        // Double-sided alpha-masked instances (cull none + alpha test).
        uint32_t startInstanceAlphaDoubleSided;
        uint32_t instanceCountAlphaDoubleSided;
        // Persistent bindless SRV indices (ResourceDescriptorHeap[]) for per-mesh VB/IB buffers.
        uint32_t vertexBufferIndex;
        uint32_t indexBufferIndex;
        uint32_t vertexStrideBytes;
        uint32_t indexFormat; // 0 = R32_UINT, 1 = R16_UINT
    };

    // Phase 1: Render visibility buffer (triangle IDs)
    Result<void> RenderVisibilityPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
        const glm::mat4& viewProj,
        const std::vector<VBMeshDrawInfo>& meshDraws,
        D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress = 0
    );

    // Phase 2: Material resolve via compute shader
    Result<void> ResolveMaterials(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* depthBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE depthSRV,
        const std::vector<VBMeshDrawInfo>& meshDraws,
        const glm::mat4& viewProj
    );

    // Optional: compute per-pixel motion vectors from the visibility buffer.
    // Writes UV velocity (prevUV - currUV) into the provided velocity UAV resource.
    Result<void> ComputeMotionVectors(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* velocityBuffer,
        const std::vector<VBMeshDrawInfo>& meshDraws,
        D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress
    );

    // Phase 3: Deferred lighting pass (PBR shading from G-buffers)
    struct DeferredLightingParams {
        glm::mat4 invViewProj;
        glm::mat4 viewMatrix;
        alignas(16) glm::mat4 lightViewProjection[6];  // 0..2 cascades, 3..5 local shadowed lights
        glm::vec4 cameraPosition;                      // xyz = camera world pos
        glm::vec4 sunDirection;                        // xyz = direction-to-light (world)
        glm::vec4 sunRadiance;                         // rgb = sun radiance (color * intensity)
        glm::vec4 cascadeSplits;                       // x,y,z = split depths (view space), w = far plane
        glm::vec4 shadowParams;                        // x=bias, y=pcfRadius(texels), z=enabled, w=pcssEnabled
        glm::vec4 envParams;                           // x=diffuse IBL, y=specular IBL, z=enabled, w unused
        glm::vec4 shadowInvSizeAndSpecMaxMip;          // xy = 1/shadowMapDim, z = specular max mip, w unused
        glm::vec4 projectionParams;                    // x=proj11, y=proj22, z=nearZ, w=farZ
        alignas(16) glm::uvec4 screenAndCluster;       // x=width, y=height, z=clusterCountX, w=clusterCountY
        alignas(16) glm::uvec4 clusterParams;          // x=clusterCountZ, y=maxLightsPerCluster, z=localLightCount, w unused
        alignas(16) glm::uvec4 reflectionProbeParams;  // x=probeTableSRVIndex, y=probeCount, z/w unused
    };

    Result<void> ApplyDeferredLighting(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* hdrTarget,
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
        ID3D12Resource* depthBuffer,
        const DescriptorHandle& depthSRV,
        const DescriptorHandle& envDiffuseSRV,
        const DescriptorHandle& envSpecularSRV,
        const DescriptorHandle& shadowMapSRV,
        const DeferredLightingParams& params
    );

    // Optional: Build per-cluster light lists for the current frame (clustered deferred).
    Result<void> BuildClusteredLightLists(ID3D12GraphicsCommandList* cmdList, const DeferredLightingParams& params);

    // Get output G-buffer textures
    [[nodiscard]] ID3D12Resource* GetAlbedoBuffer() const { return m_gbufferAlbedo.Get(); }
    [[nodiscard]] ID3D12Resource* GetNormalRoughnessBuffer() const { return m_gbufferNormalRoughness.Get(); }
    [[nodiscard]] ID3D12Resource* GetEmissiveMetallicBuffer() const { return m_gbufferEmissiveMetallic.Get(); }
    [[nodiscard]] ID3D12Resource* GetMaterialExt0Buffer() const { return m_gbufferMaterialExt0.Get(); }
    [[nodiscard]] ID3D12Resource* GetMaterialExt1Buffer() const { return m_gbufferMaterialExt1.Get(); }

    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetNormalRoughnessSRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetEmissiveMetallicSRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialExt0SRV() const;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetMaterialExt1SRV() const;

    [[nodiscard]] const DescriptorHandle& GetAlbedoSRVHandle() const { return m_albedoSRV; }
    [[nodiscard]] const DescriptorHandle& GetNormalRoughnessSRVHandle() const { return m_normalRoughnessSRV; }
    [[nodiscard]] const DescriptorHandle& GetEmissiveMetallicSRVHandle() const { return m_emissiveMetallicSRV; }
    [[nodiscard]] const DescriptorHandle& GetMaterialExt0SRVHandle() const { return m_materialExt0SRV; }
    [[nodiscard]] const DescriptorHandle& GetMaterialExt1SRVHandle() const { return m_materialExt1SRV; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetNormalRoughnessRTV() const { return m_normalRoughnessRTV.cpu; }
    [[nodiscard]] uint32_t GetReflectionProbeTableIndex() const { return m_reflectionProbeSRV.index; }
    [[nodiscard]] uint32_t GetLocalLightsTableIndex() const { return m_localLightsSRV.index; }
    [[nodiscard]] uint32_t GetClusterRangesTableIndex() const { return m_clusterRangesSRV.index; }
    [[nodiscard]] uint32_t GetClusterLightIndicesTableIndex() const { return m_clusterLightIndicesSRV.index; }
    [[nodiscard]] uint32_t GetClusterCountX() const { return m_clusterCountX; }
    [[nodiscard]] uint32_t GetClusterCountY() const { return m_clusterCountY; }
    [[nodiscard]] uint32_t GetClusterCountZ() const { return m_clusterCountZ; }
    [[nodiscard]] uint32_t GetMaxLightsPerCluster() const { return m_maxLightsPerCluster; }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetDummyCullMaskAddress() const {
        return m_dummyCullMaskBuffer ? m_dummyCullMaskBuffer->GetGPUVirtualAddress() : 0;
    }

    // Get visibility buffer for debug visualization
    [[nodiscard]] ID3D12Resource* GetVisibilityBuffer() const { return m_visibilityBuffer.Get(); }
    [[nodiscard]] const DescriptorHandle& GetVisibilitySRVHandle() const { return m_visibilitySRV; }

    enum class DebugBlitBuffer : uint32_t {
        Albedo = 0,
        NormalRoughness,
        EmissiveMetallic,
        MaterialExt0,
        MaterialExt1,
    };

    // Debug: Blit albedo to HDR buffer for visualization
    Result<void> DebugBlitAlbedoToHDR(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* hdrTarget,
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV
    );

    // Debug: Blit a selected VB G-buffer to HDR.
    Result<void> DebugBlitGBufferToHDR(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* hdrTarget,
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
        DebugBlitBuffer buffer
    );

    // Debug: Visualize the visibility buffer payload (triangleID/instanceID).
    Result<void> DebugBlitVisibilityToHDR(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* hdrTarget,
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV
    );

    // Debug: Visualize an external depth buffer (R32_FLOAT SRV expected).
    Result<void> DebugBlitDepthToHDR(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* hdrTarget,
        D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
        ID3D12Resource* depthBuffer
    );

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
    Result<void> EnsureBRDFLUT(ID3D12GraphicsCommandList* cmdList);

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
    DescriptorHandle m_visibilityUAVStaging;

    // G-buffer outputs from material resolve
    // Albedo is stored as R8G8B8A8_TYPELESS with UNORM SRV/UAV views (linear) for UAV legality.
    ComPtr<ID3D12Resource> m_gbufferAlbedo;
    ComPtr<ID3D12Resource> m_gbufferNormalRoughness; // RGBA16_FLOAT
    ComPtr<ID3D12Resource> m_gbufferEmissiveMetallic; // RGBA16_FLOAT
    ComPtr<ID3D12Resource> m_gbufferMaterialExt0; // RGBA16_FLOAT
    ComPtr<ID3D12Resource> m_gbufferMaterialExt1; // RGBA16_FLOAT

    DescriptorHandle m_albedoRTV, m_albedoSRV, m_albedoUAV;
    DescriptorHandle m_normalRoughnessRTV, m_normalRoughnessSRV, m_normalRoughnessUAV;
    DescriptorHandle m_emissiveMetallicRTV, m_emissiveMetallicSRV, m_emissiveMetallicUAV;
    DescriptorHandle m_materialExt0RTV, m_materialExt0SRV, m_materialExt0UAV;
    DescriptorHandle m_materialExt1RTV, m_materialExt1SRV, m_materialExt1UAV;

    // Instance data buffer
    ComPtr<ID3D12Resource> m_instanceBuffer;
    DescriptorHandle m_instanceSRV;
    uint8_t* m_instanceBufferMapped = nullptr;  // Persistent mapping
    uint32_t m_instanceCount = 0;
    uint32_t m_maxInstances = 65536;

    // Material constants buffer (upload heap, persistently mapped)
    ComPtr<ID3D12Resource> m_materialBuffer;
    uint8_t* m_materialBufferMapped = nullptr;
    uint32_t m_materialCount = 0;
    uint32_t m_maxMaterials = 4096;

    // Reflection probe table (upload heap, persistently mapped)
    ComPtr<ID3D12Resource> m_reflectionProbeBuffer;
    uint8_t* m_reflectionProbeBufferMapped = nullptr;
    uint32_t m_reflectionProbeCount = 0;
    uint32_t m_maxReflectionProbes = 256;
    DescriptorHandle m_reflectionProbeSRV;

    // Small 4-byte buffer used when no GPU cull mask is available. Shaders
    // guard against out-of-range reads so a single uint32 is sufficient.
    ComPtr<ID3D12Resource> m_dummyCullMaskBuffer;

    // Mesh table buffer (upload heap, persistently mapped)
    ComPtr<ID3D12Resource> m_meshTableBuffer;
    uint8_t* m_meshTableBufferMapped = nullptr;
    uint32_t m_meshCount = 0;
    uint32_t m_maxMeshes = 4096;

    // Local light buffer (upload heap, persistently mapped)
    ComPtr<ID3D12Resource> m_localLightsBuffer;
    uint8_t* m_localLightsBufferMapped = nullptr;
    uint32_t m_localLightCount = 0;
    uint32_t m_maxLocalLights = 2048;
    DescriptorHandle m_localLightsSRV;

    // Pipelines
    ComPtr<ID3D12RootSignature> m_visibilityRootSignature;
    ComPtr<ID3D12PipelineState> m_visibilityPipeline;
    ComPtr<ID3D12PipelineState> m_visibilityPipelineDoubleSided;
    ComPtr<ID3D12RootSignature> m_visibilityAlphaRootSignature;
    ComPtr<ID3D12PipelineState> m_visibilityAlphaPipeline;
    ComPtr<ID3D12PipelineState> m_visibilityAlphaPipelineDoubleSided;

    ComPtr<ID3D12RootSignature> m_resolveRootSignature;
    ComPtr<ID3D12PipelineState> m_resolvePipeline;

    ComPtr<ID3D12RootSignature> m_motionVectorsRootSignature;
    ComPtr<ID3D12PipelineState> m_motionVectorsPipeline;

    // Clustered light culling pipeline (compute)
    ComPtr<ID3D12RootSignature> m_clusterRootSignature;
    ComPtr<ID3D12PipelineState> m_clusterPipeline;

    // Clustered light list buffers (default heap)
    ComPtr<ID3D12Resource> m_clusterRangesBuffer;      // RWStructuredBuffer<uint2>
    ComPtr<ID3D12Resource> m_clusterLightIndicesBuffer; // RWStructuredBuffer<uint>
    DescriptorHandle m_clusterRangesSRV;
    DescriptorHandle m_clusterLightIndicesSRV;

    uint32_t m_clusterCountX = 16;
    uint32_t m_clusterCountY = 9;
    uint32_t m_clusterCountZ = 24;
    uint32_t m_maxLightsPerCluster = 128;
    uint32_t m_clusterCount = 0;

    // Debug blit pipeline
    ComPtr<ID3D12RootSignature> m_blitRootSignature;
    ComPtr<ID3D12PipelineState> m_blitPipeline;
    ComPtr<ID3D12PipelineState> m_blitVisibilityPipeline;
    ComPtr<ID3D12PipelineState> m_blitDepthPipeline;
    ComPtr<ID3D12DescriptorHeap> m_blitSamplerHeap;

    // Deferred lighting pipeline
    ComPtr<ID3D12RootSignature> m_deferredLightingRootSignature;
    ComPtr<ID3D12PipelineState> m_deferredLightingPipeline;
    ComPtr<ID3D12DescriptorHeap> m_deferredLightingSamplerHeap;
    ComPtr<ID3D12Resource> m_deferredLightingCB;  // Persistent constant buffer for lighting params

    // BRDF LUT (split-sum IBL)
    ComPtr<ID3D12Resource> m_brdfLut;
    DescriptorHandle m_brdfLutSRV;
    DescriptorHandle m_brdfLutUAV;
    ComPtr<ID3D12RootSignature> m_brdfLutRootSignature;
    ComPtr<ID3D12PipelineState> m_brdfLutPipeline;
    bool m_brdfLutReady = false;

    // Resource states
    D3D12_RESOURCE_STATES m_visibilityState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_albedoState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_normalRoughnessState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_emissiveMetallicState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_materialExt0State = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_materialExt1State = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_brdfLutState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_clusterRangesState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES m_clusterLightIndicesState = D3D12_RESOURCE_STATE_COMMON;

    FlushCallback m_flushCallback;
};

} // namespace Cortex::Graphics
