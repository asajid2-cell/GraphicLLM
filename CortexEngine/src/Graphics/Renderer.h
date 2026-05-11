#pragma once

#include <memory>
#include <array>
#include <vector>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <deque>
#include <type_traits>
#include <atomic>
#include <cmath>
#include <initializer_list>
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
#include "FrameContract.h"
#include "Graphics/FrameFeaturePlan.h"
#include "Graphics/GpuJobQueue.h"
#include "Graphics/RTDenoiser.h"
#include "Graphics/RTReflectionSignalStats.h"
#include "Graphics/RendererAssetRuntimeState.h"
#include "Graphics/Renderer_FrameConstantsTypes.h"
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/RendererBreadcrumbState.h"
#include "Graphics/RendererCommandResourceState.h"
#include "Graphics/RendererConstantBufferState.h"
#include "Graphics/Renderer_DiagnosticsTypes.h"
#include "Graphics/RendererFramePlanningState.h"
#include "Graphics/RendererPipelineState.h"
#include "Graphics/Renderer_PipelineSetupTypes.h"
#include "Graphics/RendererQualityRuntimeState.h"
#include "Graphics/RendererBloomState.h"
#include "Graphics/RendererCameraState.h"
#include "Graphics/RendererDebugLineState.h"
#include "Graphics/RendererDepthState.h"
#include "Graphics/RendererEnvironmentState.h"
#include "Graphics/RendererGPUCullingState.h"
#include "Graphics/RendererHZBState.h"
#include "Graphics/RendererLocalShadowState.h"
#include "Graphics/RendererMainTargetState.h"
#include "Graphics/RendererMaterialTextureState.h"
#include "Graphics/RendererParticleState.h"
#include "Graphics/RendererRTState.h"
#include "Graphics/RendererSSAOState.h"
#include "Graphics/RendererServiceState.h"
#include "Graphics/RendererShadowState.h"
#include "Graphics/RendererSSRState.h"
#include "Graphics/RendererTemporalScreenState.h"
#include "Graphics/RendererTemporalState.h"
#include "Graphics/RendererTextureUploadState.h"
#include "Graphics/RendererVegetationState.h"
#include "Graphics/RendererVisibilityBufferState.h"
#include "Graphics/RendererUploadState.h"
#include "Graphics/RendererVoxelState.h"
#include "Graphics/TemporalRejectionMask.h"
#include "Graphics/TextureUploadQueue.h"
#include "Graphics/TextureUploadReceipt.h"
#include "RenderGraph.h"
#include "VisibilityBuffer.h"
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif
#include "ShaderTypes.h"
#include "Utils/Result.h"
#include "../Scene/Components.h"
#include "../Scene/BiomeTypes.h"
#include "../Scene/VegetationTypes.h"
#include "MeshBuffers.h"
#include "Graphics/AssetRegistry.h"

namespace Cortex {
    class Window;
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
    namespace Debug {
        struct GPUFrameProfile;
    }
}

namespace Cortex::Graphics {

struct TextureSourcePlan;
struct TextureUploadTicket;

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

    // Writes a one-shot renderer state dump to spdlog. Intended to be called
    // at shutdown (it is also copied into the per-run log file).
    void LogDiagnostics() const;

    // Upload mesh to GPU
    Result<void> UploadMesh(std::shared_ptr<Scene::MeshData> mesh);
    // Enqueue a mesh upload to be processed by the GPU job queue.
    Result<void> EnqueueMeshUpload(const std::shared_ptr<Scene::MeshData>& mesh,
                                   const char* label);

    // Get default placeholder texture
    [[nodiscard]] std::shared_ptr<DX12Texture> GetPlaceholderTexture() const;
    [[nodiscard]] std::shared_ptr<DX12Texture> GetPlaceholderNormal() const;
    [[nodiscard]] std::shared_ptr<DX12Texture> GetPlaceholderMetallic() const;
    [[nodiscard]] std::shared_ptr<DX12Texture> GetPlaceholderRoughness() const;

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
    [[nodiscard]] float GetExposure() const;
    [[nodiscard]] bool GetShadowsEnabled() const;
    [[nodiscard]] int GetDebugViewMode() const;
    [[nodiscard]] uint32_t GetHZBDebugMip() const;
    [[nodiscard]] float GetShadowBias() const;
    [[nodiscard]] float GetShadowPCFRadius() const;
    [[nodiscard]] float GetCascadeSplitLambda() const;
    [[nodiscard]] float GetBloomIntensity() const;
    [[nodiscard]] bool HasCapturedVisualValidation() const;
    [[nodiscard]] float GetCascadeResolutionScale(uint32_t cascadeIndex) const;
    [[nodiscard]] std::string GetCurrentEnvironmentName() const;

