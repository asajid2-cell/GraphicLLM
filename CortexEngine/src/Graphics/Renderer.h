#pragma once

#include <memory>
#include <array>
#include <unordered_map>
#include <string>
#include <spdlog/spdlog.h>
#include "Core/Window.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/DX12Raytracing.h"
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif
#include "ShaderTypes.h"
#include "Utils/Result.h"
#include "../Scene/Components.h"
#include "MeshBuffers.h"

namespace Cortex {
    class Window;
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

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

    // Create a GPU texture from in-memory RGBA8 data (used by the Dreamer
    // diffusion pipeline to upload generated textures on the main thread).
    Result<std::shared_ptr<DX12Texture>> CreateTextureFromRGBA(
        const uint8_t* data,
        uint32_t width,
        uint32_t height,
        bool useSRGB,
        const std::string& debugName);

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
    [[nodiscard]] std::string GetCurrentEnvironmentName() const;

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

    // Lighting rigs / presets
    enum class LightingRig {
        Custom = 0,
        StudioThreePoint = 1,
        TopDownWarehouse = 2,
        HorrorSideLight = 3,
        StreetLanterns = 4
    };

    void ApplyLightingRig(LightingRig rig, Scene::ECS_Registry* registry);
    // Image-based lighting / environment controls
    void SetEnvironmentPreset(const std::string& name);
    void SetIBLIntensity(float diffuseIntensity, float specularIntensity);
    void SetIBLEnabled(bool enabled);
    void CycleEnvironmentPreset();
    void SetColorGrade(float warm, float cool);
    void SetSSAOEnabled(bool enabled);
    void SetSSAOParams(float radius, float bias, float intensity);
    void SetPCSS(bool enabled) { m_pcssEnabled = enabled; }
    void SetFXAAEnabled(bool enabled) { m_fxaaEnabled = enabled; }
    void SetTAAEnabled(bool enabled);
    bool IsTAAEnabled() const { return m_taaEnabled; }
    void ToggleTAA();
    void SetSSREnabled(bool enabled);
    void ToggleSSR();
    void CycleScreenSpaceEffectsDebug();
    void SetFogEnabled(bool enabled);
    void SetFogParams(float density, float height, float falloff);
    [[nodiscard]] bool IsFogEnabled() const { return m_fogEnabled; }
    [[nodiscard]] float GetFogDensity() const { return m_fogDensity; }
    [[nodiscard]] float GetFogHeight() const { return m_fogHeight; }
    [[nodiscard]] float GetFogFalloff() const { return m_fogFalloff; }
    [[nodiscard]] bool IsPCSS() const { return m_pcssEnabled; }
    [[nodiscard]] bool IsFXAAEnabled() const { return m_fxaaEnabled; }
    [[nodiscard]] bool GetSSAOEnabled() const { return m_ssaoEnabled; }
    [[nodiscard]] bool GetIBLEnabled() const { return m_iblEnabled; }
    [[nodiscard]] bool GetSSREnabled() const { return m_ssrEnabled; }

    // Water / liquid controls
    void SetWaterParams(float levelY, float amplitude, float waveLength, float speed,
                        float dirX = 1.0f, float dirZ = 0.0f, float secondaryAmplitude = 0.0f,
                        float steepness = 0.6f);
    // Sample the procedural water height at a given world-space XZ position.
    // This mirrors the wave function used in Water.hlsl so buoyancy and
    // other CPU-side systems can stay in sync with the GPU water surface.
    float SampleWaterHeightAt(const glm::vec2& worldXZ) const;
    [[nodiscard]] float GetWaterLevel() const { return m_waterLevelY; }
    [[nodiscard]] float GetWaterWaveAmplitude() const { return m_waterWaveAmplitude; }
    [[nodiscard]] float GetWaterWaveLength() const { return m_waterWaveLength; }
    [[nodiscard]] float GetWaterWaveSpeed() const { return m_waterWaveSpeed; }
    [[nodiscard]] float GetWaterSecondaryAmplitude() const { return m_waterSecondaryAmplitude; }
    [[nodiscard]] float GetWaterSteepness() const { return m_waterSteepness; }
    [[nodiscard]] glm::vec2 GetWaterPrimaryDir() const { return m_waterPrimaryDir; }

