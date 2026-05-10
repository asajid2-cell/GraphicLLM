#include "FrameFeaturePlan.h"

#include <cstdlib>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

RuntimeFrameDebugSwitches LoadRuntimeFrameDebugSwitches() {
    static const RuntimeFrameDebugSwitches switches = [] {
        RuntimeFrameDebugSwitches result{};
        result.forceEnableFeatures = (std::getenv("CORTEX_FORCE_ENABLE_FEATURES") != nullptr);
        if (!result.forceEnableFeatures) {
            result.disableSSR = (std::getenv("CORTEX_DISABLE_SSR") != nullptr);
            result.disableSSAO = (std::getenv("CORTEX_DISABLE_SSAO") != nullptr);
            result.disableBloom = (std::getenv("CORTEX_DISABLE_BLOOM") != nullptr);
            result.disableTAA = (std::getenv("CORTEX_DISABLE_TAA") != nullptr);
        } else {
            spdlog::warn("Renderer: CORTEX_FORCE_ENABLE_FEATURES set; env disables ignored (SSR/SSAO/Bloom/TAA)");
        }

        result.logVRAM = (std::getenv("CORTEX_LOG_VRAM") != nullptr);
        result.forceMinimalFrame = (std::getenv("CORTEX_FORCE_MINIMAL_FRAME") != nullptr);
        result.useRenderGraphShadows = (std::getenv("CORTEX_DISABLE_RG_SHADOWS") == nullptr);
        result.enableHZB = (std::getenv("CORTEX_DISABLE_HZB") == nullptr);
        result.useRenderGraphHZB = (std::getenv("CORTEX_DISABLE_RG_HZB") == nullptr);
        result.disablePostProcess = (std::getenv("CORTEX_DISABLE_POST_PROCESS") != nullptr);
        result.useRenderGraphPost = (std::getenv("CORTEX_DISABLE_RG_POST") == nullptr);

        if (result.disableSSR || result.disableSSAO || result.disableBloom || result.disableTAA) {
            spdlog::info("Renderer: env disables active (SSR={} SSAO={} Bloom={} TAA={})",
                         result.disableSSR ? "off" : "on",
                         result.disableSSAO ? "off" : "on",
                         result.disableBloom ? "off" : "on",
                         result.disableTAA ? "off" : "on");
        }
        if (result.logVRAM) {
            spdlog::info("Renderer: CORTEX_LOG_VRAM set; logging DXGI video memory usage periodically");
        }
        if (result.forceMinimalFrame) {
            spdlog::warn("Renderer: CORTEX_FORCE_MINIMAL_FRAME set; running ultra-minimal clear-only frame path");
        }
        spdlog::info("Shadow pass: RenderGraph transitions {}",
                     result.useRenderGraphShadows ? "enabled (default)" :
                                                    "disabled (CORTEX_DISABLE_RG_SHADOWS=1)");
        if (result.enableHZB) {
            spdlog::info("HZB enabled (RenderGraph builder: {})", result.useRenderGraphHZB ? "yes" : "no");
        } else {
            spdlog::info("HZB disabled via CORTEX_DISABLE_HZB=1");
        }
        if (result.disablePostProcess) {
            spdlog::warn("Renderer: CORTEX_DISABLE_POST_PROCESS set; skipping RenderPostProcess pass");
        }
        spdlog::info("Post-process: RenderGraph transitions {}",
                     result.useRenderGraphPost ? "enabled (default)" :
                                                 "disabled (CORTEX_DISABLE_RG_POST=1)");
        return result;
    }();
    return switches;
}