    // Mutators for renderer-level commands
    void SetExposure(float exposure);
    void SetShadowsEnabled(bool enabled);
    void SetDebugViewMode(int mode);
    void SetShadowBias(float bias);
    void SetShadowPCFRadius(float radius);
    void SetCascadeSplitLambda(float lambda);
    void SetBloomIntensity(float intensity);
    void SetBloomShape(float threshold, float softKnee, float maxContribution);
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
    void SetLightingRigContract(std::string rigId, std::string source, bool safeVariantActive);
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
    [[nodiscard]] bool IsIBLLimitEnabled() const;
    [[nodiscard]] float GetIBLDiffuseIntensity() const;
    [[nodiscard]] float GetIBLSpecularIntensity() const;
    void SetIBLIntensity(float diffuseIntensity, float specularIntensity);
    void SetIBLEnabled(bool enabled);
    void SetBackgroundPresentation(bool visible, float exposure, float blur);
    void CycleEnvironmentPreset();
    void SetColorGrade(float warm, float cool);
    void SetCinematicPostEnabled(bool enabled);
    void SetCinematicPost(float vignette, float lensDirt);
    void SetSSAOEnabled(bool enabled);
    void SetSSAOParams(float radius, float bias, float intensity);
    void SetPCSS(bool enabled);
    void SetFXAAEnabled(bool enabled);
    void SetTAAEnabled(bool enabled);
    [[nodiscard]] bool IsTAAEnabled() const;
    void ToggleTAA();

    // Per-scene particle toggle so heavy layouts on 8 GB GPUs can turn off
    // billboard particles without affecting global renderer state.
    void SetParticlesEnabled(bool enabled);
    [[nodiscard]] bool GetParticlesEnabled() const;
    void SetParticleDensityScale(float scale);
    [[nodiscard]] float GetParticleDensityScale() const;
    void SetSSREnabled(bool enabled);
    void ToggleSSR();
    void CycleScreenSpaceEffectsDebug();
    void SetFogEnabled(bool enabled);
    void SetFogParams(float density, float height, float falloff);
    [[nodiscard]] bool IsFogEnabled() const;
    [[nodiscard]] float GetFogDensity() const;
    [[nodiscard]] float GetFogHeight() const;
    [[nodiscard]] float GetFogFalloff() const;
    [[nodiscard]] bool IsPCSS() const;
    [[nodiscard]] bool IsFXAAEnabled() const;
    [[nodiscard]] bool GetSSAOEnabled() const;
    [[nodiscard]] bool GetIBLEnabled() const;
    [[nodiscard]] bool GetSSREnabled() const;

    // Water / liquid controls
    void SetWaterParams(float levelY, float amplitude, float waveLength, float speed,
                        float dirX = 1.0f, float dirZ = 0.0f, float secondaryAmplitude = 0.0f,
                        float steepness = 0.6f);
    // Sample the procedural water height at a given world-space XZ position.
    // This mirrors the wave function used in Water.hlsl so buoyancy and
    // other CPU-side systems can stay in sync with the GPU water surface.
    float SampleWaterHeightAt(const glm::vec2& worldXZ) const;
    [[nodiscard]] float GetWaterLevel() const;
    [[nodiscard]] float GetWaterWaveAmplitude() const;
    [[nodiscard]] float GetWaterWaveLength() const;
    [[nodiscard]] float GetWaterWaveSpeed() const;
    [[nodiscard]] float GetWaterSecondaryAmplitude() const;
    [[nodiscard]] float GetWaterSteepness() const;
    [[nodiscard]] glm::vec2 GetWaterPrimaryDir() const;

    void SetGodRayIntensity(float intensity);
    [[nodiscard]] float GetGodRayIntensity() const;

    void SetAreaLightSizeScale(float scale);
    [[nodiscard]] float GetAreaLightSizeScale() const;

    [[nodiscard]] float GetRenderScale() const;
    void SetRenderScale(float scale);
    void SetActiveGraphicsPreset(const std::string& id, bool dirtyFromUI);
    [[nodiscard]] std::string GetActiveGraphicsPreset() const;
    [[nodiscard]] bool IsGraphicsPresetDirtyFromUI() const;

