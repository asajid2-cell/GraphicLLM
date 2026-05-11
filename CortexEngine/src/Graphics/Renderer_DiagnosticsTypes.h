#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

#include "FrameContract.h"
#include "RendererPostProcessState.h"

namespace Cortex::Graphics {

struct RendererVRAMBreakdown {
    uint64_t renderTargetBytes = 0; // depth, shadow, HDR, G-buffer, velocity
    uint64_t postProcessBytes  = 0; // bloom, SSAO, SSR/RT history, TAA history
    uint64_t debugBytes        = 0; // debug line/overlay buffers
    uint64_t voxelBytes        = 0; // experimental voxel backend resources
    uint64_t textureBytes      = 0; // material/generic texture assets
    uint64_t environmentBytes  = 0; // IBL/environment assets
    uint64_t geometryBytes     = 0; // mesh vertex/index buffers
    uint64_t rtStructureBytes  = 0; // BLAS/TLAS/scratch still resident

    [[nodiscard]] uint64_t TotalBytes() const {
        return renderTargetBytes + postProcessBytes + debugBytes + voxelBytes +
               textureBytes + environmentBytes + geometryBytes + rtStructureBytes;
    }
};

struct RendererDescriptorStats {
    uint32_t rtvUsed = 0;
    uint32_t rtvCapacity = 0;
    uint32_t dsvUsed = 0;
    uint32_t dsvCapacity = 0;
    uint32_t shaderVisibleUsed = 0;
    uint32_t shaderVisibleCapacity = 0;
    uint32_t persistentUsed = 0;
    uint32_t persistentReserve = 0;
    uint32_t transientStart = 0;
    uint32_t transientEnd = 0;
    uint32_t stagingUsed = 0;
    uint32_t stagingCapacity = 0;
    uint32_t bindlessAllocated = 0;
    uint32_t bindlessCapacity = 0;
};

struct RendererHealthState {
    std::string adapterName;
    std::string qualityPreset;
    std::string graphicsPresetId;
    bool graphicsPresetDirtyFromUI = false;
    bool rayTracingRequested = false;
    bool rayTracingEffective = false;
    bool environmentLoaded = false;
    bool environmentFallback = false;
    std::string activeEnvironment;
    uint32_t residentEnvironments = 0;
    uint32_t pendingEnvironments = 0;
    uint32_t frameWarnings = 0;
    uint32_t assetFallbacks = 0;
    uint32_t descriptorPersistentUsed = 0;
    uint32_t descriptorPersistentBudget = 0;
    uint32_t descriptorTransientUsed = 0;
    uint32_t descriptorTransientBudget = 0;
    uint64_t estimatedVRAMBytes = 0;
    uint64_t textureBytes = 0;
    uint64_t environmentBytes = 0;
    uint64_t geometryBytes = 0;
    uint64_t rtStructureBytes = 0;
    std::string lastWarningCode;
    std::string lastWarningMessage;
};

struct RendererFrameRuntimeState {
    float totalTime = 0.0f;
    std::array<uint64_t, 3> fenceValues{};
    std::array<uint64_t, 3> computeFenceValues{};
    bool asyncComputeSupported = false;
    uint32_t frameIndex = 0;
    uint64_t absoluteFrameIndex = 0;
    bool commandListOpen = false;
    bool computeListOpen = false;
};

struct RendererFrameContractState {
    FrameContract contract;
    FrameContract::DrawCounts drawCounts{};
    FrameContract::MotionVectorInfo motionVectors{};
    FrameContract::TemporalMaskInfo temporalMask{};
    std::vector<FrameContract::PassRecord> passRecords;
    FrameContract::DescriptorUsage lastPassDescriptorUsage{};
};

struct RendererFrameLifecycleState {
    // Sticky flag set when the DX12 device reports device removal. Once set,
    // the renderer skips heavy work for the rest of the run.
    bool deviceRemoved = false;
    bool deviceRemovedLogged = false;
    bool visualValidationCaptured = false;

    // Runtime diagnostics for device-removed hangs and smoke logs.
    const char* lastCompletedPass = "None";
    uint64_t renderFrameCounter = 0;

    // Warning throttles for incomplete scene content.
    bool missingBufferWarningLogged = false;
    bool zeroDrawWarningLogged = false;
    bool verboseLoggingEnabled = true;