    void SetGodRayIntensity(float intensity);
    [[nodiscard]] float GetGodRayIntensity() const { return m_godRayIntensity; }

    void SetAreaLightSizeScale(float scale);
    [[nodiscard]] float GetAreaLightSizeScale() const { return m_areaLightSizeScale; }

    [[nodiscard]] float GetRenderScale() const { return m_renderScale; }
    void SetRenderScale(float scale) {
        // Ignore render-scale changes after a fatal device-removed event; the
        // renderer is no longer in a state where reallocating major resources
        // is meaningful.
        if (m_deviceRemoved) {
            return;
        }
        m_renderScale = std::clamp(scale, 0.5f, 1.5f);
    }

    // Optional RT feature toggles exposed to UI.
    void SetRTReflectionsEnabled(bool enabled) { m_rtReflectionsEnabled = enabled; }
    void SetRTGIEnabled(bool enabled) { m_rtGIEnabled = enabled; }
    [[nodiscard]] bool GetRTReflectionsEnabled() const { return m_rtReflectionsEnabled; }
    [[nodiscard]] bool GetRTGIEnabled() const { return m_rtGIEnabled; }

    // Approximate VRAM usage for the current frame, in megabytes. This is
    // intentionally coarse and only sums major render targets (HDR, depth,
    // SSAO, SSR, RT buffers, shadow maps) plus a rough estimate for mesh
    // buffers so the UI can provide a sense of GPU pressure.
    [[nodiscard]] float GetEstimatedVRAMMB() const;

    // Conservative quality preset intended for lower-end or
    // memory-constrained GPUs. Scales resolution down and disables
    // expensive RT and screen-space effects.
    void ApplySafeQualityPreset();

    // Ray tracing capability and toggle (DXR)
    [[nodiscard]] bool IsRayTracingSupported() const { return m_rayTracingSupported; }
    [[nodiscard]] bool IsRayTracingEnabled() const { return m_rayTracingEnabled; }
    [[nodiscard]] bool IsDeviceRemoved() const { return m_deviceRemoved; }
    void SetRayTracingEnabled(bool enabled);
    // Clear temporal history (TAA, RT shadows/GI) when the scene is rebuilt
    // so the new layout does not inherit afterimages from the old one.
    void ResetTemporalHistoryForSceneChange();
    // Dynamically register an environment map from an existing texture (used by Dreamer).
    Result<void> AddEnvironmentFromTexture(const std::shared_ptr<DX12Texture>& tex, const std::string& name);

    // Direct sun controls for LLM/renderer integration.
    void SetSunDirection(const glm::vec3& dir);
    void SetSunColor(const glm::vec3& color);
    void SetSunIntensity(float intensity);

private:
    static constexpr uint32_t kShadowCascadeCount = 3;
    // Total shadow-map array slices: cascades (sun) + local lights.
    static constexpr uint32_t kMaxShadowedLocalLights = 3;
    static constexpr uint32_t kShadowArraySize = kShadowCascadeCount + kMaxShadowedLocalLights;
    static constexpr uint32_t kBloomLevels = 3;

    void BeginFrame();
    void RenderDepthPrepass(Scene::ECS_Registry* registry);
    void PrepareMainPass();
    void EndFrame();

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
    Result<void> CreateSSAOResources();
    Result<void> CreateRTShadowMask();
    Result<void> CreateRTReflectionResources();
    Result<void> CreateRTGIResources();
    Result<void> InitializeEnvironmentMaps();
    void UpdateEnvironmentDescriptorTable();
    void ProcessPendingEnvironmentMaps(uint32_t maxPerFrame);
    void RefreshMaterialDescriptors(Scene::RenderableComponent& renderable);
    void EnsureMaterialTextures(Scene::RenderableComponent& renderable);
    void RenderShadowPass(Scene::ECS_Registry* registry);
    void RenderSkybox();
    void RenderScene(Scene::ECS_Registry* registry);
    void RenderTransparent(Scene::ECS_Registry* registry);
    void RenderSSR();
    void RenderTAA();
    void RenderMotionVectors();
    void RenderSSAO();
    void RenderBloom();
    void RenderPostProcess();
    void RenderDebugLines();
    void RenderRayTracing(Scene::ECS_Registry* registry);
    void RenderParticles(Scene::ECS_Registry* registry);