    // Optional RT feature toggles exposed to UI.
    void SetRTReflectionsEnabled(bool enabled);
    void SetRTGIEnabled(bool enabled);
    [[nodiscard]] bool GetRTReflectionsEnabled() const;
    [[nodiscard]] bool GetRTGIEnabled() const;

    using VRAMBreakdown = RendererVRAMBreakdown;
    using DescriptorStats = RendererDescriptorStats;
    using HealthState = RendererHealthState;
    using QualityState = RendererQualityState;
    using FeatureState = RendererFeatureState;
    using RayTracingState = RendererRayTracingState;
    using WaterState = RendererWaterState;
    using FractalSurfaceState = RendererFractalSurfaceState;
    using PostProcessState = RendererPostProcessState;
    using FogState = RendererFogState;
    using LightingState = RendererLightingState;

    [[nodiscard]] QualityState GetQualityState() const;
    [[nodiscard]] FeatureState GetFeatureState() const;
    [[nodiscard]] RayTracingState GetRayTracingState() const;
    [[nodiscard]] WaterState GetWaterState() const;
    [[nodiscard]] PostProcessState GetPostProcessState() const;

    // Estimated VRAM usage for the current frame. This walks renderer-owned
    // D3D12 resources plus asset-registry buckets so the UI reflects actual
    // residency pressure instead of a fixed-resolution guess.
    [[nodiscard]] VRAMBreakdown GetEstimatedVRAMBreakdown() const;
    [[nodiscard]] float GetEstimatedVRAMMB() const;
    [[nodiscard]] const AssetRegistry& GetAssetRegistry() const;
    [[nodiscard]] AssetRegistry::MemoryBreakdown GetAssetMemoryBreakdown() const;
    [[nodiscard]] const std::vector<TextureUploadReceipt>& GetTextureUploadReceipts() const;
    [[nodiscard]] const TextureUploadQueueStats& GetTextureUploadQueueStats() const;
    // Mesh key lookups for asset / BLAS management.
    [[nodiscard]] const std::unordered_map<const Scene::MeshData*, std::string>& GetMeshAssetKeys() const;

    // Lightweight CPU-side timings for major passes, in milliseconds.
    [[nodiscard]] float GetLastMainPassTimeMS() const;
    [[nodiscard]] float GetLastRTTimeMS() const;
    [[nodiscard]] float GetLastPostTimeMS() const;
    [[nodiscard]] float GetLastDepthPrepassTimeMS() const;
    [[nodiscard]] float GetLastShadowPassTimeMS() const;
    [[nodiscard]] float GetLastSSRTimeMS() const;
    [[nodiscard]] float GetLastSSAOTimeMS() const;
    [[nodiscard]] float GetLastBloomTimeMS() const;
    [[nodiscard]] DescriptorStats GetDescriptorStats() const;
    [[nodiscard]] HealthState BuildHealthState() const;
    [[nodiscard]] const Debug::GPUFrameProfile* GetLastGPUProfile() const;
    [[nodiscard]] const FrameContract& GetFrameContract() const;

    // Memory-budget preset intended for lower-end or memory-constrained GPUs.
    // Scales resolution and shadow residency down while preserving feature flags.
    void ApplySafeQualityPreset();

    // Ray tracing capability and toggle (DXR)
    [[nodiscard]] bool IsRayTracingSupported() const;
    [[nodiscard]] bool IsRayTracingEnabled() const;
    [[nodiscard]] bool IsDeviceRemoved() const;
    void SetRayTracingEnabled(bool enabled);

    // GPU-driven rendering (Phase 1 GPU culling + indirect draw)
    void SetGPUCullingEnabled(bool enabled);
    [[nodiscard]] bool IsGPUCullingEnabled() const;
    void SetGPUCullingFreeze(bool enabled);
    void ToggleGPUCullingFreeze();
    [[nodiscard]] bool IsGPUCullingFreezeEnabled() const;
    [[nodiscard]] bool IsIndirectDrawEnabled() const;
    [[nodiscard]] uint32_t GetGPUCulledCount() const;
    [[nodiscard]] uint32_t GetGPUTotalInstances() const;
    [[nodiscard]] GPUCullingPipeline::DebugStats GetGPUCullingDebugStats() const;

    // Biome materials buffer access (for VisibilityBuffer binding)
    [[nodiscard]] bool IsBiomeMaterialsValid() const;
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS GetBiomeMaterialsGPUAddress() const;

