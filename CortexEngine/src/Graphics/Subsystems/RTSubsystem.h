#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>

#include "Graphics/RendererRTState.h"          // RT*State structs (+ kFrameCount, DescriptorHandle, Result)
#include "Graphics/RendererEnvironmentState.h" // EnvironmentLightingState, EnvironmentMaps
#include "Graphics/RendererFramePlanningState.h"
#include "Graphics/RendererAssetRuntimeState.h"
#include "Graphics/FrameFeaturePlan.h"

namespace Cortex {
class Window;
namespace Scene { class ECS_Registry; }

namespace Graphics {

class DX12Device;
class DX12RaytracingContext;
class RTDenoiser;
class RTReflectionSignalStats;
class VisibilityBufferRenderer;
class DescriptorHeapManager;
class TemporalManager;

// Per-frame dependencies for the ray-tracing family (shadows, reflections, GI,
// denoise, signal stats). The subsystem owns the RT target/runtime/denoise/
// reflection-stats state; everything below is injected by the Renderer each
// frame. The shared temporal-history manager and the visibility buffer are
// borrowed; frame-planning + asset-runtime state and the reflection-written
// flag are written back through pointers; environment table refresh and the
// frame-pass diagnostics recorder are passed as service callbacks.
struct RTContext {
    // Core devices / services.
    DX12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RaytracingContext* rayTracingContext = nullptr;
    RTDenoiser* rtDenoiser = nullptr;
    RTReflectionSignalStats* rtReflectionSignalStats = nullptr;
    VisibilityBufferRenderer* visibilityBuffer = nullptr;
    Window* window = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    // Frame scalars.
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;
    uint32_t absoluteFrameIndex = 0;
    uint64_t renderFrameCounter = 0;
    uint32_t internalRenderWidth = 0;
    uint32_t internalRenderHeight = 0;

    // Depth.
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthSrv{};

    // Main targets.
    ID3D12Resource* hdrColor = nullptr; // for RT resource sizing
    ID3D12Resource* normalRoughness = nullptr;
    D3D12_RESOURCE_STATES* normalRoughnessState = nullptr;
    DescriptorHandle normalRoughnessSrv{};

    // Temporal rejection mask (owned by Renderer).
    ID3D12Resource* maskTexture = nullptr;
    D3D12_RESOURCE_STATES* maskState = nullptr;
    DescriptorHandle maskSrv{};
    bool maskBuiltThisFrame = false;

    // Velocity (owned by the temporal subsystem's screen state).
    ID3D12Resource* velocityBuffer = nullptr;
    D3D12_RESOURCE_STATES* velocityState = nullptr;
    DescriptorHandle velocitySrv{};

    // Environment lighting state (read live; refreshed via updateEnvironmentTable).
    EnvironmentLightingState* environment = nullptr;

    // Visibility buffer status.
    bool visibilityBufferRenderedThisFrame = false;

    // Debug view selector.
    uint32_t debugViewMode = 0;

    // Shared temporal-history manager (also used by TAA).
    TemporalManager* historyManager = nullptr;

    // Mutable back-references owned by the Renderer.
    RendererFramePlanningState* framePlanning = nullptr;
    RendererAssetRuntimeState* assetRuntime = nullptr;
    bool* rtReflectionWrittenThisFrame = nullptr;

    // Service callbacks.
    std::function<void()> updateEnvironmentTable;
    std::function<void(const char* name,
                       bool planned,
                       bool executed,
                       uint32_t drawCount,
                       std::initializer_list<const char*> reads,
                       std::initializer_list<const char*> writes,
                       bool fallbackUsed,
                       const char* fallbackReason)>
        recordFramePass;
};

// Owns ray-tracing target/runtime/denoise/reflection-stats state and runs the
// RT shadow/GI/reflection dispatch, denoise, and reflection signal-stats passes.
class RTSubsystem {
public:
    void RenderRayTracing(Scene::ECS_Registry* registry, const RTContext& ctx);
    void RenderRayTracedReflections(const RTContext& ctx);
    void ExecuteRTDenoisePass(const char* frameNormalRoughnessResource, const RTContext& ctx);
    void UpdateRTFramePlan(const FrameFeaturePlan& featurePlan, const RTContext& ctx);

    [[nodiscard]] Result<void> CreateRTShadowMask(const RTContext& ctx);
    [[nodiscard]] Result<void> CreateRTGIResources(const RTContext& ctx);
    [[nodiscard]] Result<void> CreateRTReflectionResources(const RTContext& ctx);

    void CaptureRTReflectionSignalStats(const RTContext& ctx);
    void CaptureRTReflectionHistorySignalStats(const RTContext& ctx);
    void UpdateRTReflectionSignalStatsFromReadback(const RTContext& ctx);

    void ClearBLASCache(const RTContext& ctx);

    [[nodiscard]] bool IsRTWarmingUp(DX12RaytracingContext* rayTracingContext,
                                     uint32_t pendingRendererBLASJobs) const;

    void InvalidateRTShadowHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason);
    void MarkRTShadowHistoryValid(TemporalManager& mgr, uint64_t frameCounter);
    void InvalidateRTReflectionHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason);
    void MarkRTReflectionHistoryValid(TemporalManager& mgr, uint64_t frameCounter);
    void InvalidateRTGIHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason);
    void MarkRTGIHistoryValid(TemporalManager& mgr, uint64_t frameCounter);

    [[nodiscard]] RTRuntimeState& RuntimeState() { return m_runtime; }
    [[nodiscard]] const RTRuntimeState& RuntimeState() const { return m_runtime; }
    [[nodiscard]] RTDenoisePassState& DenoiseState() { return m_denoise; }
    [[nodiscard]] const RTDenoisePassState& DenoiseState() const { return m_denoise; }
    [[nodiscard]] RTReflectionSignalStatsState& ReflectionSignalState() { return m_reflectionSignal; }
    [[nodiscard]] const RTReflectionSignalStatsState& ReflectionSignalState() const { return m_reflectionSignal; }
    [[nodiscard]] RTShadowTargetState& ShadowTargets() { return m_shadowTargets; }
    [[nodiscard]] const RTShadowTargetState& ShadowTargets() const { return m_shadowTargets; }
    [[nodiscard]] RTReflectionTargetState& ReflectionTargets() { return m_reflectionTargets; }
    [[nodiscard]] const RTReflectionTargetState& ReflectionTargets() const { return m_reflectionTargets; }
    [[nodiscard]] RTGITargetState& GITargets() { return m_giTargets; }
    [[nodiscard]] const RTGITargetState& GITargets() const { return m_giTargets; }
    [[nodiscard]] RTReflectionReadinessState& ReflectionReadiness() { return m_reflectionReadiness; }
    [[nodiscard]] const RTReflectionReadinessState& ReflectionReadiness() const { return m_reflectionReadiness; }

private:
    RTRuntimeState m_runtime;
    RTDenoisePassState m_denoise;
    RTReflectionSignalStatsState m_reflectionSignal;
    RTShadowTargetState m_shadowTargets;
    RTReflectionTargetState m_reflectionTargets;
    RTGITargetState m_giTargets;
    RTReflectionReadinessState m_reflectionReadiness;
};

} // namespace Graphics
} // namespace Cortex
