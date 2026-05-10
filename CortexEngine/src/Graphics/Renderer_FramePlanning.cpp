#include "Renderer.h"
#include "Debug/GPUProfiler.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::ResetFrameExecutionState() {
    // Monotonic frame counter for diagnostics; independent of swap-chain
    // buffer index so logs can be correlated easily.
    ++m_frameLifecycle.renderFrameCounter;
    MarkPassComplete("Render_Entry");
    m_visibilityBufferState.ResetFrameFlags();
    m_gpuCullingState.hzbOcclusionUsedThisFrame = false;
    m_frameDiagnostics.contract.drawCounts = {};
    m_frameDiagnostics.contract.motionVectors = {};
    m_frameDiagnostics.contract.motionVectors.previousTransformHistoryReset = m_gpuCullingState.previousTransformHistoryResetPending;
    m_gpuCullingState.previousTransformHistoryResetPending = false;
    m_frameDiagnostics.contract.passRecords.clear();
    m_frameDiagnostics.contract.lastPassDescriptorUsage = {};
    m_frameDiagnostics.renderGraph.info = {};
}

FrameExecutionContext Renderer::BuildFrameExecutionContext(Scene::ECS_Registry* registry, float deltaTime) {
    m_frameRuntime.totalTime += deltaTime;

    FrameExecutionContext frameCtx{};
    frameCtx.registry = registry;
    frameCtx.deltaTime = deltaTime;
    frameCtx.frameNumber = m_frameLifecycle.renderFrameCounter;
    frameCtx.frameIndex = m_frameRuntime.frameIndex;

    const RendererPipelineReadiness pipelineReadiness = m_pipelineState.GetReadiness();

    FrameFeaturePlanInputs featureInputs{};
    featureInputs.debug = LoadRuntimeFrameDebugSwitches();
    featureInputs.rayTracingSupported = m_rtRuntimeState.supported;
    featureInputs.rayTracingEnabled = m_rtRuntimeState.enabled;
    featureInputs.rtReflectionsEnabled = m_rtRuntimeState.reflectionsEnabled;
    featureInputs.rtGIEnabled = m_rtRuntimeState.giEnabled;
    featureInputs.shadowsEnabled = m_shadowResources.enabled;
    featureInputs.gpuCullingEnabled = m_gpuCullingState.enabled;
    featureInputs.visibilityBufferEnabled = m_visibilityBufferState.enabled;
    featureInputs.taaEnabled = m_temporalAAState.enabled;
    featureInputs.ssrEnabled = m_ssrResources.enabled;
    featureInputs.ssaoEnabled = m_ssaoResources.enabled;
    featureInputs.bloomEnabled = (m_bloomResources.intensity > 0.0f);
    featureInputs.fxaaEnabled = m_postProcessState.fxaaEnabled;
    featureInputs.iblEnabled = m_environmentState.enabled;
    featureInputs.fogEnabled = m_fogState.enabled;
    featureInputs.voxelBackendEnabled = m_voxelState.backendEnabled;
    featureInputs.hasRayTracingContext = (m_services.rayTracingContext != nullptr);
    featureInputs.hasRTReflectionColor = (m_rtReflectionTargets.color != nullptr);
    featureInputs.hasRTGIColor = (m_rtGITargets.color != nullptr);
    featureInputs.hasShadowMap = (m_shadowResources.map != nullptr);
    featureInputs.hasShadowPipeline = pipelineReadiness.shadow;
    featureInputs.hasGPUCulling = (m_services.gpuCulling != nullptr);
    featureInputs.hasVisibilityBuffer = (m_services.visibilityBuffer != nullptr);
    featureInputs.hasTAAPipeline = pipelineReadiness.taa;
    featureInputs.hasHistoryColor = (m_temporalScreenState.historyColor != nullptr);
    featureInputs.hasTAAIntermediate = (m_temporalScreenState.taaIntermediate != nullptr);
    featureInputs.hasSSRPipeline = pipelineReadiness.ssr;
    featureInputs.hasSSRColor = (m_ssrResources.color != nullptr);
    featureInputs.hasHDRColor = (m_mainTargets.hdrColor != nullptr);
    featureInputs.hasSSAOTarget = (m_ssaoResources.texture != nullptr);
    featureInputs.hasSSAOComputePipeline = pipelineReadiness.ssaoCompute;
    featureInputs.hasSSAOPipeline = pipelineReadiness.ssao;
    featureInputs.hasBloomBase = (m_bloomResources.texA[0] != nullptr);
    featureInputs.hasBloomDownsamplePipeline = pipelineReadiness.bloomDownsample;
    featureInputs.hasVoxelPipeline = pipelineReadiness.voxel;
    featureInputs.hasMotionVectorsPipeline = pipelineReadiness.motionVectors;
    featureInputs.hasVelocityBuffer = (m_temporalScreenState.velocityBuffer != nullptr);
    featureInputs.hasDepthBuffer = (m_depthResources.buffer != nullptr);
    featureInputs.hasPostProcessPipeline = pipelineReadiness.postProcess;
    featureInputs.particlesEnabledForScene = m_particleState.enabledForScene;

    frameCtx.features = BuildFrameFeaturePlan(featureInputs);
    return frameCtx;
}