    // Debug drawing API
public:
    void AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void ClearDebugLines();

    // GPU-driven debug overlay (settings panel) state fed into FrameConstants.
    void SetDebugOverlayState(bool visible, int selectedSection) {
        m_debugOverlayVisible = visible;
        m_debugOverlaySelectedRow = selectedSection;
    }

    // Flag used by the GPU post-process path to draw a simple settings
    // overlay instead of relying on GDI, which is unreliable with modern
    // flip-model swap chains.
    void SetDebugOverlayVisible(bool visible) { m_debugOverlayVisible = visible; }

    // Graphics resources
    DX12Device* m_device = nullptr;
    Window* m_window = nullptr;

    std::unique_ptr<DX12CommandQueue> m_commandQueue;
    std::unique_ptr<DX12CommandQueue> m_uploadQueue;
    std::unique_ptr<DescriptorHeapManager> m_descriptorManager;
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    std::unique_ptr<HyperGeometry::HyperGeometryEngine> m_hyperGeometry;
#endif
    std::unique_ptr<DX12RaytracingContext> m_rayTracingContext;

    ComPtr<ID3D12CommandAllocator> m_commandAllocators[3];  // One per frame
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    uint32_t m_frameIndex = 0;

    // Pipeline state
    std::unique_ptr<DX12RootSignature> m_rootSignature;
    std::unique_ptr<DX12Pipeline> m_pipeline;
    // Blended variant of the main PBR pipeline used for glass/transparent
    // materials. Shares the same shaders and input layout but enables
    // alpha blending and disables depth writes so transparent surfaces can
    // be rendered back-to-front over the opaque scene.
    std::unique_ptr<DX12Pipeline> m_transparentPipeline;
    std::unique_ptr<DX12Pipeline> m_depthOnlyPipeline;
    std::unique_ptr<DX12Pipeline> m_shadowPipeline;
    // Fullscreen / post pipelines
    std::unique_ptr<DX12Pipeline> m_postProcessPipeline;
    std::unique_ptr<DX12Pipeline> m_taaPipeline;
    std::unique_ptr<DX12Pipeline> m_ssrPipeline;
    std::unique_ptr<DX12Pipeline> m_ssaoPipeline;
    std::unique_ptr<DX12Pipeline> m_motionVectorsPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurHPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurVPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomCompositePipeline;
    std::unique_ptr<DX12Pipeline> m_skyboxPipeline;
    std::unique_ptr<DX12Pipeline> m_debugLinePipeline;
    std::unique_ptr<DX12Pipeline> m_waterPipeline;
    std::unique_ptr<DX12Pipeline> m_particlePipeline;

    // If we ever fail to map the particle instance buffer (for example due
    // to device removal or severe memory pressure), flip this flag so that
    // subsequent frames simply skip particle rendering instead of spamming
    // warnings or risking further failures.
    bool m_particleBufferMapFailed = false;

    bool m_debugOverlayVisible = false;
    int  m_debugOverlaySelectedRow = 0;

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
    DescriptorHandle m_depthSRV;
    D3D12_RESOURCE_STATES m_depthState = D3D12_RESOURCE_STATE_COMMON;

    // Shadow map (directional light, cascaded)
    ComPtr<ID3D12Resource> m_shadowMap;
    std::array<DescriptorHandle, kShadowArraySize> m_shadowMapDSVs;
    DescriptorHandle m_shadowMapSRV;
    // Shadow + environment descriptor table (space1):
    //   t0 = shadow map array
    //   t1 = diffuse IBL
    //   t2 = specular IBL
    //   t3 = RT shadow mask (optional, DXR)
    //   t4 = RT shadow mask history (optional, DXR)
    //   t5 = RT diffuse GI buffer (optional, DXR)
    //   t6 = RT diffuse GI history buffer (optional, DXR)
    std::array<DescriptorHandle, 7> m_shadowAndEnvDescriptors{};
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissor{};
    D3D12_RESOURCE_STATES m_shadowMapState = D3D12_RESOURCE_STATE_COMMON;