    bool rtReflectionWrittenThisFrame = false;
    bool backBufferUsedAsRTThisFrame = false;
};

struct RendererPassTimingState {
    float depthPrepassMs = 0.0f;
    float shadowPassMs = 0.0f;
    float mainPassMs = 0.0f;
    float rtPassMs = 0.0f;
    float ssrMs = 0.0f;
    float ssaoMs = 0.0f;
    float bloomMs = 0.0f;
    float postMs = 0.0f;
};

struct RendererDebugOverlayState {
    bool visible = false;
    int selectedRow = 0;
};

struct RendererRenderGraphTransitionState {
    bool depthPrepassSkipTransitions = false;
    bool shadowPassSkipTransitions = false;
    bool ssrSkipTransitions = false;
    bool ssaoSkipTransitions = false;
    bool bloomSkipTransitions = false;
    bool postProcessSkipTransitions = false;
};

struct RendererRenderGraphRuntimeState {
    FrameContract::RenderGraphInfo info{};
    bool transientValidationRan = false;
    RendererRenderGraphTransitionState transitions;
};

struct RendererFrameDiagnosticsState {
    RendererFrameContractState contract;
    RendererRenderGraphRuntimeState renderGraph;
    RendererPassTimingState timings;
};

struct RendererQualityState {
    std::string activeGraphicsPresetId = "runtime";
    bool graphicsPresetDirtyFromUI = false;
    float exposure = 1.0f;
    float bloomIntensity = 0.0f;
    float renderScale = 1.0f;
    bool shadowsEnabled = true;
    int debugViewMode = 0;
    uint32_t hzbDebugMip = 0;
    float shadowBias = 0.0f;
    float shadowPCFRadius = 0.0f;
    float cascadeSplitLambda = 0.0f;
    float cascade0ResolutionScale = 1.0f;
    bool visualValidationCaptured = false;
};

struct RendererFeatureState {
    bool taaEnabled = false;
    bool fxaaEnabled = false;
    bool pcssEnabled = false;
    bool ssaoEnabled = false;
    float ssaoRadius = 0.25f;
    float ssaoBias = 0.03f;
    float ssaoIntensity = 0.35f;
    bool iblEnabled = false;
    bool iblLimitEnabled = false;
    bool ssrEnabled = false;
    bool fogEnabled = false;
    bool particlesEnabled = false;
    float particleDensityScale = 1.0f;
    bool vegetationEnabled = false;
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogFalloff = 0.0f;
    float iblDiffuseIntensity = 0.0f;
    float iblSpecularIntensity = 0.0f;
    bool backgroundVisible = true;
    float backgroundExposure = 1.0f;
    float backgroundBlur = 0.0f;
    float godRayIntensity = 0.0f;
    float areaLightSizeScale = 1.0f;
    float sunIntensity = 0.0f;
    bool useSafeLightingRigOnLowVRAM = false;
};

struct RendererRayTracingState {
    bool supported = false;
    bool enabled = false;
    bool reflectionsEnabled = false;
    bool giEnabled = false;
    bool warmingUp = false;
    float reflectionDenoiseAlpha = 0.28f;
    float reflectionCompositionStrength = 1.0f;
};

struct RendererWaterState {
    float levelY = 0.0f;
    float waveAmplitude = 0.2f;
    float waveLength = 8.0f;
    float waveSpeed = 1.0f;
    float secondaryAmplitude = 0.1f;
    float steepness = 0.6f;
    glm::vec2 primaryDirection{1.0f, 0.0f};
};

struct RendererFractalSurfaceState {
    float amplitude = 0.0f;
    float frequency = 0.5f;
    float octaves = 4.0f;
    float coordMode = 1.0f; // 0 = UV, 1 = world XZ
    float scaleX = 1.0f;
    float scaleZ = 1.0f;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float warpStrength = 0.0f;
    float noiseType = 0.0f;
};

struct RendererFogState {
    bool enabled = false; // Disabled by default for clearer terrain
    float density = 0.02f;
    float height = 0.0f;
    float falloff = 0.5f;
};

struct RendererLightingState {
    glm::vec3 directionalDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)); // direction from surface to light
    glm::vec3 directionalColor{1.0f};
    float directionalIntensity = 5.0f;
    glm::vec3 ambientColor{0.04f};
    float ambientIntensity = 1.0f;
    std::string activeRigId = "custom";
    std::string activeRigSource = "manual";
    bool useSafeRigOnLowVRAM = false;
    bool safeRigVariantActive = false;
    float areaLightSizeScale = 1.0f;
};

} // namespace Cortex::Graphics
