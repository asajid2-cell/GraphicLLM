#pragma once

#include <memory>
#include <array>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include "Core/Window.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif
#include "ShaderTypes.h"
#include "Utils/Result.h"
#include "../Scene/Components.h"

namespace Cortex {
    class Window;
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

struct MeshBuffers {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
};

// Constant buffer wrapper
template<typename T>
struct ConstantBuffer {
    ComPtr<ID3D12Resource> buffer;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    uint8_t* mappedBytes = nullptr;
    size_t bufferSize = 0;
    size_t alignedSize = 0;
    size_t offset = 0;

    static constexpr size_t Align256(size_t value) {
        return (value + 255) & ~static_cast<size_t>(255);
    }

    Result<void> Initialize(ID3D12Device* device, size_t elementCount = 1) {
        // Create upload heap buffer sized for the requested element count
        alignedSize = Align256(sizeof(T));
        bufferSize = Align256(alignedSize * elementCount);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)
        );

        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create constant buffer");
        }

        gpuAddress = buffer->GetGPUVirtualAddress();

        // Map persistently (upload heap allows this)
        D3D12_RANGE readRange = { 0, 0 };
        hr = buffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedBytes));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to map constant buffer");
        }

        offset = 0;
        return Result<void>::Ok();
    }

    void ResetOffset() { offset = 0; }

    // Write data into the next slice of the buffer and return the GPU address
    D3D12_GPU_VIRTUAL_ADDRESS AllocateAndWrite(const T& data) {
        if (!mappedBytes || alignedSize == 0) {
            return gpuAddress;
        }
        if (offset + alignedSize > bufferSize) {
            offset = 0; // wrap for simplicity; safe because we fence per frame
        }
        memcpy(mappedBytes + offset, &data, sizeof(T));
        D3D12_GPU_VIRTUAL_ADDRESS addr = gpuAddress + offset;
        offset += alignedSize;
        return addr;
    }

    // Convenience for single-slot buffers (frame constants)
    void UpdateData(const T& data) {
        if (mappedBytes) {
            memcpy(mappedBytes, &data, sizeof(T));
        }
    }

    ~ConstantBuffer() {
        if (buffer && mappedBytes) {
            buffer->Unmap(0, nullptr);
            mappedBytes = nullptr;
        }
    }
};

// Main renderer class
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize renderer
    Result<void> Initialize(DX12Device* device, Window* window);
    void Shutdown();

    // Main render function
    void Render(Scene::ECS_Registry* registry, float deltaTime);

    // Upload mesh to GPU
    Result<void> UploadMesh(std::shared_ptr<Scene::MeshData> mesh);

    // Get default placeholder texture
    std::shared_ptr<DX12Texture> GetPlaceholderTexture() const { return m_placeholderAlbedo; }
    std::shared_ptr<DX12Texture> GetPlaceholderNormal() const { return m_placeholderNormal; }
    std::shared_ptr<DX12Texture> GetPlaceholderMetallic() const { return m_placeholderMetallic; }
    std::shared_ptr<DX12Texture> GetPlaceholderRoughness() const { return m_placeholderRoughness; }

    // Load texture from disk (sRGB aware)
    Result<std::shared_ptr<DX12Texture>> LoadTextureFromFile(const std::string& path, bool useSRGB);

    // Debug/inspection controls
    void ToggleShadows();
    void CycleDebugViewMode();
    void AdjustShadowBias(float delta);
    void AdjustShadowPCFRadius(float delta);
    void AdjustCascadeSplitLambda(float delta);
    void AdjustCascadeResolutionScale(uint32_t cascadeIndex, float delta);

    // Introspection for LLM/diagnostics
    [[nodiscard]] float GetExposure() const { return m_exposure; }
    [[nodiscard]] bool GetShadowsEnabled() const { return m_shadowsEnabled; }
    [[nodiscard]] int GetDebugViewMode() const { return static_cast<int>(m_debugViewMode); }
    [[nodiscard]] float GetShadowBias() const { return m_shadowBias; }
    [[nodiscard]] float GetShadowPCFRadius() const { return m_shadowPCFRadius; }
    [[nodiscard]] float GetCascadeSplitLambda() const { return m_cascadeSplitLambda; }
    [[nodiscard]] float GetBloomIntensity() const { return m_bloomIntensity; }
    [[nodiscard]] float GetCascadeResolutionScale(uint32_t cascadeIndex) const {
        return (cascadeIndex < kShadowCascadeCount) ? m_cascadeResolutionScale[cascadeIndex] : 1.0f;
    }

    // Mutators for renderer-level commands
    void SetExposure(float exposure);
    void SetShadowsEnabled(bool enabled);
    void SetDebugViewMode(int mode);
    void SetShadowBias(float bias);
    void SetShadowPCFRadius(float radius);
    void SetCascadeSplitLambda(float lambda);
    void SetBloomIntensity(float intensity);
    void SetFractalParams(float amplitude, float frequency, float octaves,
                          float coordMode, float scaleX, float scaleZ,
                          float lacunarity = 2.0f, float gain = 0.5f,
                          float warpStrength = 0.0f, float noiseType = 0.0f);
    void SetPCSS(bool enabled) { m_pcssEnabled = enabled; }
    void SetFXAAEnabled(bool enabled) { m_fxaaEnabled = enabled; }
    [[nodiscard]] bool IsPCSS() const { return m_pcssEnabled; }
    [[nodiscard]] bool IsFXAAEnabled() const { return m_fxaaEnabled; }