    // HDR color target for main pass
    ComPtr<ID3D12Resource> m_hdrColor;
    DescriptorHandle m_hdrRTV;
    DescriptorHandle m_hdrSRV;
    D3D12_RESOURCE_STATES m_hdrState = D3D12_RESOURCE_STATE_COMMON;

    // RT sun shadow mask and simple history buffer used for temporal
    // smoothing. These are only created when DXR is supported; shaders
    // treat them as optional and fall back to cascaded shadows when
    // unavailable.
    ComPtr<ID3D12Resource> m_rtShadowMask;
    DescriptorHandle m_rtShadowMaskSRV;
    DescriptorHandle m_rtShadowMaskUAV;
    D3D12_RESOURCE_STATES m_rtShadowMaskState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> m_rtShadowMaskHistory;
    DescriptorHandle m_rtShadowMaskHistorySRV;
    D3D12_RESOURCE_STATES m_rtShadowMaskHistoryState = D3D12_RESOURCE_STATE_COMMON;
    bool m_rtHasHistory = false;
    // G-buffer target storing world-space normal (xyz) and roughness (w)
    ComPtr<ID3D12Resource> m_gbufferNormalRoughness;
    DescriptorHandle m_gbufferNormalRoughnessRTV;
    DescriptorHandle m_gbufferNormalRoughnessSRV;
    D3D12_RESOURCE_STATES m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_COMMON;

    // SSAO target (single-channel occlusion)
    ComPtr<ID3D12Resource> m_ssaoTex;
    DescriptorHandle m_ssaoRTV;
    DescriptorHandle m_ssaoSRV;
    D3D12_RESOURCE_STATES m_ssaoState = D3D12_RESOURCE_STATE_COMMON;

    // Screen-space reflection color buffer
    ComPtr<ID3D12Resource> m_ssrColor;
    DescriptorHandle m_ssrRTV;
    DescriptorHandle m_ssrSRV;
    D3D12_RESOURCE_STATES m_ssrState = D3D12_RESOURCE_STATE_COMMON;

    // Camera motion vector buffer (UV-space velocity)
    ComPtr<ID3D12Resource> m_velocityBuffer;
    DescriptorHandle m_velocityRTV;
    DescriptorHandle m_velocitySRV;
    D3D12_RESOURCE_STATES m_velocityState = D3D12_RESOURCE_STATE_COMMON;

    // RT reflection color target written by DXR (hybrid SSR/RT path) and an
    // optional history buffer for simple temporal accumulation/denoising.
    ComPtr<ID3D12Resource> m_rtReflectionColor;
    DescriptorHandle m_rtReflectionSRV;
    DescriptorHandle m_rtReflectionUAV;
    D3D12_RESOURCE_STATES m_rtReflectionState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> m_rtReflectionHistory;
    DescriptorHandle m_rtReflectionHistorySRV;
    D3D12_RESOURCE_STATES m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_COMMON;

    // RT diffuse global illumination buffer written by DXR. This is a
    // low-frequency indirect lighting term that can be sampled by the main
    // PBR shader or viewed in debug modes. A small history buffer is kept
    // alongside it for simple temporal accumulation.
    ComPtr<ID3D12Resource> m_rtGIColor;
    DescriptorHandle m_rtGISRV;
    DescriptorHandle m_rtGIUAV;
    D3D12_RESOURCE_STATES m_rtGIState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> m_rtGIHistory;
    DescriptorHandle m_rtGIHistorySRV;
    D3D12_RESOURCE_STATES m_rtGIHistoryState = D3D12_RESOURCE_STATE_COMMON;
    bool m_rtGIHasHistory = false;