    // Experimental voxel backend toggle. When enabled, the main Render path
    // skips the traditional raster + RT pipeline and instead runs a
    // fullscreen voxel raymarch pass that visualizes the scene using a
    // grid-based voxel prototype. This is wired from EngineConfig so that
    // the launcher/CLI can select it at startup.
    // Only enable the voxel backend when both requested and the experimental
    // voxel pipeline was created successfully. This prevents configuration or
    // shader compile errors from leaving the renderer in a state where the
    // classic DX12 path is skipped but the voxel path has nothing to draw.
    void SetVoxelBackendEnabled(bool enabled);
    [[nodiscard]] bool IsVoxelBackendEnabled() const;
    // Mark the dense voxel grid as out of date so the next voxel render pass
    // rebuilds it from the current ECS scene. Called on scene rebuilds or
    // when large structural changes occur.
    void MarkVoxelGridDirty();
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
    [[nodiscard]] float GetSunIntensity() const;

    // Biome materials upload for terrain rendering
    void UpdateBiomeMaterialsBuffer(const std::vector<Scene::BiomeConfig>& configs);
    [[nodiscard]] bool AreBiomeMaterialsValid() const;

    // Vegetation rendering system
    void SetVegetationEnabled(bool enabled);
    [[nodiscard]] bool IsVegetationEnabled() const;
    void UpdateVegetationInstances(const std::vector<Scene::VegetationInstance>& instances,
                                   const std::vector<Scene::VegetationPrototype>& prototypes,
                                   const glm::vec3& cameraPos);
    void UpdateBillboardInstances(const std::vector<Scene::VegetationInstance>& instances,
                                  const std::vector<Scene::VegetationPrototype>& prototypes);
    void UpdateGrassInstances(const std::vector<Scene::VegetationInstance>& instances);
    void SetWindParams(const Scene::WindParams& params);
    [[nodiscard]] const Scene::WindParams& GetWindParams() const;
    [[nodiscard]] const Scene::VegetationStats& GetVegetationStats() const;
    Result<void> LoadVegetationAtlas(const std::string& path);
    void RenderVegetation(Scene::ECS_Registry* registry);
    void RenderVegetationShadows(Scene::ECS_Registry* registry);

    // GPU job queue introspection for incremental loading / diagnostics.
    [[nodiscard]] bool HasPendingGpuJobs() const;
    [[nodiscard]] uint32_t GetPendingMeshJobs() const;
    [[nodiscard]] uint32_t GetPendingBLASJobs() const;
    // Conservative signal that RT is still "warming up" its BLAS set.
    [[nodiscard]] bool IsRTWarmingUp() const;

    // Block until all outstanding GPU work on the main and upload queues has
    // completed. Used sparingly before large reallocations (e.g., when
    // resizing depth/HDR targets after a render-scale change) to avoid
    // transient allocation spikes on memory-constrained GPUs.
    void WaitForGPU();

    // Lighting rig safety toggle: when true, ApplyLightingRig selects a
    // lower-intensity / fewer-shadows variant on <=8 GB adapters.
    void SetUseSafeLightingRigOnLowVRAM(bool enabled);
    [[nodiscard]] bool GetUseSafeLightingRigOnLowVRAM() const;

    // Asset lifetime helpers used by the engine after scene rebuilds.
    void RebuildAssetRefsFromScene(Scene::ECS_Registry* registry);
    void PruneUnusedMeshes(Scene::ECS_Registry* registry);
    void PruneUnusedTextures();

    // === Engine Editor Mode: Selective Renderer Usage ===
    // These methods allow EngineEditorMode to control the render flow
    // instead of using the monolithic Render() method.

    // Frame management (call in order: BeginFrame -> ... -> EndFrame)
    void BeginFrameForEditor();
    void EndFrameForEditor();
    void PrepareMainPassForEditor();
    void UpdateFrameConstantsForEditor(float deltaTime, Scene::ECS_Registry* registry);

    // Individual render passes (call selectively as needed)
    void RenderSkyboxForEditor();
    void RenderShadowPassForEditor(Scene::ECS_Registry* registry);
    void RenderSceneForEditor(Scene::ECS_Registry* registry);
    void RenderSSAOForEditor();
    void RenderBloomForEditor();
    void RenderPostProcessForEditor();
    void RenderDebugLinesForEditor();
    void RenderTAAForEditor();
    void RenderSSRForEditor();
    void PrewarmMaterialDescriptorsForEditor(Scene::ECS_Registry* registry);