private:
    static constexpr uint32_t kShadowCascadeCount = 3;

    void BeginFrame();
    void PrepareMainPass();
    void EndFrame();

    void RenderScene(Scene::ECS_Registry* registry);
    void UpdateFrameConstants(float deltaTime, Scene::ECS_Registry* registry);
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    Result<void> EnsureHyperGeometryScene(Scene::ECS_Registry* registry);
#endif

    Result<void> CreateDepthBuffer();
    Result<void> CreateCommandList();
    Result<void> CompileShaders();
    Result<void> CreatePipeline();
    Result<void> CreatePlaceholderTexture();
    Result<void> CreateShadowMapResources();
    Result<void> CreateHDRTarget();
    Result<void> CreateBloomResources();
    void RefreshMaterialDescriptors(Scene::RenderableComponent& renderable);
    void EnsureMaterialTextures(Scene::RenderableComponent& renderable);
    void RenderShadowPass(Scene::ECS_Registry* registry);
    void RenderBloom();
    void RenderPostProcess();

    // Graphics resources
    DX12Device* m_device = nullptr;
    Window* m_window = nullptr;

    std::unique_ptr<DX12CommandQueue> m_commandQueue;
    std::unique_ptr<DX12CommandQueue> m_uploadQueue;
    std::unique_ptr<DescriptorHeapManager> m_descriptorManager;
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    std::unique_ptr<HyperGeometry::HyperGeometryEngine> m_hyperGeometry;
#endif

    ComPtr<ID3D12CommandAllocator> m_commandAllocators[3];  // One per frame
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    uint32_t m_frameIndex = 0;

    // Pipeline state
    std::unique_ptr<DX12RootSignature> m_rootSignature;
    std::unique_ptr<DX12Pipeline> m_pipeline;
    std::unique_ptr<DX12Pipeline> m_shadowPipeline;
    std::unique_ptr<DX12Pipeline> m_postProcessPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurHPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurVPipeline;

    // Constant buffers
    ConstantBuffer<FrameConstants> m_frameConstantBuffer;
    ConstantBuffer<ObjectConstants> m_objectConstantBuffer;
    ConstantBuffer<MaterialConstants> m_materialConstantBuffer;
    ConstantBuffer<ShadowConstants> m_shadowConstantBuffer;

    // Upload helpers
    static constexpr uint32_t kUploadPoolSize = 4;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kUploadPoolSize> m_uploadCommandAllocators;
    std::array<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>, kUploadPoolSize> m_uploadCommandLists;
    uint32_t m_uploadAllocatorIndex = 0;
    std::array<uint64_t, kUploadPoolSize> m_uploadFences{0, 0, 0, 0};
    uint64_t m_pendingUploadFence = 0;

    // Depth buffer
    ComPtr<ID3D12Resource> m_depthBuffer;
    DescriptorHandle m_depthStencilView;

    // Shadow map (directional light, cascaded)
    ComPtr<ID3D12Resource> m_shadowMap;
    std::array<DescriptorHandle, kShadowCascadeCount> m_shadowMapDSVs;
    DescriptorHandle m_shadowMapSRV;
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissor{};
    D3D12_RESOURCE_STATES m_shadowMapState = D3D12_RESOURCE_STATE_COMMON;

    // HDR color target for main pass
    ComPtr<ID3D12Resource> m_hdrColor;
    DescriptorHandle m_hdrRTV;
    DescriptorHandle m_hdrSRV;
    D3D12_RESOURCE_STATES m_hdrState = D3D12_RESOURCE_STATE_COMMON;

    // Bloom textures (quarter resolution ping-pong)
    ComPtr<ID3D12Resource> m_bloomTexA;
    ComPtr<ID3D12Resource> m_bloomTexB;
    DescriptorHandle m_bloomRTV[2];
    DescriptorHandle m_bloomSRV[2];
    D3D12_RESOURCE_STATES m_bloomState[2] = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };

    // Default resources
    std::shared_ptr<DX12Texture> m_placeholderAlbedo;
    std::shared_ptr<DX12Texture> m_placeholderNormal;
    std::shared_ptr<DX12Texture> m_placeholderMetallic;
    std::shared_ptr<DX12Texture> m_placeholderRoughness;

    // Lighting state
    glm::vec3 m_directionalLightDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)); // direction from surface to light
    glm::vec3 m_directionalLightColor = glm::vec3(1.0f);
    float m_directionalLightIntensity = 5.0f;
    glm::vec3 m_ambientLightColor = glm::vec3(0.04f);
    float m_ambientLightIntensity = 1.0f;
    float m_exposure = 1.0f;
    float m_bloomIntensity = 0.25f;

    bool m_shadowsEnabled = true;
    float m_shadowMapSize = 2048.0f;
    float m_shadowBias = 0.0005f;
    float m_shadowPCFRadius = 1.5f;

    // Camera-followed shadow frustum parameters
    float m_shadowOrthoRange = 20.0f;
    float m_shadowNearPlane = 1.0f;
    float m_shadowFarPlane = 100.0f;

    glm::mat4 m_lightViewMatrix{1.0f};
    glm::mat4 m_lightProjectionMatrices[kShadowCascadeCount]{};
    glm::mat4 m_lightViewProjectionMatrices[kShadowCascadeCount]{};
    float m_cascadeSplits[kShadowCascadeCount]{};
    float m_cascadeSplitLambda = 0.5f;
    float m_cascadeResolutionScale[kShadowCascadeCount] = { 1.0f, 1.0f, 1.0f };

    uint32_t m_debugViewMode = 0;
    bool m_pcssEnabled = false;
    bool m_fxaaEnabled = true;

    // Global fractal surface parameters (applied uniformly to all materials)
    float m_fractalAmplitude = 0.0f;
    float m_fractalFrequency = 0.5f;
    float m_fractalOctaves = 4.0f;
    float m_fractalCoordMode = 1.0f; // 0 = UV, 1 = world XZ
    float m_fractalScaleX = 1.0f;
    float m_fractalScaleZ = 1.0f;
    float m_fractalLacunarity = 2.0f;
    float m_fractalGain = 0.5f;
    float m_fractalWarpStrength = 0.0f;
    float m_fractalNoiseType = 0.0f;

    // Frame state
    float m_totalTime = 0.0f;
    uint64_t m_fenceValues[3] = { 0, 0, 0 };
    FrameConstants m_frameDataCPU{};
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    bool m_hyperSceneBuilt = false;
#endif
};

} // namespace Cortex::Graphics