    // Bloom textures as a small mip pyramid (multi-scale, ping-pong per level)
    ComPtr<ID3D12Resource> m_bloomTexA[kBloomLevels];
    ComPtr<ID3D12Resource> m_bloomTexB[kBloomLevels];
    DescriptorHandle m_bloomRTV[kBloomLevels][2];
    DescriptorHandle m_bloomSRV[kBloomLevels][2];
    D3D12_RESOURCE_STATES m_bloomState[kBloomLevels][2] = {};
    // SRV pointing to the final combined bloom texture used by post-process
    DescriptorHandle m_bloomCombinedSRV;

    // Default resources
    std::shared_ptr<DX12Texture> m_placeholderAlbedo;
    std::shared_ptr<DX12Texture> m_placeholderNormal;
    std::shared_ptr<DX12Texture> m_placeholderMetallic;
    std::shared_ptr<DX12Texture> m_placeholderRoughness;

    // Debug line rendering (world-space overlay)
    struct DebugLineVertex {
        glm::vec3 position;
        glm::vec4 color;
    };
    std::vector<DebugLineVertex> m_debugLines;
    // Transient vertex buffer reused across frames to avoid per-frame heap
    // allocations for debug lines.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_debugLineVertexBuffer;
    uint32_t m_debugLineVertexCapacity = 0;
    bool m_debugLinesDisabled = false;

    // Environment maps for image-based lighting
    struct EnvironmentMaps {
        std::string name;                                  // display name
        std::shared_ptr<DX12Texture> diffuseIrradiance;    // low-frequency env for diffuse
        std::shared_ptr<DX12Texture> specularPrefiltered;  // mip-chain env for specular
    };

    std::vector<EnvironmentMaps> m_environmentMaps;

    struct PendingEnvironment {
        std::string path;
        std::string name;
    };
    std::vector<PendingEnvironment> m_pendingEnvironments;

    size_t m_currentEnvironment = 0;
    float m_iblDiffuseIntensity = 1.1f;
    float m_iblSpecularIntensity = 1.3f;
    // Start in a neutral "no IBL" mode so the engine boots into the default
    // background; environments remain loaded and can be enabled via the
    // debug menu or environment cycling controls.
    bool m_iblEnabled = false;

