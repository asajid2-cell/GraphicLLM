#pragma once

#include <memory>
#include <array>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <deque>
#include <type_traits>
#include <spdlog/spdlog.h>
#include "Core/Window.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/DX12Raytracing.h"
#include "RHI/BindlessResources.h"
#include "GPUCulling.h"
#include "RenderGraph.h"
#include "VisibilityBuffer.h"
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif
#include "ShaderTypes.h"
#include "Utils/Result.h"
#include "../Scene/Components.h"
#include "MeshBuffers.h"
#include "Graphics/AssetRegistry.h"

namespace Cortex {
    class Window;
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

// Number of frames in flight (triple buffering)
static constexpr uint32_t kFrameCount = 3;

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
    struct EnvironmentMaps;

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize renderer
    Result<void> Initialize(DX12Device* device, Window* window);
    void Shutdown();

    // Main render function
    void Render(Scene::ECS_Registry* registry, float deltaTime);

    // Writes a one-shot renderer state dump to spdlog. Intended to be called
    // at shutdown (it is also copied into the per-run log file).
    void LogDiagnostics() const;

    // Upload mesh to GPU
    Result<void> UploadMesh(std::shared_ptr<Scene::MeshData> mesh);
    // Enqueue a mesh upload to be processed by the GPU job queue.
    Result<void> EnqueueMeshUpload(const std::shared_ptr<Scene::MeshData>& mesh,
                                   const char* label);

    // Get default placeholder texture
    std::shared_ptr<DX12Texture> GetPlaceholderTexture() const { return m_placeholderAlbedo; }
    std::shared_ptr<DX12Texture> GetPlaceholderNormal() const { return m_placeholderNormal; }
    std::shared_ptr<DX12Texture> GetPlaceholderMetallic() const { return m_placeholderMetallic; }
    std::shared_ptr<DX12Texture> GetPlaceholderRoughness() const { return m_placeholderRoughness; }

    // Load texture from disk (sRGB aware). The optional kind parameter
    // allows the asset registry to separate environment maps from generic
    // material textures when reporting memory usage.
    Result<std::shared_ptr<DX12Texture>> LoadTextureFromFile(
        const std::string& path,
        bool useSRGB,
        AssetRegistry::TextureKind kind = AssetRegistry::TextureKind::Generic);

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
    void AdjustHZBDebugMip(int delta);
    void AdjustShadowBias(float delta);
    void AdjustShadowPCFRadius(float delta);
    void AdjustCascadeSplitLambda(float delta);
    void AdjustCascadeResolutionScale(uint32_t cascadeIndex, float delta);

    // Introspection for LLM/diagnostics
    [[nodiscard]] float GetExposure() const { return m_exposure; }
    [[nodiscard]] bool GetShadowsEnabled() const { return m_shadowsEnabled; }
    [[nodiscard]] int GetDebugViewMode() const { return static_cast<int>(m_debugViewMode); }
    [[nodiscard]] uint32_t GetHZBDebugMip() const { return m_hzbDebugMip; }
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
    // Manually load a limited number of deferred environment maps from disk
    // into GPU memory. Used by performance tools to incrementally test IBL
    // pressure instead of loading all HDRs at startup.
    void LoadAdditionalEnvironmentMaps(uint32_t maxToLoad);
    // Optional residency limit for IBL environments. When enabled, only a
    // small fixed number of environments are kept resident; loading or
    // creating new environments will evict the oldest unused ones in FIFO
    // order to keep VRAM usage predictable on 8 GB-class GPUs.
    void SetIBLLimitEnabled(bool enabled);
    [[nodiscard]] bool IsIBLLimitEnabled() const { return m_iblLimitEnabled; }
    [[nodiscard]] float GetIBLDiffuseIntensity() const { return m_iblDiffuseIntensity; }
    [[nodiscard]] float GetIBLSpecularIntensity() const { return m_iblSpecularIntensity; }
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

    // Per-scene particle toggle so heavy layouts on 8 GB GPUs can turn off
    // billboard particles without affecting global renderer state.
    void SetParticlesEnabled(bool enabled) { m_particlesEnabledForScene = enabled; }
    [[nodiscard]] bool GetParticlesEnabled() const { return m_particlesEnabledForScene; }
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