    // Query command list state
    [[nodiscard]] bool IsCommandListOpen() const;

private:
    static constexpr uint32_t kShadowCascadeCount = 3;
    // Total shadow-map array slices: cascades (sun) + local lights.
    static constexpr uint32_t kMaxShadowedLocalLights = 3;
    static constexpr uint32_t kShadowArraySize = kShadowCascadeCount + kMaxShadowedLocalLights;
    static constexpr uint32_t kBloomLevels = 3;
    static constexpr uint32_t kBloomDescriptorSlots =
        1u + (kBloomLevels - 1u) + (2u * kBloomLevels) + kBloomLevels;

    struct MainSceneEffectsResult {
        const char* frameNormalRoughnessResource = "gbuffer_normal_roughness";
        bool rgHasPendingHzb = false;
    };

    void BeginFrame();
    void ResetFrameExecutionState();
    [[nodiscard]] FrameExecutionContext BuildFrameExecutionContext(Scene::ECS_Registry* registry, float deltaTime);
    void RunPreFrameServices(const FrameExecutionContext& frameCtx);
    [[nodiscard]] bool BeginFrameExecution(FrameExecutionContext& frameCtx);
    [[nodiscard]] bool TryRenderSpecialFramePath(const FrameExecutionContext& frameCtx);
    void ExecuteRayTracingFramePhase(const FrameExecutionContext& frameCtx);
    void ExecuteShadowFramePhase(const FrameExecutionContext& frameCtx);
    void BeginMainSceneFramePhase(const FrameExecutionContext& frameCtx);
    void ExecuteGeometryFramePhase(const FrameExecutionContext& frameCtx);
    [[nodiscard]] MainSceneEffectsResult ExecuteMainSceneEffectsFramePhase(const FrameExecutionContext& frameCtx);
    void ExecutePostProcessingFramePhase(const FrameExecutionContext& frameCtx,
                                         const MainSceneEffectsResult& mainEffects);
    void CompleteFrameExecutionPhase(const FrameExecutionContext& frameCtx);
    void UpdateFrameContractSnapshot(Scene::ECS_Registry* registry,
                                     const FrameFeaturePlan& featurePlan);
    void UpdateFrameContractHistories();
    void ValidateFrameContract();
    void RecordFramePass(const char* name,
                        bool planned,
                        bool executed,
                        uint32_t drawCount,
                        std::initializer_list<const char*> reads,
                        std::initializer_list<const char*> writes,
                        bool fallbackUsed = false,
                        const char* fallbackReason = nullptr,
                        bool renderGraphOwned = false);
    [[nodiscard]] FrameContract::DescriptorUsage CaptureFrameDescriptorUsage() const;
    void InvalidateTAAHistory(const char* reason);
    void MarkTAAHistoryValid();
    void InvalidateRTShadowHistory(const char* reason);
    void MarkRTShadowHistoryValid();
    void InvalidateRTReflectionHistory(const char* reason);
    void MarkRTReflectionHistoryValid();
    void InvalidateRTGIHistory(const char* reason);
    void MarkRTGIHistoryValid();
    void RenderDepthPrepass(Scene::ECS_Registry* registry);
    void PrepareMainPass();
    void EndFrame();
    [[nodiscard]] UINT GetInternalRenderWidth() const;
    [[nodiscard]] UINT GetInternalRenderHeight() const;

    void UpdateFrameConstants(float deltaTime, Scene::ECS_Registry* registry);
    void PopulateFrameLightingAndShadows(FrameConstants& frameData,
                                         float deltaTime,
                                         Scene::ECS_Registry* registry,
                                         const FrameConstantCameraState& cameraState);
    void PopulateFrameDebugAndPostConstants(FrameConstants& frameData,
                                            Scene::ECS_Registry* registry,
                                            const FrameConstantCameraState& cameraState);
    void PublishFrameConstants(FrameConstants& frameData,
                               const FrameConstantCameraState& cameraState);
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    Result<void> EnsureHyperGeometryScene(Scene::ECS_Registry* registry);
#endif