FrameFeaturePlan BuildFrameFeaturePlan(const FrameFeaturePlanInputs& inputs) {
    FrameFeaturePlan plan{};
    plan.debug = inputs.debug;

    plan.planned.rayTracingSupported = inputs.rayTracingSupported;
    plan.planned.rayTracingEnabled = inputs.rayTracingEnabled;
    plan.planned.rtReflectionsEnabled = inputs.rtReflectionsEnabled;
    plan.planned.rtGIEnabled = inputs.rtGIEnabled;
    plan.planned.shadowsEnabled = inputs.shadowsEnabled;
    plan.planned.gpuCullingEnabled = inputs.gpuCullingEnabled;
    plan.planned.visibilityBufferEnabled = inputs.visibilityBufferEnabled;
    plan.planned.taaEnabled = inputs.taaEnabled && !plan.debug.disableTAA;
    plan.planned.ssrEnabled = inputs.ssrEnabled && !plan.debug.disableSSR;
    plan.planned.ssaoEnabled = inputs.ssaoEnabled && !plan.debug.disableSSAO;
    plan.planned.bloomEnabled = inputs.bloomEnabled && !plan.debug.disableBloom;
    plan.planned.fxaaEnabled = inputs.fxaaEnabled;
    plan.planned.iblEnabled = inputs.iblEnabled;
    plan.planned.fogEnabled = inputs.fogEnabled;
    plan.planned.voxelBackendEnabled = inputs.voxelBackendEnabled;

    plan.active = plan.planned;
    plan.active.rayTracingEnabled =
        plan.planned.rayTracingEnabled && inputs.rayTracingSupported && inputs.hasRayTracingContext;
    plan.active.rtReflectionsEnabled =
        plan.active.rayTracingEnabled && plan.planned.rtReflectionsEnabled && inputs.hasRTReflectionColor;
    plan.active.rtGIEnabled =
        plan.active.rayTracingEnabled && plan.planned.rtGIEnabled && inputs.hasRTGIColor;
    plan.active.shadowsEnabled =
        plan.planned.shadowsEnabled && inputs.hasShadowMap && inputs.hasShadowPipeline;
    plan.active.gpuCullingEnabled =
        plan.planned.gpuCullingEnabled && inputs.hasGPUCulling;
    plan.active.visibilityBufferEnabled =
        plan.planned.visibilityBufferEnabled && inputs.hasVisibilityBuffer;
    plan.active.taaEnabled =
        plan.planned.taaEnabled && inputs.hasTAAPipeline && inputs.hasHistoryColor && inputs.hasTAAIntermediate;
    plan.active.ssrEnabled =
        plan.planned.ssrEnabled && inputs.hasSSRPipeline && inputs.hasSSRColor && inputs.hasHDRColor;
    plan.active.ssaoEnabled =
        plan.planned.ssaoEnabled && inputs.hasSSAOTarget &&
        (inputs.hasSSAOComputePipeline || inputs.hasSSAOPipeline);
    plan.active.bloomEnabled =
        plan.planned.bloomEnabled && inputs.hasBloomBase && inputs.hasBloomDownsamplePipeline;
    plan.active.voxelBackendEnabled =
        plan.planned.voxelBackendEnabled && inputs.hasVoxelPipeline;

    plan.runMinimalFrame = plan.debug.forceMinimalFrame;
    plan.runVoxelBackend = !plan.runMinimalFrame && plan.active.voxelBackendEnabled;
    plan.runRayTracing = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.rayTracingEnabled;
    plan.runDepthPrepass = plan.runRayTracing;
    plan.runShadowPass = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.shadowsEnabled;
    plan.runVisibilityBuffer = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.visibilityBufferEnabled;
    plan.runGpuCullingFallback = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.gpuCullingEnabled;
    plan.runMotionVectors =
        !plan.runMinimalFrame && !plan.runVoxelBackend && inputs.hasMotionVectorsPipeline &&
        inputs.hasVelocityBuffer && inputs.hasDepthBuffer;
    plan.runTAA = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.taaEnabled;
    plan.runSSR = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.ssrEnabled;
    plan.runSSAO = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.ssaoEnabled;
    plan.runBloom = !plan.runMinimalFrame && !plan.runVoxelBackend && plan.active.bloomEnabled;
    plan.runPostProcess =
        !plan.runMinimalFrame && !plan.runVoxelBackend && !plan.debug.disablePostProcess &&
        inputs.hasPostProcessPipeline && inputs.hasHDRColor;
    plan.runParticles =
        !plan.runMinimalFrame && !plan.runVoxelBackend && inputs.particlesEnabledForScene;
    plan.runDebugLines = !plan.runMinimalFrame && !plan.runVoxelBackend;
    plan.runHZB =
        !plan.runMinimalFrame && !plan.runVoxelBackend && plan.debug.enableHZB;
    plan.useRenderGraphHZB = plan.runHZB && plan.debug.useRenderGraphHZB;
    plan.useRenderGraphShadows = plan.runShadowPass && plan.debug.useRenderGraphShadows;
    plan.useRenderGraphPost = plan.runPostProcess && plan.debug.useRenderGraphPost;

    if (plan.runMinimalFrame || plan.runVoxelBackend) {
        plan.active.rayTracingEnabled = false;
        plan.active.rtReflectionsEnabled = false;
        plan.active.rtGIEnabled = false;
        plan.active.shadowsEnabled = false;
        plan.active.gpuCullingEnabled = false;
        plan.active.visibilityBufferEnabled = false;
        plan.active.taaEnabled = false;
        plan.active.ssrEnabled = false;
        plan.active.ssaoEnabled = false;
        plan.active.bloomEnabled = false;
        plan.active.fxaaEnabled = false;
        plan.active.iblEnabled = false;
        plan.active.fogEnabled = false;
        plan.active.voxelBackendEnabled = plan.runVoxelBackend;
    }

    return plan;
}

} // namespace Cortex::Graphics