    // Lighting state
    glm::vec3 m_directionalLightDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)); // direction from surface to light
    glm::vec3 m_directionalLightColor = glm::vec3(1.0f);
    float m_directionalLightIntensity = 5.0f;
    glm::vec3 m_ambientLightColor = glm::vec3(0.04f);
    float m_ambientLightIntensity = 1.0f;
    float m_exposure = 1.0f;
    // Internal rendering resolution scale for simple supersampling. Default
    // to 1.0 so that HDR and depth targets match the window resolution; this
    // keeps VRAM usage predictable on 8 GB GPUs. For heavier scenes this can
    // be reduced (e.g. 0.75) to trade some sharpness for significantly lower
    // VRAM usage and shading cost.
    float m_renderScale = 1.0f;
    float m_bloomIntensity = 0.25f;
    float m_bloomThreshold = 1.0f;
    float m_bloomSoftKnee = 0.5f;
    float m_bloomMaxContribution = 4.0f;

    // Temporal anti-aliasing (camera-only) state
    bool  m_taaEnabled = false;
    float m_taaBlendFactor = 0.06f;
    // Approximate camera motion flag for jitter/taa tuning.
    bool  m_cameraIsMoving = false;
    // Cached camera parameters for the current frame, used by culling and RT.
    glm::vec3 m_cameraPositionWS{0.0f};
    glm::vec3 m_cameraForwardWS{0.0f, 0.0f, 1.0f};
    float m_cameraNearPlane = 0.1f;
    float m_cameraFarPlane  = 1000.0f;
    bool  m_hasHistory = false;
    glm::vec2 m_taaJitterPrevPixels{0.0f, 0.0f};
    glm::vec2 m_taaJitterCurrPixels{0.0f, 0.0f};
    uint32_t m_taaSampleIndex = 0;
    glm::mat4 m_prevViewProjMatrix{1.0f};
    bool m_hasPrevViewProj = false;
    // HDR history buffer for TAA (matches HDR color format) and an intermediate
    // resolve target used when reprojecting into the current frame.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_historyColor;
    DescriptorHandle m_historySRV;
    D3D12_RESOURCE_STATES m_historyState = D3D12_RESOURCE_STATE_COMMON;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_taaIntermediate;
    DescriptorHandle m_taaIntermediateRTV;
    D3D12_RESOURCE_STATES m_taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

      bool m_shadowsEnabled = true;
    float m_shadowMapSize = 2048.0f;
    float m_shadowBias = 0.0005f;
    float m_shadowPCFRadius = 1.5f;
    bool  m_hasLocalShadow = false;
    uint32_t m_localShadowCount = 0;
    glm::mat4 m_localLightViewProjMatrices[kMaxShadowedLocalLights]{};

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
      bool m_ssrEnabled = true;
      bool m_rayTracingSupported = false;
      bool m_rayTracingEnabled = false;
      // Optional RT feature toggles; RT shadows follow the main toggle,
      // while reflections and GI can be enabled separately once stability
      // and memory headroom have been verified.
      bool m_rtReflectionsEnabled = false;
      bool m_rtGIEnabled = false;
      // Sticky flag set when the DX12 device reports "device removed" during
      // resource creation (typically due to GPU memory pressure). Once this
      // is true the renderer will skip further heavy work for the remainder
      // of the run so scene rebuilds do not spam errors.
      bool m_deviceRemoved = false;
      bool m_deviceRemovedLogged = false;

      // Logging throttles so we do not spam the console with the same
      // warnings every frame once the renderer has entered an error state or
      // when scene content is incomplete.
      bool m_missingBufferWarningLogged = false;
      bool m_zeroDrawWarningLogged      = false;

      // Camera history used to decide when to reset RT temporal history
      // (shadows / GI) after large movements to avoid ghosting.
      glm::vec3 m_prevCameraPos{0.0f};
      glm::vec3 m_prevCameraForward{0.0f, 0.0f, 1.0f};
      bool      m_hasPrevCamera = false;

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

    // Simple warm/cool grading applied in post-process, plus an extra
    // channel (m_godRayIntensity) used to scale volumetric sun shafts.
    float m_colorGradeWarm = 0.0f;
    float m_colorGradeCool = 0.0f;
    float m_godRayIntensity = 1.0f;

    // Screen-space ambient occlusion parameters
    bool  m_ssaoEnabled = true;
    float m_ssaoRadius = 0.25f;
    float m_ssaoBias = 0.03f;
    float m_ssaoIntensity = 0.35f;

    // Exponential height fog parameters
    bool  m_fogEnabled = false;
    float m_fogDensity = 0.02f;
    float m_fogHeight = 0.0f;
    float m_fogFalloff = 0.5f;

    // Water / liquid parameters shared with shaders via waterParams0/1.
    // Defaults describe a calm, low-amplitude water plane at Y=0.
    float m_waterLevelY = 0.0f;
    float m_waterWaveAmplitude = 0.2f;
    float m_waterWaveLength = 8.0f;
    float m_waterWaveSpeed = 1.0f;
    glm::vec2 m_waterPrimaryDir = glm::vec2(1.0f, 0.0f);
    float m_waterSecondaryAmplitude = 0.1f;
    float m_waterSteepness = 0.6f;

    // Global scale factor applied to rectangular area light sizes when
    // packing GPU light parameters; exposed via the quick settings UI.
    float m_areaLightSizeScale = 1.0f;

    // Particle system GPU data
    struct ParticleInstance {
        glm::vec3 position;
        float     size;
        glm::vec4 color;
    };
    ComPtr<ID3D12Resource> m_particleInstanceBuffer;
    UINT                   m_particleInstanceCapacity = 0;

    // Frame state
    float m_totalTime = 0.0f;
    uint64_t m_fenceValues[3] = { 0, 0, 0 };
    FrameConstants m_frameDataCPU{};
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    bool m_hyperSceneBuilt = false;
#endif
};

} // namespace Cortex::Graphics