        float clamped = std::clamp(scale, 0.5f, 1.5f);

        // On high-resolution displays, clamp the internal render scale based
        // on the active resolution and heavy features (RT/SSR/SSAO/RT GI) so
        // that 1440p and 4K runs do not inadvertently exceed the VRAM budget.
        if (m_window) {
            const unsigned int width  = std::max(1u, m_window->GetWidth());
            const unsigned int height = std::max(1u, m_window->GetHeight());
            const bool heavyEffects =
                m_rayTracingEnabled || m_ssrEnabled || m_ssaoEnabled ||
                m_rtReflectionsEnabled || m_rtGIEnabled;

            // Approximate 4K and 1440p thresholds by height; fall back to
            // width for ultrawide setups.
            if (height >= 2160 || width >= 3840) {
                // 4K: favor 0.5–0.6 when heavy effects are enabled, allow up
                // to ~0.75 when RT/SSR/SSAO/RT GI are all off.
                const float maxScale = heavyEffects ? 0.6f : 0.75f;
                clamped = std::clamp(clamped, 0.5f, maxScale);
            } else if (height >= 1440 || width >= 2560) {
                // 1440p: favor ~0.7–0.8 when heavy effects are enabled; allow
                // full 1.0 when they are disabled.
                const float maxScale = heavyEffects ? 0.8f : 1.0f;
                clamped = std::clamp(clamped, 0.5f, maxScale);
            }
        }

        m_renderScale = clamped;
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
    [[nodiscard]] const AssetRegistry& GetAssetRegistry() const { return m_assetRegistry; }
    [[nodiscard]] AssetRegistry::MemoryBreakdown GetAssetMemoryBreakdown() const {
        return m_assetRegistry.GetMemoryBreakdown();
    }
    // Mesh key lookups for asset / BLAS management.
    [[nodiscard]] const std::unordered_map<const Scene::MeshData*, std::string>& GetMeshAssetKeys() const {
        return m_meshAssetKeys;
    }

    // Lightweight CPU-side timings for major passes, in milliseconds.
    [[nodiscard]] float GetLastMainPassTimeMS() const { return m_lastMainPassMs; }
    [[nodiscard]] float GetLastRTTimeMS()   const { return m_lastRTPassMs; }
    [[nodiscard]] float GetLastPostTimeMS() const { return m_lastPostMs; }

    // Conservative quality preset intended for lower-end or
    // memory-constrained GPUs. Scales resolution down and disables
    // expensive RT and screen-space effects.
    void ApplySafeQualityPreset();

    // Ray tracing capability and toggle (DXR)
    [[nodiscard]] bool IsRayTracingSupported() const { return m_rayTracingSupported; }
    [[nodiscard]] bool IsRayTracingEnabled() const { return m_rayTracingEnabled; }
    [[nodiscard]] bool IsDeviceRemoved() const { return m_deviceRemoved; }
    void SetRayTracingEnabled(bool enabled);

    // GPU-driven rendering (Phase 1 GPU culling + indirect draw)
    void SetGPUCullingEnabled(bool enabled);
    [[nodiscard]] bool IsGPUCullingEnabled() const { return m_gpuCullingEnabled && m_gpuCulling != nullptr; }
    void SetGPUCullingFreeze(bool enabled) { m_gpuCullingFreeze = enabled; }
    void ToggleGPUCullingFreeze() { m_gpuCullingFreeze = !m_gpuCullingFreeze; }
    [[nodiscard]] bool IsGPUCullingFreezeEnabled() const { return m_gpuCullingFreeze; }
    [[nodiscard]] bool IsIndirectDrawEnabled() const { return m_indirectDrawEnabled; }
    [[nodiscard]] uint32_t GetGPUCulledCount() const;
    [[nodiscard]] uint32_t GetGPUTotalInstances() const;
    [[nodiscard]] GPUCullingPipeline::DebugStats GetGPUCullingDebugStats() const;

