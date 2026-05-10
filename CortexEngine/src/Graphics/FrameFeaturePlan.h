#pragma once

#include "Graphics/FrameContract.h"

#include <cstdint>

namespace Cortex::Scene {
class ECS_Registry;
}

namespace Cortex::Graphics {

struct RuntimeFrameDebugSwitches {
    bool forceEnableFeatures = false;
    bool disableSSR = false;
    bool disableSSAO = false;
    bool disableBloom = false;
    bool disableTAA = false;
    bool logVRAM = false;
    bool forceMinimalFrame = false;
    bool useRenderGraphShadows = true;
    bool enableHZB = true;
    bool useRenderGraphHZB = true;
    bool disablePostProcess = false;
    bool useRenderGraphPost = true;
};

[[nodiscard]] RuntimeFrameDebugSwitches LoadRuntimeFrameDebugSwitches();

struct FrameFeaturePlan {
    FrameContract::FeatureFlags planned{};
    FrameContract::FeatureFlags active{};
    RuntimeFrameDebugSwitches debug{};

    bool runMinimalFrame = false;
    bool runVoxelBackend = false;
    bool runRayTracing = false;
    bool runDepthPrepass = false;
    bool runShadowPass = false;
    bool runVisibilityBuffer = false;
    bool runGpuCullingFallback = false;
    bool runMotionVectors = false;
    bool runTAA = false;
    bool runSSR = false;
    bool runSSAO = false;
    bool runBloom = false;
    bool runPostProcess = false;
    bool runParticles = false;
    bool runDebugLines = false;
    bool runHZB = false;
    bool useRenderGraphHZB = false;
    bool useRenderGraphShadows = false;
    bool useRenderGraphPost = false;
};

struct FrameFeaturePlanInputs {
    RuntimeFrameDebugSwitches debug{};

    bool rayTracingSupported = false;
    bool rayTracingEnabled = false;
    bool rtReflectionsEnabled = false;
    bool rtGIEnabled = false;
    bool shadowsEnabled = false;
    bool gpuCullingEnabled = false;
    bool visibilityBufferEnabled = false;
    bool taaEnabled = false;
    bool ssrEnabled = false;
    bool ssaoEnabled = false;
    bool bloomEnabled = false;
    bool fxaaEnabled = false;
    bool iblEnabled = false;
    bool fogEnabled = false;
    bool voxelBackendEnabled = false;

    bool hasRayTracingContext = false;
    bool hasRTReflectionColor = false;
    bool hasRTGIColor = false;
    bool hasShadowMap = false;
    bool hasShadowPipeline = false;
    bool hasGPUCulling = false;
    bool hasVisibilityBuffer = false;
    bool hasTAAPipeline = false;
    bool hasHistoryColor = false;
    bool hasTAAIntermediate = false;
    bool hasSSRPipeline = false;
    bool hasSSRColor = false;
    bool hasHDRColor = false;
    bool hasSSAOTarget = false;
    bool hasSSAOComputePipeline = false;
    bool hasSSAOPipeline = false;
    bool hasBloomBase = false;
    bool hasBloomDownsamplePipeline = false;
    bool hasVoxelPipeline = false;
    bool hasMotionVectorsPipeline = false;
    bool hasVelocityBuffer = false;
    bool hasDepthBuffer = false;
    bool hasPostProcessPipeline = false;
    bool particlesEnabledForScene = false;
};

[[nodiscard]] FrameFeaturePlan BuildFrameFeaturePlan(const FrameFeaturePlanInputs& inputs);

struct FrameExecutionContext {
    Scene::ECS_Registry* registry = nullptr;
    float deltaTime = 0.0f;
    uint64_t frameNumber = 0;
    uint32_t frameIndex = 0;
    FrameFeaturePlan features{};
};

} // namespace Cortex::Graphics
