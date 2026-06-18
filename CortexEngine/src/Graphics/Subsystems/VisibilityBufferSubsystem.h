#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "Graphics/RendererVisibilityBufferState.h" // RendererVisibilityBufferState (owned)
#include "Graphics/VisibilityBuffer.h"              // VisibilityBufferRenderer, VBReflectionProbe, DeferredLightingParams
#include "Graphics/ShaderTypes.h"                   // Light
#include "Utils/Result.h"

namespace Cortex {
class Window;
namespace Scene {
class ECS_Registry;
struct MeshData;
struct RenderableComponent;
} // namespace Scene

namespace Graphics {

class DX12Device;
class DescriptorHeapManager;
class GPUCullingPipeline;
class HZBSubsystem;
class ShadowSubsystem;
struct GpuCullingRuntimeState;
struct RendererFrameDiagnosticsState;
struct RendererFramePlanningState;
struct DepthTargetState;
struct MainRenderTargetState;
struct RendererConstantBufferState;
struct RendererLightingState;
struct EnvironmentLightingState;
struct EnvironmentMaps;
struct MaterialFallbackTextureState;

// Per-frame dependencies for the visibility-buffer path (instance collection,
// GPU culling, visibility + material-resolve stages, deferred lighting). The
// subsystem owns the visibility-buffer CPU-side state; the GPU renderer, GPU
// culling pipeline, and the Renderer sub-states it coordinates are injected by
// pointer (mutated where it transitions resources or allocates culling IDs).
// Material preparation, mesh upload, environment SRV setup, and the V3 payload
// param builders are passed as Renderer service callbacks.
struct VisibilityBufferContext {
    DX12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    Window* window = nullptr;
    VisibilityBufferRenderer* visibilityBuffer = nullptr;
    GPUCullingPipeline* gpuCulling = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;

    uint32_t frameIndex = 0;
    uint64_t renderFrameCounter = 0;
    uint32_t debugViewMode = 0;
    uint32_t internalRenderWidth = 0;
    uint32_t internalRenderHeight = 0;

    GpuCullingRuntimeState* gpuCullingState = nullptr;
    RendererFrameDiagnosticsState* frameDiagnostics = nullptr;
    const RendererFramePlanningState* framePlanning = nullptr;
    DepthTargetState* depthResources = nullptr;
    MainRenderTargetState* mainTargets = nullptr;
    const RendererConstantBufferState* constantBuffers = nullptr;
    const RendererLightingState* lightingState = nullptr;
    EnvironmentLightingState* environment = nullptr;
    const MaterialFallbackTextureState* materialFallbacks = nullptr;
    HZBSubsystem* hzb = nullptr;
    ShadowSubsystem* shadows = nullptr;
    ID3D12Resource* rtGIResource = nullptr;

    std::function<void(Scene::RenderableComponent&)> prepareMaterialResources;
    std::function<Result<void>(const std::shared_ptr<Scene::MeshData>&, const char*)> enqueueMeshUpload;
    std::function<void(EnvironmentMaps&)> ensureEnvironmentBindlessSRVs;
    std::function<glm::vec4()> buildSceneLocalEnvironmentV3PayloadParams;
    std::function<glm::vec4()> buildCinematicStabilityParams;
};

// Owns the visibility-buffer CPU state and drives the VB collect / cull /
// visibility / material-resolve / deferred-lighting passes.
class VisibilityBufferSubsystem {
public:
    struct DeferredLightingInputs {
        std::vector<Light> localLights;
        std::vector<VBReflectionProbe> reflectionProbes;
        uint32_t skippedReflectionProbes = 0;
        VisibilityBufferRenderer::DeferredLightingParams params{};
        ID3D12Resource* envDiffuseResource = nullptr;
        ID3D12Resource* envSpecularResource = nullptr;
        ID3D12Resource* rtGIResource = nullptr;
        DXGI_FORMAT envFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    };

    void CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry, const VisibilityBufferContext& ctx);
    void RenderVisibilityBufferPath(Scene::ECS_Registry* registry, const VisibilityBufferContext& ctx);

    [[nodiscard]] uint32_t GetVisibilityBufferDebugView(uint32_t rendererDebugMode) const;
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS ResolveVisibilityBufferCullMask(uint32_t debugView,
                                                                            const VisibilityBufferContext& ctx);

    [[nodiscard]] bool RenderVisibilityBufferVisibilityStage(D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress,
                                                             uint32_t debugView,
                                                             bool& completedPath,
                                                             const VisibilityBufferContext& ctx);
    [[nodiscard]] bool RenderVisibilityBufferMaterialResolveStage(uint32_t debugView,
                                                                 bool& completedPath,
                                                                 const VisibilityBufferContext& ctx);

    [[nodiscard]] DeferredLightingInputs PrepareVisibilityBufferDeferredLighting(Scene::ECS_Registry* registry,
                                                                                 const VisibilityBufferContext& ctx);
    void ApplyVisibilityBufferDeferredLighting(const DeferredLightingInputs& inputs, const VisibilityBufferContext& ctx);
    void RenderVisibilityBufferDeferredLightingStage(Scene::ECS_Registry* registry, const VisibilityBufferContext& ctx);

    void LogVisibilityBufferFirstFrame() const;

    [[nodiscard]] RendererVisibilityBufferState& State() { return m_state; }
    [[nodiscard]] const RendererVisibilityBufferState& State() const { return m_state; }

private:
    RendererVisibilityBufferState m_state;
};

} // namespace Graphics
} // namespace Cortex