    // Experimental voxel backend toggle. When enabled, the main Render path
    // skips the traditional raster + RT pipeline and instead runs a
    // fullscreen voxel raymarch pass that visualizes the scene using a
    // grid-based voxel prototype. This is wired from EngineConfig so that
    // the launcher/CLI can select it at startup.
    // Only enable the voxel backend when both requested and the experimental
    // voxel pipeline was created successfully. This prevents configuration or
    // shader compile errors from leaving the renderer in a state where the
    // classic DX12 path is skipped but the voxel path has nothing to draw.
    void SetVoxelBackendEnabled(bool enabled) {
        m_voxelBackendEnabled = enabled && (m_voxelPipeline != nullptr);
    }
    [[nodiscard]] bool IsVoxelBackendEnabled() const { return m_voxelBackendEnabled; }
    // Mark the dense voxel grid as out of date so the next voxel render pass
    // rebuilds it from the current ECS scene. Called on scene rebuilds or
    // when large structural changes occur.
    void MarkVoxelGridDirty() { m_voxelGridDirty = true; }
    // Clear temporal history (TAA, RT shadows/GI/reflections) when the scene
    // is rebuilt so the new layout does not inherit afterimages from the old
    // one.
    void ResetTemporalHistoryForSceneChange();
    // Reset the command list state before scene changes to prevent referencing
    // objects that are about to be deleted. This closes any pending command list
    // and resets it to a clean state.
    void ResetCommandList();

    // Clear all cached BLAS (Bottom-Level Acceleration Structures) during scene
    // switches. Call this AFTER ResetCommandList() has completed to ensure no
    // GPU operations are still referencing the BLAS resources.
    void ClearBLASCache();

    // Wait for ALL in-flight frames to complete (not just the current one).
    // Use this during scene switches to ensure frames N-1 and N-2 are also done
    // before destroying resources they might still be using.
    void WaitForAllFrames();
    // Dynamically register an environment map from an existing texture (used by Dreamer).
    Result<void> AddEnvironmentFromTexture(const std::shared_ptr<DX12Texture>& tex, const std::string& name);

    // Direct sun controls for LLM/renderer integration.
    void SetSunDirection(const glm::vec3& dir);
    void SetSunColor(const glm::vec3& color);
    void SetSunIntensity(float intensity);
    [[nodiscard]] float GetSunIntensity() const { return m_directionalLightIntensity; }

    // GPU job queue introspection for incremental loading / diagnostics.
    [[nodiscard]] bool HasPendingGpuJobs() const { return !m_gpuJobQueue.empty(); }
    [[nodiscard]] uint32_t GetPendingMeshJobs() const { return m_pendingMeshJobs; }
    [[nodiscard]] uint32_t GetPendingBLASJobs() const { return m_pendingBLASJobs; }
    // Conservative signal that RT is still "warming up" its BLAS set.
    [[nodiscard]] bool IsRTWarmingUp() const;

    // Block until all outstanding GPU work on the main and upload queues has
    // completed. Used sparingly before large reallocations (e.g., when
    // resizing depth/HDR targets after a render-scale change) to avoid
    // transient allocation spikes on memory-constrained GPUs.
    void WaitForGPU();

    // Lighting rig safety toggle: when true, ApplyLightingRig selects a
    // lower-intensity / fewer-shadows variant on <=8 GB adapters.
    void SetUseSafeLightingRigOnLowVRAM(bool enabled) { m_useSafeLightingRigOnLowVRAM = enabled; }
    [[nodiscard]] bool GetUseSafeLightingRigOnLowVRAM() const { return m_useSafeLightingRigOnLowVRAM; }