    Result<void> CreateDepthBuffer();
    Result<void> CreateHZBResources();
    Result<void> CreateCommandList();
    Result<void> CompileShaders();
    Result<RendererCompiledShaders> CompileRendererPipelineShaders();
    Result<void> CreateRendererRootSignaturesAndComputePasses();
    Result<void> CreateGeometryPipelineStates(const RendererCompiledShaders& shaders);
    Result<void> CreateScreenSpacePipelineStates(const RendererCompiledShaders& shaders);
    Result<void> CreatePipeline();
    Result<void> CreatePlaceholderTexture();
    [[nodiscard]] std::string BuildTextureCacheKey(const std::string& path,
                                                   bool useSRGB,
                                                   AssetRegistry::TextureKind kind) const;
    [[nodiscard]] TextureUploadReceipt BuildTextureReceiptFromPlan(
        const TextureSourcePlan& plan,
        const std::string& key,
        bool useSRGB,
        AssetRegistry::TextureKind kind,
        uint64_t residentTextureBytesBefore) const;
    Result<DX12Texture> ExecuteTextureUpload(const TextureSourcePlan& plan);
    void RecordTextureUploadResult(const TextureSourcePlan& plan, double uploadMs, bool succeeded);
    Result<std::shared_ptr<DX12Texture>> PublishTexture(TextureUploadTicket ticket);
    void StoreTextureUploadReceipt(TextureUploadReceipt receipt);
    bool TryGetCachedTexture(const std::string& path,
                             bool useSRGB,
                             AssetRegistry::TextureKind kind,
                             std::shared_ptr<DX12Texture>& outTexture) const;
    [[nodiscard]] bool IsTextureUploadPending(const std::string& path,
                                              bool useSRGB,
                                              AssetRegistry::TextureKind kind) const;
    [[nodiscard]] bool IsTextureUploadFailed(const std::string& path,
                                             bool useSRGB,
                                             AssetRegistry::TextureKind kind) const;
    Result<void> QueueTextureUploadFromFile(const std::string& path,
                                            bool useSRGB,
                                            AssetRegistry::TextureKind kind);
    void ProcessTextureUploadJobsPerFrame(uint32_t maxJobs);
    Result<void> CreateShadowMapResources();
    void RecreateShadowMapResourcesForCurrentSize();
    Result<void> CreateHDRTarget();
    Result<void> CreateVisibilityBuffer();
    Result<void> CreateBloomResources();
    Result<void> CreateSSAOResources();
    Result<void> CreateTemporalRejectionMaskResources();
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
    struct RenderGraphPassResult {
        bool executed = false;
        bool fallbackUsed = false;
        std::string fallbackReason;
        uint32_t graphPasses = 0;
        uint32_t graphBarriers = 0;
    };
    void AccumulateRenderGraphExecutionStats(RenderGraphPassResult* result = nullptr);
    struct EndFrameGraphInputs {
        bool hzbPending = false;
        bool runBloom = false;
        bool runPostProcess = false;
        bool useRenderGraphHZB = false;
        bool useRenderGraphPost = false;
        const char* frameNormalRoughnessResource = "gbuffer_normal_roughness";
    };
    struct EndFrameGraphResult {
        bool attempted = false;
        bool attemptedBloom = false;
        bool ranBloom = false;
        bool ranPostProcess = false;
        bool ranHZB = false;
        bool fallbackUsed = false;
        std::string fallbackReason;
    };
    [[nodiscard]] RenderGraphPassResult ExecuteShadowPassInRenderGraph(Scene::ECS_Registry* registry);
    [[nodiscard]] RenderGraphPassResult ExecuteDepthPrepassInRenderGraph(Scene::ECS_Registry* registry);
    [[nodiscard]] RenderGraphPassResult ExecuteVisibilityBufferInRenderGraph(Scene::ECS_Registry* registry);
    [[nodiscard]] RenderGraphPassResult ExecuteMotionVectorsInRenderGraph();
    [[nodiscard]] RenderGraphPassResult ExecuteTemporalRejectionMaskInRenderGraph(const char* frameNormalRoughnessResource);
    [[nodiscard]] RenderGraphPassResult ExecuteTAAInRenderGraph();
    [[nodiscard]] RenderGraphPassResult ExecuteSSRInRenderGraph();
    [[nodiscard]] RenderGraphPassResult ExecuteSSAOInRenderGraph();
    [[nodiscard]] RenderGraphPassResult ExecuteBloomInRenderGraph();
    [[nodiscard]] EndFrameGraphResult ExecuteEndFrameInRenderGraph(const EndFrameGraphInputs& inputs);
    void RunRenderGraphTransientValidation();
    void RenderSkybox();
    void RenderScene(Scene::ECS_Registry* registry);
    void RenderOverlays(Scene::ECS_Registry* registry);
    void RenderWaterSurfaces(Scene::ECS_Registry* registry);
    void RenderTransparent(Scene::ECS_Registry* registry);
    void RenderSSR();
    void RenderTAA();
    [[nodiscard]] bool SeedTAAHistory(bool skipTransitions);
    [[nodiscard]] bool ResolveTAAIntermediate(bool skipTransitions);
    [[nodiscard]] bool CopyTAAIntermediateToHDR(bool skipTransitions);
    [[nodiscard]] bool CopyHDRToTAAHistory(bool skipTransitions);
    void RenderMotionVectors();
    void BuildTemporalRejectionMask(const char* frameNormalRoughnessResource,
                                    bool skipTransitions = false,
                                    bool renderGraphOwned = false);
    void CaptureTemporalRejectionMaskStats();
    void UpdateTemporalRejectionMaskStatsFromReadback();
    void CaptureRTReflectionSignalStats();
    void CaptureRTReflectionHistorySignalStats();
    void UpdateRTReflectionSignalStatsFromReadback();
    void BuildHZBFromDepth();
    void AddHZBFromDepthPasses_RG(RenderGraph& graph, RGResourceHandle depthHandle, RGResourceHandle hzbHandle);
    Result<void> InitializeTAAResolveDescriptorTable();
    void UpdateTAAResolveDescriptorTable();
    Result<void> InitializePostProcessDescriptorTable();
    void UpdatePostProcessDescriptorTable();
    void RenderSSAO();
    void RenderSSAOAsync();  // Async compute version
    void RenderBloom();
    [[nodiscard]] bool PrepareBloomPassState();
    [[nodiscard]] bool BindBloomPassSRV(DescriptorHandle source, const char* label, uint32_t tableSlot);
    [[nodiscard]] bool BindBloomPassTexture(ID3D12Resource* source, DXGI_FORMAT format, const char* label, uint32_t tableSlot);
    [[nodiscard]] bool RenderBloomDownsampleBase(bool skipTransitions);
    [[nodiscard]] bool RenderBloomDownsampleLevel(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool RenderBloomBlurHorizontal(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool RenderBloomBlurVertical(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool RenderBloomComposite(bool skipTransitions);
    [[nodiscard]] bool CopyBloomCompositeToCombined(bool skipTransitions);
    void RenderPostProcess();
    void RenderDebugLines();
    void ProcessGpuJobsPerFrame();
    void RenderVoxel(Scene::ECS_Registry* registry);
    Result<void> BuildVoxelGridFromScene(Scene::ECS_Registry* registry);
    Result<void> UploadVoxelGridToGPU();
    void RenderRayTracing(Scene::ECS_Registry* registry);
    void RenderRayTracedReflections();
    void UpdateRTFramePlan(const FrameFeaturePlan& featurePlan);
    void ExecuteRTDenoisePass(const char* frameNormalRoughnessResource);
    void RenderParticles(Scene::ECS_Registry* registry);

    // Vegetation rendering helpers
    Result<void> CreateVegetationPipelines();
    Result<void> CreateVegetationInstanceBuffer(UINT capacity);
    Result<void> CreateBillboardInstanceBuffer(UINT capacity);
    Result<void> CreateGrassInstanceBuffer(UINT capacity);
    void UpdateVegetationConstantBuffer(const glm::mat4& viewProj, const glm::mat4& view,
                                        const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                                        const glm::vec3& cameraUp);
    void RenderVegetationMeshes();
    void RenderVegetationBillboards();
    void RenderGrassCards();

    // GPU-driven rendering (Phase 1)
    void CollectInstancesForGPUCulling(Scene::ECS_Registry* registry);
    void DispatchGPUCulling();
    void RenderSceneIndirect(Scene::ECS_Registry* registry);

    // Visibility buffer rendering (Phase 2.1)
    void CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry);
    void RenderVisibilityBufferPath(Scene::ECS_Registry* registry);
    [[nodiscard]] uint32_t GetVisibilityBufferDebugView() const;
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS ResolveVisibilityBufferCullMask(uint32_t debugView);
    void LogVisibilityBufferFirstFrame();
    [[nodiscard]] bool RenderVisibilityBufferVisibilityStage(D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress,
                                                             uint32_t debugView,
                                                             bool& completedPath);
    [[nodiscard]] bool RenderVisibilityBufferMaterialResolveStage(uint32_t debugView,
                                                                  bool& completedPath);
    struct VisibilityBufferDeferredLightingInputs {
        std::vector<Light> localLights;
        std::vector<VBReflectionProbe> reflectionProbes;
        uint32_t skippedReflectionProbes = 0;
        VisibilityBufferRenderer::DeferredLightingParams params{};
        ID3D12Resource* envDiffuseResource = nullptr;
        ID3D12Resource* envSpecularResource = nullptr;
        DXGI_FORMAT envFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    };
    [[nodiscard]] VisibilityBufferDeferredLightingInputs
    PrepareVisibilityBufferDeferredLighting(Scene::ECS_Registry* registry);
    void ApplyVisibilityBufferDeferredLighting(const VisibilityBufferDeferredLightingInputs& inputs);
    void RenderVisibilityBufferDeferredLightingStage(Scene::ECS_Registry* registry);

    // Debug drawing API
public:
    void AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void ClearDebugLines();

    // GPU-driven debug overlay (settings panel) state fed into FrameConstants.
    void SetDebugOverlayState(bool visible, int selectedSection) {
        m_debugOverlayState.visible = visible;
        m_debugOverlayState.selectedRow = selectedSection;
    }

    // Flag used by the GPU post-process path to draw a simple settings
    // overlay instead of relying on GDI, which is unreliable with modern
    // flip-model swap chains.
    void SetDebugOverlayVisible(bool visible) { m_debugOverlayState.visible = visible; }

    // Graphics resources
    RendererServiceState m_services;

    RTDenoisePassState m_rtDenoiseState;
    RTReflectionSignalStatsState m_rtReflectionSignalState;
    RTShadowTargetState m_rtShadowTargets;
    RTReflectionTargetState m_rtReflectionTargets;
    RTGITargetState m_rtGITargets;
    RTReflectionReadinessState m_rtReflectionReadiness;

    RendererCommandResourceState m_commandResources;

    RendererPipelineState m_pipelineState;

    ParticleRenderState m_particleState;

    RendererDebugOverlayState m_debugOverlayState;

    RendererConstantBufferState m_constantBuffers;

    UploadCommandPoolState m_uploadCommands;

    // Manual GPU breadcrumbs: a tiny readback buffer written via
    // WriteBufferImmediate so that device-removed errors can report which
    // high-level GPU marker was last executed before the fault.
    Result<void> CreateBreadcrumbBuffer();
    void WriteBreadcrumb(GpuMarker marker);

    RendererBreadcrumbState m_breadcrumbs;

    RendererAssetRuntimeState m_assetRuntime;

    DepthTargetState m_depthResources;
    HZBPassState m_hzbResources;

    ShadowMapPassState<kShadowArraySize, kShadowCascadeCount> m_shadowResources;
    EnvironmentLightingState m_environmentState;
    MainRenderTargetState m_mainTargets;

    SSAOPassState m_ssaoResources;

    SSRPassState m_ssrResources;

    TemporalScreenPassState m_temporalScreenState;
    TemporalMaskPassState m_temporalMaskState;


    BloomPassState<kBloomLevels, kBloomDescriptorSlots> m_bloomResources;

    MaterialFallbackTextureState m_materialFallbacks;

    DebugLineRenderState m_debugLineState;
    RendererDebugViewState m_debugViewState;

    RendererLightingState m_lightingState;
    RendererQualityRuntimeState m_qualityRuntimeState;
    // Temporal anti-aliasing (camera-only) state
    TemporalAAState m_temporalAAState;
    // Cached camera parameters and history used by culling, RT, and temporal passes.
    RendererCameraFrameState m_cameraState;
    RendererLocalShadowState<kMaxShadowedLocalLights> m_localShadowState;
    ShadowCascadeFrameState<kShadowCascadeCount> m_shadowCascadeState;

    RTRuntimeState m_rtRuntimeState;
    GpuCullingRuntimeState m_gpuCullingState;
    RendererVisibilityBufferState m_visibilityBufferState;
    RendererFrameLifecycleState m_frameLifecycle;
    RendererFramePlanningState m_framePlanning;
    RendererTemporalHistoryState m_temporalHistory;
    RendererFrameDiagnosticsState m_frameDiagnostics;

    VoxelRenderState m_voxelState;

    // Global fractal surface parameters (applied uniformly to all materials).
    RendererFractalSurfaceState m_fractalSurfaceState;

    // Simple feature toggles and grading controls consumed by the post shader.
    RendererPostProcessState m_postProcessState;

    // Internal helpers for diagnostics and error reporting.
    void MarkPassComplete(const char* passName);
    void ReportDeviceRemoved(const char* context, HRESULT hr, const char* file, int line);

    // Exponential height fog parameters.
    RendererFogState m_fogState;

    // Water / liquid parameters shared with shaders via waterParams0/1.
    // Defaults describe a calm, low-amplitude water plane at Y=0.
    RendererWaterState m_waterState;

    // Vegetation rendering system
    VegetationRenderState m_vegetationState;

    // Frame state
    RendererFrameRuntimeState m_frameRuntime;

};

} // namespace Cortex::Graphics