void Renderer::RunPreFrameServices(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;

    // Optional DXGI video memory diagnostics. When CORTEX_LOG_VRAM is set,
    // log current GPU memory usage and budget periodically so device-removed
    // faults under HDR/post-process load can be correlated with VRAM
    // pressure on the user's adapter.
    bool logVramUsage = featurePlan.debug.logVRAM;
    if (logVramUsage && m_services.device) {
        constexpr uint64_t kLogIntervalFrames = 60;
        if ((m_frameLifecycle.renderFrameCounter % kLogIntervalFrames) == 0) {
            auto vramResult = m_services.device->QueryVideoMemoryInfo();
            if (vramResult.IsOk()) {
                const DX12Device::VideoMemoryInfo& info = vramResult.Value();
                const double usageMB = static_cast<double>(info.currentUsageBytes) / (1024.0 * 1024.0);
                const double budgetMB = static_cast<double>(info.budgetBytes) / (1024.0 * 1024.0);
                const double availMB = static_cast<double>(info.availableForReservationBytes) / (1024.0 * 1024.0);
                spdlog::info("VRAM: usage={:.1f} MB, budget={:.1f} MB, availableForReservation={:.1f} MB",
                             usageMB, budgetMB, availMB);
            } else {
                spdlog::warn("Renderer: QueryVideoMemoryInfo failed (disabling CORTEX_LOG_VRAM): {}",
                             vramResult.Error());
                logVramUsage = false;
            }
        }
    }

    // Environment loading obeys the selected budget profile. Low-memory
    // profiles keep pending environments deferred once the residency cap is
    // full; high profiles may stream a small number per frame.
    uint32_t maxEnvLoadsPerFrame = 0;
    const uint32_t residentLimit = std::max(1u, m_framePlanning.budgetPlan.iblResidentEnvironmentLimit);
    if (m_environmentState.maps.size() < residentLimit) {
        maxEnvLoadsPerFrame = std::min<uint32_t>(
            2u,
            residentLimit - static_cast<uint32_t>(m_environmentState.maps.size()));
    }
    ProcessPendingEnvironmentMaps(maxEnvLoadsPerFrame);
    ProcessTextureUploadJobsPerFrame(m_assetRuntime.textureUploads.queue.maxJobsPerFrame);

    // Process a limited number of heavy GPU jobs (mesh uploads / BLAS builds)
    // per frame so scene rebuilds and RT warm-up do not spike the first frame.
    ProcessGpuJobsPerFrame();
    MarkPassComplete("Render_BeforeBeginFrame");
}

bool Renderer::BeginFrameExecution(FrameExecutionContext& frameCtx) {
    FrameFeaturePlan& featurePlan = frameCtx.features;

    // Common frame setup (depth/HDR resize, command list reset, constant
    // buffer updates) shared by both the classic raster/RT backend and the
    // experimental voxel renderer.
    BeginFrame();
    WriteBreadcrumb(GpuMarker::BeginFrame);
    if (m_frameLifecycle.deviceRemoved) {
        // A fatal error occurred while preparing frame resources (for example,
        // depth/HDR creation failed due to device removal). Skip the rest of
        // this frame; the next call will early-out at the top.
        MarkPassComplete("BeginFrame_DeviceRemoved");
        return false;
    }

    // VB instance/mesh lists are rebuilt only when the VB path is taken; clear
    // them every frame so downstream passes (motion vectors) don't see stale data.
    m_visibilityBufferState.ClearDrawInputs();
    m_framePlanning.sceneSnapshot = BuildRendererSceneSnapshot(frameCtx.registry, m_frameLifecycle.renderFrameCounter);
    UpdateRTFramePlan(featurePlan);
    MarkPassComplete("BeginFrame_Done");

    Debug::GPUProfiler::Get().BeginFrame(m_commandResources.graphicsList.Get());
    Debug::GPUProfiler::Get().BeginScope(m_commandResources.graphicsList.Get(), "Frame", "Frame");
    UpdateFrameContractSnapshot(frameCtx.registry, featurePlan);

    // Warm per-material descriptor tables after BeginFrame() has waited for the
    // current frame's transient descriptor segment to become available. This is
    // required for correctness when descriptors are sourced from per-frame
    // transient ranges (avoids overwriting in-flight heap entries).
    PrewarmMaterialDescriptors(frameCtx.registry);

    UpdateFrameConstants(frameCtx.deltaTime, frameCtx.registry);
    MarkPassComplete("UpdateFrameConstants_Done");
    m_frameDiagnostics.contract.lastPassDescriptorUsage = CaptureFrameDescriptorUsage();
    RunRenderGraphTransientValidation();
    return true;
}

bool Renderer::TryRenderSpecialFramePath(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;

    // Optional ultra-minimal debug frame: clear the current back buffer and
    // present, skipping all geometry, lighting, and post-process work. This is
    // controlled via an environment variable so normal builds render the full
    // scene by default.
    if (featurePlan.runMinimalFrame) {
        ID3D12Resource* backBuffer = m_services.window ? m_services.window->GetCurrentBackBuffer() : nullptr;
        if (backBuffer) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_services.window->GetCurrentRTV();

            D3D12_RESOURCE_BARRIER bbBarrier{};
            bbBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bbBarrier.Transition.pResource   = backBuffer;
            bbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            bbBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bbBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &bbBarrier);
            m_frameLifecycle.backBufferUsedAsRTThisFrame = true;

            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_commandResources.graphicsList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            m_commandResources.graphicsList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        }

        MarkPassComplete("MinimalFrame_Done");
        EndFrame();
        return true;
    }

    // Experimental voxel backend: replace the traditional raster + RT path
    // with a fullscreen voxel raymarch pass while reusing the same frame
    // lifecycle and diagnostics contract as the classic renderer.
    if (featurePlan.runVoxelBackend) {
        RenderVoxel(frameCtx.registry);
        MarkPassComplete("RenderVoxel_Done");
        EndFrame();
        return true;
    }

    return false;
}

} // namespace Cortex::Graphics