    // Asset lifetime helpers used by the engine after scene rebuilds.
    void RebuildAssetRefsFromScene(Scene::ECS_Registry* registry);
    void PruneUnusedMeshes(Scene::ECS_Registry* registry);
    void PruneUnusedTextures();

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
    Result<void> CreateHZBResources();
    Result<void> CreateCommandList();
    Result<void> CompileShaders();
    Result<void> CreatePipeline();
    Result<void> CreatePlaceholderTexture();
    Result<void> CreateShadowMapResources();
    void RecreateShadowMapResourcesForCurrentSize();
    Result<void> CreateHDRTarget();
    Result<void> CreateVisibilityBuffer();
    Result<void> CreateBloomResources();
    Result<void> CreateSSAOResources();
    Result<void> CreateRTShadowMask();
    Result<void> CreateRTReflectionResources();
    Result<void> CreateRTGIResources();
    Result<void> InitializeEnvironmentMaps();
    void UpdateEnvironmentDescriptorTable();
    void EnsureEnvironmentBindlessSRVs(EnvironmentMaps& env);
    void ProcessPendingEnvironmentMaps(uint32_t maxPerFrame);
    void EnforceIBLResidencyLimit();
    void PrewarmMaterialDescriptors(Scene::ECS_Registry* registry);
    void RefreshMaterialDescriptors(Scene::RenderableComponent& renderable);
    void EnsureMaterialTextures(Scene::RenderableComponent& renderable);
    void FillMaterialTextureIndices(const Scene::RenderableComponent& renderable,
                                    MaterialConstants& materialData) const;
    void RenderShadowPass(Scene::ECS_Registry* registry);
    void RenderSkybox();
    void RenderScene(Scene::ECS_Registry* registry);
    void RenderOverlays(Scene::ECS_Registry* registry);
    void RenderWaterSurfaces(Scene::ECS_Registry* registry);
    void RenderTransparent(Scene::ECS_Registry* registry);
    void RenderSSR();
    void RenderTAA();
    void RenderMotionVectors();
    void BuildHZBFromDepth();
    void AddHZBFromDepthPasses_RG(RenderGraph& graph, RGResourceHandle depthHandle, RGResourceHandle hzbHandle);
    Result<void> InitializeTAAResolveDescriptorTable();
    void UpdateTAAResolveDescriptorTable();
    Result<void> InitializePostProcessDescriptorTable();
    void UpdatePostProcessDescriptorTable();
    void RenderSSAO();
    void RenderSSAOAsync();  // Async compute version
    void RenderBloom();
    void RenderPostProcess();
    void RenderDebugLines();
    void ProcessGpuJobsPerFrame();
    void RenderVoxel(Scene::ECS_Registry* registry);
    Result<void> BuildVoxelGridFromScene(Scene::ECS_Registry* registry);
    Result<void> UploadVoxelGridToGPU();
    void RenderRayTracing(Scene::ECS_Registry* registry);
    void RenderRayTracedReflections();
    void RenderParticles(Scene::ECS_Registry* registry);

    // GPU-driven rendering (Phase 1)
    void CollectInstancesForGPUCulling(Scene::ECS_Registry* registry);
    void DispatchGPUCulling();
    void RenderSceneIndirect(Scene::ECS_Registry* registry);

    // Stable IDs for occlusion history indexing (maps entity -> cullingId).
    struct EntityHash {
        size_t operator()(entt::entity e) const noexcept {
            using Underlying = std::underlying_type_t<entt::entity>;
            return std::hash<Underlying>{}(static_cast<Underlying>(e));
        }
    };
    std::unordered_map<entt::entity, uint32_t, EntityHash> m_gpuCullingIdByEntity;
    std::vector<uint32_t> m_gpuCullingIdFreeList;
    // Per-slot generation counters to prevent occlusion-history smear when
    // cullingId slots are recycled (packed into cullingId as gen<<16 | slot).
    std::vector<uint16_t> m_gpuCullingIdGeneration;
    uint32_t m_gpuCullingNextId = 0;

    // Previous frame world-space centers for motion-inflated occlusion culling.
    std::unordered_map<entt::entity, glm::vec3, EntityHash> m_gpuCullingPrevCenterByEntity;

    // Visibility buffer rendering (Phase 2.1)
    void CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry);
    void RenderVisibilityBufferPath(Scene::ECS_Registry* registry);

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
    std::unique_ptr<DX12CommandQueue> m_computeQueue;  // Async compute for parallel workloads
    std::unique_ptr<DescriptorHeapManager> m_descriptorManager;
    std::unique_ptr<BindlessResourceManager> m_bindlessManager;
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    std::unique_ptr<HyperGeometry::HyperGeometryEngine> m_hyperGeometry;
#endif
    std::unique_ptr<DX12RaytracingContext> m_rayTracingContext;
    std::unique_ptr<GPUCullingPipeline> m_gpuCulling;
    std::unique_ptr<RenderGraph> m_renderGraph;
    std::unique_ptr<VisibilityBufferRenderer> m_visibilityBuffer;

    ComPtr<ID3D12CommandAllocator> m_commandAllocators[kFrameCount];  // One per frame
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    // Async compute command resources
    ComPtr<ID3D12CommandAllocator> m_computeAllocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> m_computeCommandList;
    uint64_t m_computeFenceValues[kFrameCount] = {0, 0, 0};
    bool m_asyncComputeSupported = false;
    uint32_t m_frameIndex = 0;
    uint64_t m_absoluteFrameIndex = 0;  // Monotonically increasing frame counter
    bool m_commandListOpen = false;  // Tracks whether command list is currently recording
    bool m_computeListOpen = false;  // Tracks compute command list state

    // Pipeline state
    std::unique_ptr<DX12RootSignature> m_rootSignature;
    std::unique_ptr<DX12ComputeRootSignature> m_computeRootSignature;  // For compute shaders
    std::unique_ptr<DX12Pipeline> m_pipeline;
    // Depth-tested, depth-write-disabled overlay pipeline (decals/markings) that
    // renders HDR-only so it does not disturb GBuffer/normal targets.
    std::unique_ptr<DX12Pipeline> m_overlayPipeline;
    // Blended variant of the main PBR pipeline used for glass/transparent
    // materials. Shares the same shaders and input layout but enables
    // alpha blending and disables depth writes so transparent surfaces can
    // be rendered back-to-front over the opaque scene.
    std::unique_ptr<DX12Pipeline> m_transparentPipeline;
    std::unique_ptr<DX12Pipeline> m_depthOnlyPipeline;
    std::unique_ptr<DX12Pipeline> m_shadowPipeline;
    std::unique_ptr<DX12Pipeline> m_shadowPipelineDoubleSided;
    std::unique_ptr<DX12Pipeline> m_shadowAlphaPipeline;
    std::unique_ptr<DX12Pipeline> m_shadowAlphaDoubleSidedPipeline;
    // Fullscreen / post pipelines
    std::unique_ptr<DX12Pipeline> m_postProcessPipeline;
    std::unique_ptr<DX12Pipeline> m_taaPipeline;
    std::unique_ptr<DX12Pipeline> m_ssrPipeline;
    std::unique_ptr<DX12Pipeline> m_ssaoPipeline;
    std::unique_ptr<DX12ComputePipeline> m_ssaoComputePipeline;  // Async compute version of SSAO
    std::unique_ptr<DX12ComputePipeline> m_hzbInitPipeline;
    std::unique_ptr<DX12ComputePipeline> m_hzbDownsamplePipeline;
    std::unique_ptr<DX12Pipeline> m_motionVectorsPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomDownsamplePipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurHPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomBlurVPipeline;
    std::unique_ptr<DX12Pipeline> m_bloomCompositePipeline;
    std::unique_ptr<DX12Pipeline> m_skyboxPipeline;
    std::unique_ptr<DX12Pipeline> m_proceduralSkyPipeline;  // For outdoor terrain (no IBL)
    std::unique_ptr<DX12Pipeline> m_debugLinePipeline;
    std::unique_ptr<DX12Pipeline> m_waterPipeline;
    std::unique_ptr<DX12Pipeline> m_waterOverlayPipeline;
    std::unique_ptr<DX12Pipeline> m_particlePipeline;
    // Experimental fullscreen voxel renderer pipeline (SV_VertexID triangle).
    std::unique_ptr<DX12Pipeline> m_voxelPipeline;

    // If we ever fail to map the particle instance buffer (for example due
    // to device removal or severe memory pressure), flip this flag so that
    // subsequent frames simply skip particle rendering instead of spamming
    // warnings or risking further failures.
    bool m_particleBufferMapFailed = false;
    bool m_particlesEnabledForScene = true;

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

    // Manual GPU breadcrumbs: a tiny readback buffer written via
    // WriteBufferImmediate so that device-removed errors can report which
    // high-level GPU marker was last executed before the fault.
    enum class GpuMarker : uint32_t {
        None              = 0,
        BeginFrame        = 1,
        ShadowPass        = 2,
        Skybox            = 3,
        OpaqueGeometry    = 4,
        TransparentGeom   = 5,
        MotionVectors     = 6,
        TAAResolve        = 7,
        SSR               = 8,
        Particles         = 9,
        SSAO              = 10,
        Bloom             = 11,
        PostProcess       = 12,
        DebugLines        = 13,
        EndFrame          = 14,
    };

    Result<void> CreateBreadcrumbBuffer();
    void WriteBreadcrumb(GpuMarker marker);

    Microsoft::WRL::ComPtr<ID3D12Resource> m_breadcrumbBuffer;
    uint32_t* m_breadcrumbMap = nullptr;

    // Asset/memory registry for approximate per-asset GPU usage accounting.
    mutable AssetRegistry m_assetRegistry;
    // Map from MeshData pointer to the registry key used for geometry/BLAS.
    std::unordered_map<const Scene::MeshData*, std::string> m_meshAssetKeys;

    // Lightweight GPU job queue used to spread heavy work (mesh uploads and
    // BLAS builds) across multiple frames to reduce first-frame spikes.
    enum class GpuJobType : uint32_t {
        MeshUpload = 0,
        BuildBLAS  = 1
    };

    struct GpuJob {
        GpuJobType type = GpuJobType::MeshUpload;
        std::shared_ptr<Scene::MeshData> mesh;          // for MeshUpload
        const Scene::MeshData* blasMeshKey = nullptr;   // for BuildBLAS
        std::string label;
    };

    std::deque<GpuJob> m_gpuJobQueue;
    uint32_t m_maxMeshJobsPerFrame = 16;  // Increased from 2 to allow faster mesh upload completion
    uint32_t m_maxBLASJobsPerFrame = 4;
    uint32_t m_pendingMeshJobs = 0;
    uint32_t m_pendingBLASJobs = 0;

    // Depth buffer
    ComPtr<ID3D12Resource> m_depthBuffer;
    DescriptorHandle m_depthStencilView;
    DescriptorHandle m_depthStencilViewReadOnly;
    DescriptorHandle m_depthSRV;
    D3D12_RESOURCE_STATES m_depthState = D3D12_RESOURCE_STATE_COMMON;

    // Hierarchical Z-buffer (depth pyramid) built from the main depth buffer.
    ComPtr<ID3D12Resource> m_hzbTexture;
    // SRV spanning the full mip chain (mip 0..mipCount-1). Used for debug
    // visualization and any pass that wants to sample multiple mips.
    DescriptorHandle m_hzbFullSRV;
    std::vector<DescriptorHandle> m_hzbMipSRVStaging;
    std::vector<DescriptorHandle> m_hzbMipUAVStaging;
    uint32_t m_hzbMipCount = 0;
    uint32_t m_hzbWidth = 0;
    uint32_t m_hzbHeight = 0;
    D3D12_RESOURCE_STATES m_hzbState = D3D12_RESOURCE_STATE_COMMON;
    bool m_hzbValid = false;
    uint32_t m_hzbDebugMip = 0;

    // Captured camera state associated with the currently valid HZB.
    // This is used by occlusion culling to project bounds in the same space
    // as the depth pyramid, and to safely gate HZB usage on camera motion.
    glm::mat4 m_hzbCaptureViewMatrix{1.0f};
    glm::mat4 m_hzbCaptureViewProjMatrix{1.0f};
    glm::vec3 m_hzbCaptureCameraPosWS{0.0f};
    glm::vec3 m_hzbCaptureCameraForwardWS{0.0f, 0.0f, 1.0f};
    float m_hzbCaptureNearPlane = 0.1f;
    float m_hzbCaptureFarPlane = 1000.0f;
    uint64_t m_hzbCaptureFrameCounter = 0;
    bool m_hzbCaptureValid = false;

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
    DescriptorHandle m_ssaoUAV;  // For async compute SSAO
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
    std::array<DescriptorHandle, 4> m_fallbackMaterialDescriptors = {};
    bool m_visibilityBufferEnabled = false;

    // Texture cache to prevent duplicate loads (CRITICAL for GPU memory management)
    std::unordered_map<std::string, std::shared_ptr<DX12Texture>> m_textureCache;

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
        std::string name;   // display name
        std::string path;   // source file on disk (if any)
        std::shared_ptr<DX12Texture> diffuseIrradiance;    // low-frequency env for diffuse
        std::shared_ptr<DX12Texture> specularPrefiltered;  // mip-chain env for specular
        // Optional persistent shader-visible SRVs for bindless access via
        // ResourceDescriptorHeap[] (used by VB deferred reflection probes).
        DescriptorHandle diffuseIrradianceSRV{};
        DescriptorHandle specularPrefilteredSRV{};
    };

    std::vector<EnvironmentMaps> m_environmentMaps;

    struct PendingEnvironment {
        std::string path;
        std::string name;
    };
    std::vector<PendingEnvironment> m_pendingEnvironments;

    size_t m_currentEnvironment = 0;
    // Optional IBL residency limit used by the performance tools. When
    // enabled, the renderer keeps at most kMaxIBLResident environments
    // resident at a time and evicts older ones in FIFO order when new
    // environments are loaded.
    bool  m_iblLimitEnabled = false;
    static constexpr uint32_t kMaxIBLResident = 4;
    float m_iblDiffuseIntensity = 1.1f;
    float m_iblSpecularIntensity = 1.3f;
    // Enable IBL by default now that environment loading is stable again
    bool m_iblEnabled = true;

    // Lighting state
    glm::vec3 m_directionalLightDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)); // direction from surface to light
    glm::vec3 m_directionalLightColor = glm::vec3(1.0f);
    float m_directionalLightIntensity = 5.0f;
    glm::vec3 m_ambientLightColor = glm::vec3(0.04f);
    float m_ambientLightIntensity = 1.0f;
    bool  m_useSafeLightingRigOnLowVRAM = false;
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
    bool  m_taaEnabled = true;  // Re-enabled now that memory issues are fixed
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
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> m_taaResolveSrvTables{};
    bool m_taaResolveSrvTableValid = false;
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> m_postProcessSrvTables{};
    bool m_postProcessSrvTableValid = false;

      bool m_shadowsEnabled = true;
    float m_shadowMapSize = 2048.0f;
    float m_shadowBias = 0.0005f;
    float m_shadowPCFRadius = 1.5f;
    bool  m_hasLocalShadow = false;
    uint32_t m_localShadowCount = 0;
    bool     m_localShadowBudgetWarningEmitted = false;
    glm::mat4 m_localLightViewProjMatrices[kMaxShadowedLocalLights]{};
    std::array<entt::entity, kMaxShadowedLocalLights> m_localShadowEntities{};

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
      // GPU-driven rendering flags
      bool m_gpuCullingEnabled = false;    // Use GPU frustum culling
      bool m_indirectDrawEnabled = false;  // Use ExecuteIndirect for draws
      bool m_gpuCullingFreeze = false;     // Freeze culling frustum (debug)
      bool m_gpuCullingFreezeCaptured = false;
      glm::mat4 m_gpuCullingFrozenViewProj{1.0f};
      glm::vec3 m_gpuCullingFrozenCameraPos{0.0f};
      // Cached instance data for GPU culling (rebuilt each frame from ECS)
      std::vector<GPUInstanceData> m_gpuInstances;
      // Mesh info for indirect draws (maps instance -> mesh draw args)
      std::vector<MeshInfo> m_meshInfos;
      // Visibility buffer instance data (Phase 2.1)
      std::vector<VBInstanceData> m_vbInstances;
      std::vector<VisibilityBufferRenderer::VBMeshDrawInfo> m_vbMeshDraws;
      bool m_vbPlannedThisFrame = false;
      bool m_vbRenderedThisFrame = false;
      bool m_vbDebugOverrideThisFrame = false;
      // Sticky flag set when the DX12 device reports "device removed" during
      // resource creation (typically due to GPU memory pressure). Once this
      // is true the renderer will skip further heavy work for the remainder
      // of the run so scene rebuilds do not spam errors.
      bool m_deviceRemoved = false;
      bool m_deviceRemovedLogged = false;

      // Lightweight runtime diagnostics for device-removed hangs. We track
      // the last successfully completed high-level pass within Render() and
      // a monotonically increasing frame counter so that device removal
      // logs can report where and when the failure was first observed.
      const char* m_lastCompletedPass = "None";
      uint64_t    m_renderFrameCounter = 0;

      // When true, the renderer skips the classic raster/IBL/RT path and
      // instead runs the experimental voxel raymarch backend. This is
      // currently a prototype path intended for research and can be
      // selected via the launcher or CLI.
      bool m_voxelBackendEnabled = false;

      // Dense voxel grid backing the experimental voxel renderer. For now we
      // build a uniform grid in CPU memory and upload it to a structured
      // buffer SRV that the voxel pixel shader reads from during DDA
      // traversal. This acts as a bridge toward a future sparse voxel octree.
      // The default dimension of 384 yields ~56.6M voxels (~216 MB at
      // 4 bytes each), which keeps the raymarch reasonably fast on the 8 GB
      // target GPU while still providing high detail when combined with
      // interior triangle sampling.
      std::vector<uint32_t> m_voxelGridCPU;
      uint32_t m_voxelGridDim = 384;
      bool     m_voxelGridDirty = true;
      Microsoft::WRL::ComPtr<ID3D12Resource> m_voxelGridBuffer;
      DescriptorHandle m_voxelGridSRV{};
      // Simple mapping from material preset / tag names to compact 8-bit
      // material identifiers used by the voxel grid. Keeps the palette small
      // and stable across frames while allowing different meshes or presets
      // to show distinct colors in voxel mode.
      std::unordered_map<std::string, uint8_t> m_voxelMaterialIds;
      uint8_t m_nextVoxelMaterialId = 1;

      // Logging throttles so we do not spam the console with the same
      // warnings every frame once the renderer has entered an error state or
      // when scene content is incomplete.
      bool m_missingBufferWarningLogged = false;
      bool m_zeroDrawWarningLogged      = false;
      bool m_verboseLoggingEnabled      = true;  // Enable detailed frame-by-frame logging for debugging

    // Camera history used to decide when to reset RT temporal history
    // (shadows / GI / reflections) after large movements to avoid ghosting.
    glm::vec3 m_prevCameraPos{0.0f};
    glm::vec3 m_prevCameraForward{0.0f, 0.0f, 1.0f};
    bool      m_hasPrevCamera = false;
    bool      m_rtReflHasHistory = false;
    bool      m_rtReflectionWrittenThisFrame = false;
    // Tracks whether the swap-chain back buffer has been used as a render
    // target in the current frame so EndFrame() can transition it back to
    // PRESENT only when appropriate.
    bool      m_backBufferUsedAsRTThisFrame = false;

    // When enabled, specific passes skip their internal ResourceBarrier calls
    // because an outer RenderGraph is responsible for transitions.
    bool      m_shadowPassSkipTransitions = false;
    bool      m_postProcessSkipTransitions = false;

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

      // Internal helpers for diagnostics and error reporting.
      void MarkPassComplete(const char* passName);
      void ReportDeviceRemoved(const char* context, HRESULT hr, const char* file, int line);

    // Exponential height fog parameters
    bool  m_fogEnabled = true;  // Re-enabled now that memory issues are fixed
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
    ComPtr<ID3D12Resource> m_particleQuadVertexBuffer;

    // Frame state
    float m_totalTime = 0.0f;
    uint64_t m_fenceValues[3] = { 0, 0, 0 };
    FrameConstants m_frameDataCPU{};
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    bool m_hyperSceneBuilt = false;
#endif

    // CPU timing (approximate) for major passes, updated once per frame.
    float m_lastDepthPrepassMs = 0.0f;
    float m_lastShadowPassMs   = 0.0f;
    float m_lastMainPassMs     = 0.0f;
    float m_lastRTPassMs       = 0.0f;
    float m_lastSSRMs          = 0.0f;
    float m_lastSSAOMs         = 0.0f;
    float m_lastBloomMs        = 0.0f;
    float m_lastPostMs         = 0.0f;
};

} // namespace Cortex::Graphics
