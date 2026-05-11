#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Graphics {

enum class RenderableDepthClass : uint8_t {
    Invalid = 0,
    OpaqueDepthWriting,
    AlphaTestedDepthWriting,
    DoubleSidedOpaqueDepthWriting,
    DoubleSidedAlphaTestedDepthWriting,
    TransparentDepthTested,
    WaterDepthTestedNoWrite,
    OverlayDepthTestedNoWrite
};

inline bool WritesSceneDepth(RenderableDepthClass depthClass) {
    return depthClass == RenderableDepthClass::OpaqueDepthWriting ||
           depthClass == RenderableDepthClass::AlphaTestedDepthWriting ||
           depthClass == RenderableDepthClass::DoubleSidedOpaqueDepthWriting ||
           depthClass == RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting;
}

inline bool IsAlphaTestedDepthClass(RenderableDepthClass depthClass) {
    return depthClass == RenderableDepthClass::AlphaTestedDepthWriting ||
           depthClass == RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting;
}

inline bool IsDoubleSidedDepthClass(RenderableDepthClass depthClass) {
    return depthClass == RenderableDepthClass::DoubleSidedOpaqueDepthWriting ||
           depthClass == RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting;
}

struct FrameContract {
    struct StartupInfo {
        bool preflightRan = false;
        bool preflightPassed = true;
        bool safeMode = false;
        bool dxrRequested = false;
        bool environmentManifestPresent = false;
        bool environmentFallbackAvailable = false;
        uint32_t issueCount = 0;
        uint32_t warningCount = 0;
        uint32_t errorCount = 0;
        std::string configProfile = "default";
        std::string workingDirectory;
    };

    struct HealthInfo {
        std::string adapterName;
        std::string qualityPreset;
        bool rayTracingRequested = false;
        bool rayTracingEffective = false;
        bool environmentLoaded = false;
        bool environmentFallback = false;
        uint32_t frameWarnings = 0;
        uint32_t assetFallbacks = 0;
        uint32_t descriptorPersistentUsed = 0;
        uint32_t descriptorPersistentBudget = 0;
        uint32_t descriptorTransientUsed = 0;
        uint32_t descriptorTransientBudget = 0;
        uint64_t estimatedVRAMBytes = 0;
        std::string lastWarningCode;
        std::string lastWarningMessage;
    };

    struct EnvironmentInfo {
        std::string active;
        std::string requested;
        std::string runtimePath;
        std::string budgetClass = "unknown";
        bool loaded = false;
        bool fallback = false;
        std::string fallbackReason;
        bool manifestPresent = false;
        bool iblLimitEnabled = false;
        bool backgroundVisible = true;
        float backgroundExposure = 1.0f;
        float backgroundBlur = 0.0f;
        uint32_t residentCount = 0;
        uint32_t pendingCount = 0;
        uint32_t residentLimit = 0;
        uint32_t activeWidth = 0;
        uint32_t activeHeight = 0;
        uint32_t maxRuntimeDimension = 0;
        uint64_t residentBytes = 0;
        uint32_t localReflectionProbeCount = 0;
        uint32_t localReflectionProbeSkipped = 0;
        bool localReflectionProbeTableValid = false;
    };

    struct GraphicsPresetInfo {
        std::string id = "runtime";
        uint32_t schema = 1;
        bool dirtyFromUI = false;
        float renderScale = 1.0f;
    };

    struct FeatureFlags {
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
        bool particlesEnabled = false;
        bool voxelBackendEnabled = false;
    };

    struct ResourceInfo {
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t expectedWidth = 0;
        uint32_t expectedHeight = 0;
        uint64_t bytes = 0;
        bool valid = false;
        bool sizeMatchesContract = true;
    };

    struct HistoryInfo {
        std::string name;
        bool valid = false;
        bool resourceValid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t lastValidFrame = 0;
        uint64_t ageFrames = 0;
        std::string invalidReason;
        std::string lastResetReason;
        bool seeded = false;
        uint64_t lastInvalidatedFrame = 0;
        std::string rejectionMode = "none";
        float accumulationAlpha = 1.0f;
        bool usesDepthNormalRejection = false;
        bool usesVelocityReprojection = false;
        bool usesDisocclusionRejection = false;
    };

    struct RenderableClasses {
        uint32_t total = 0;
        uint32_t visible = 0;
        uint32_t invisible = 0;
        uint32_t meshless = 0;
        uint32_t opaqueDepthWriting = 0;
        uint32_t alphaTestedDepthWriting = 0;
        uint32_t doubleSidedOpaqueDepthWriting = 0;
        uint32_t doubleSidedAlphaTestedDepthWriting = 0;
        uint32_t transparentDepthTested = 0;
        uint32_t waterDepthTestedNoWrite = 0;
        uint32_t overlay = 0;
        uint32_t emissive = 0;
        uint32_t metallic = 0;
        uint32_t transmissive = 0;
        uint32_t clearcoat = 0;
    };

    struct MaterialStats {
        uint32_t sampled = 0;
        float minAlbedoLuminance = 0.0f;
        float maxAlbedoLuminance = 0.0f;
        float avgAlbedoLuminance = 0.0f;
        float minRoughness = 0.0f;
        float maxRoughness = 0.0f;
        float avgRoughness = 0.0f;
        float minMetallic = 0.0f;
        float maxMetallic = 0.0f;
        float avgMetallic = 0.0f;
        uint32_t veryDarkAlbedo = 0;
        uint32_t veryBrightAlbedo = 0;
        uint32_t roughnessOutOfRange = 0;
        uint32_t metallicOutOfRange = 0;
        uint32_t alphaBlend = 0;
        uint32_t alphaMask = 0;
        uint32_t presetNamed = 0;
        uint32_t presetDefaultMetallic = 0;
        uint32_t presetDefaultRoughness = 0;
        uint32_t presetDefaultTransmission = 0;
        uint32_t presetDefaultEmission = 0;
        uint32_t resolvedMetallic = 0;
        uint32_t resolvedConductor = 0;
        uint32_t resolvedTransmissive = 0;
        uint32_t resolvedEmissive = 0;
        uint32_t resolvedClearcoat = 0;
        uint32_t advancedFeatureMaterials = 0;
        uint32_t advancedClearcoat = 0;
        uint32_t advancedTransmission = 0;
        uint32_t advancedEmissive = 0;
        uint32_t advancedSpecular = 0;
        uint32_t advancedSheen = 0;
        uint32_t advancedSubsurface = 0;
        uint32_t surfaceDefault = 0;
        uint32_t surfaceGlass = 0;
        uint32_t surfaceMirror = 0;
        uint32_t surfacePlastic = 0;
        uint32_t surfaceMasonry = 0;
        uint32_t surfaceEmissive = 0;
        uint32_t surfaceBrushedMetal = 0;
        uint32_t surfaceWood = 0;
        uint32_t surfaceWater = 0;
        uint32_t reflectionEligible = 0;
        uint32_t reflectionHighCeiling = 0;
        uint32_t reflectionMirror = 0;
        uint32_t reflectionConductor = 0;
        uint32_t reflectionTransmissive = 0;
        uint32_t reflectionWater = 0;
        float maxReflectionCeilingEstimate = 0.0f;
        float avgReflectionCeilingEstimate = 0.0f;
        uint32_t validationIssues = 0;
        uint32_t validationWarnings = 0;
        uint32_t validationErrors = 0;
        uint32_t blendTransmission = 0;
        uint32_t metallicTransmission = 0;
        uint32_t lowRoughnessNormal = 0;
    };

    struct LightingInfo {
        std::string rigId = "custom";
        std::string rigSource = "manual";
        bool safeRigOnLowVRAM = false;
        bool safeRigVariantActive = false;
        float exposure = 1.0f;
        float sunIntensity = 0.0f;
        float iblDiffuseIntensity = 0.0f;
        float iblSpecularIntensity = 0.0f;
        float bloomIntensity = 0.0f;
        float ssaoRadius = 0.0f;
        float ssaoBias = 0.0f;
        float ssaoIntensity = 0.0f;
        float fogDensity = 0.0f;
        float fogHeight = 0.0f;
        float fogFalloff = 0.0f;
        float godRayIntensity = 0.0f;
        float shadowBias = 0.0f;
        float shadowPCFRadius = 0.0f;
        uint32_t lightCount = 0;
        uint32_t shadowCastingLightCount = 0;
        float totalLightIntensity = 0.0f;
        float maxLightIntensity = 0.0f;
    };

    struct CullingInfo {
        bool gpuCullingEnabled = false;
        bool cullingFrozen = false;
        bool statsValid = false;
        uint32_t tested = 0;
        uint32_t frustumCulled = 0;
        uint32_t occluded = 0;
        uint32_t visible = 0;
        bool visibilityBufferPlanned = false;
        bool visibilityBufferRendered = false;
        bool hzbResourceValid = false;
        bool hzbValid = false;
        bool hzbCaptureValid = false;
        bool hzbOcclusionUsedByVisibilityBuffer = false;
        bool hzbOcclusionUsedByGpuCulling = false;
        uint32_t hzbWidth = 0;
        uint32_t hzbHeight = 0;
        uint32_t hzbMipCount = 0;
        uint64_t hzbCaptureFrame = 0;
        uint64_t hzbAgeFrames = 0;
    };

    struct ParticleInfo {
        bool enabled = false;
        bool planned = false;
        bool executed = false;
        bool instanceMapFailed = false;
        bool capped = false;
        float densityScale = 1.0f;
        uint32_t emitterCount = 0;
        uint32_t liveParticles = 0;
        uint32_t submittedInstances = 0;
        uint32_t frustumCulled = 0;
        uint32_t maxInstances = 0;
        uint32_t instanceCapacity = 0;
        uint64_t instanceBufferBytes = 0;
    };

    struct VegetationInfo {
        bool enabled = false;
        bool meshPipelineReady = false;
        bool billboardPipelineReady = false;
        bool grassPipelineReady = false;
        bool shadowPipelineReady = false;
        bool atlasLoaded = false;
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t meshInstances = 0;
        uint32_t billboardInstances = 0;
        uint32_t grassInstances = 0;
        uint32_t meshCapacity = 0;
        uint32_t billboardCapacity = 0;
        uint32_t grassCapacity = 0;
    };

    struct CinematicPostInfo {
        bool enabled = false;
        bool postProcessPlanned = false;
        bool postProcessExecuted = false;
        bool bloomPlanned = false;
        bool bloomExecuted = false;
        float bloomIntensity = 0.0f;
        float bloomThreshold = 1.0f;
        float bloomSoftKnee = 0.5f;
        float bloomMaxContribution = 4.0f;
        float vignette = 0.0f;
        float lensDirt = 0.0f;
        float warm = 0.0f;
        float cool = 0.0f;
        float godRayIntensity = 0.0f;
    };

    struct MotionVectorInfo {
        bool planned = false;
        bool executed = false;
        bool visibilityBufferMotion = false;
        bool cameraOnlyFallback = false;
        bool previousTransformHistoryReset = false;
        uint32_t instanceCount = 0;
        uint32_t meshCount = 0;
        uint32_t previousWorldMatrices = 0;
        uint32_t seededPreviousWorldMatrices = 0;
        uint32_t prunedPreviousWorldMatrices = 0;
        float maxObjectMotionWorld = 0.0f;
    };

    struct TemporalMaskInfo {
        bool valid = false;
        bool built = false;
        uint64_t sampleFrame = 0;
        uint32_t pixelCount = 0;
        float acceptedRatio = 0.0f;
        float disocclusionRatio = 0.0f;
        float highMotionRatio = 0.0f;
        float outOfBoundsRatio = 0.0f;
        uint32_t readbackLatencyFrames = 0;
    };

    struct BudgetInfo {
        std::string profileName;
        bool forced = false;
        uint64_t dedicatedVideoMemoryBytes = 0;
        float targetRenderScale = 1.0f;
        uint32_t maxRenderWidth = 0;
        uint32_t maxRenderHeight = 0;
        uint32_t ssaoDivisor = 0;
        uint32_t shadowMapSize = 0;
        uint32_t bloomLevels = 0;
        uint32_t iblResidentEnvironmentLimit = 0;
        uint32_t materialTextureMaxDimension = 0;
        uint32_t materialTextureBudgetFloorDimension = 0;
        uint64_t textureBudgetBytes = 0;
        uint64_t environmentBudgetBytes = 0;
        uint64_t geometryBudgetBytes = 0;
        uint64_t rtStructureBudgetBytes = 0;
        float rtResolutionScale = 1.0f;
        uint32_t reflectionUpdateCadence = 1;
        uint32_t giUpdateCadence = 1;
    };

    struct AssetMemoryInfo {
        uint64_t textureBytes = 0;
        uint64_t environmentBytes = 0;
        uint64_t geometryBytes = 0;
        uint64_t rtStructureBytes = 0;
        bool textureBudgetExceeded = false;
        bool environmentBudgetExceeded = false;
        bool geometryBudgetExceeded = false;
        bool rtStructureBudgetExceeded = false;
    };

    struct RayTracingInfo {
        bool supported = false;
        bool enabled = false;
        bool warmingUp = false;
        bool schedulerEnabled = false;
        bool schedulerBuildTLAS = false;
        bool dispatchShadows = false;
        bool dispatchReflections = false;
        bool dispatchGI = false;
        bool reflectionDispatchReady = false;
        bool reflectionHasPipeline = false;
        bool reflectionHasTLAS = false;
        bool reflectionHasMaterialBuffer = false;
        bool reflectionHasOutput = false;
        bool reflectionHasDepth = false;
        bool reflectionHasNormalRoughness = false;
        bool reflectionHasMaterialExt2 = false;
        bool reflectionHasEnvironmentTable = false;
        bool reflectionHasFrameConstants = false;
        bool reflectionHasDispatchDescriptors = false;
        uint32_t reflectionDispatchWidth = 0;
        uint32_t reflectionDispatchHeight = 0;
        std::string reflectionReadinessReason;
        bool reflectionSignalStatsCaptured = false;
        bool reflectionSignalValid = false;
        uint64_t reflectionSignalSampleFrame = 0;
        uint32_t reflectionSignalPixelCount = 0;
        float reflectionSignalAvgLuma = 0.0f;
        float reflectionSignalMaxLuma = 0.0f;
        float reflectionSignalNonZeroRatio = 0.0f;
        float reflectionSignalBrightRatio = 0.0f;
        float reflectionSignalOutlierRatio = 0.0f;
        uint32_t reflectionSignalReadbackLatencyFrames = 0;
        bool reflectionHistorySignalStatsCaptured = false;
        bool reflectionHistorySignalValid = false;
        uint64_t reflectionHistorySignalSampleFrame = 0;
        uint32_t reflectionHistorySignalPixelCount = 0;
        float reflectionHistorySignalAvgLuma = 0.0f;
        float reflectionHistorySignalMaxLuma = 0.0f;
        float reflectionHistorySignalNonZeroRatio = 0.0f;
        float reflectionHistorySignalBrightRatio = 0.0f;
        float reflectionHistorySignalOutlierRatio = 0.0f;
        float reflectionHistorySignalAvgLumaDelta = 0.0f;
        uint32_t reflectionHistorySignalReadbackLatencyFrames = 0;
        bool denoiseShadows = false;
        bool denoiseReflections = false;
        bool denoiseGI = false;
        bool denoiserExecuted = false;
        uint32_t denoiserPasses = 0;
        bool denoiserUsesDepthNormalRejection = false;
        bool denoiserUsesVelocityReprojection = false;
        bool denoiserUsesDisocclusionRejection = false;
        float shadowDenoiseAlpha = 1.0f;
        float reflectionDenoiseAlpha = 1.0f;
        float giDenoiseAlpha = 1.0f;
        std::string budgetProfile;
        std::string schedulerDisabledReason;
        uint32_t schedulerTLASCandidates = 0;
        uint32_t schedulerMaxTLASInstances = 0;
        uint32_t reflectionWidth = 0;
        uint32_t reflectionHeight = 0;
        uint32_t giWidth = 0;
        uint32_t giHeight = 0;
        uint32_t reflectionUpdateCadence = 1;
        uint32_t giUpdateCadence = 1;
        uint32_t reflectionFramePhase = 0;
        uint32_t giFramePhase = 0;
        uint64_t dedicatedVideoMemoryBytes = 0;
        uint64_t maxBLASBuildBytesPerFrame = 0;
        uint64_t maxBLASTotalBytes = 0;
        uint32_t pendingBLAS = 0;
        uint32_t pendingRendererBLASJobs = 0;
        uint32_t tlasInstances = 0;
        uint32_t materialRecords = 0;
        uint64_t materialBufferBytes = 0;
        uint32_t tlasCandidates = 0;
        uint32_t tlasSkippedInvalid = 0;
        uint32_t tlasMissingGeometry = 0;
        uint32_t tlasDistanceCulled = 0;
        uint32_t tlasBLASBuildRequested = 0;
        uint32_t tlasBLASBuildBudgetDeferred = 0;
        uint32_t tlasBLASTotalBudgetSkipped = 0;
        uint32_t tlasBLASBuildFailed = 0;
        uint32_t surfaceDefault = 0;
        uint32_t surfaceGlass = 0;
        uint32_t surfaceMirror = 0;
        uint32_t surfacePlastic = 0;
        uint32_t surfaceMasonry = 0;
        uint32_t surfaceEmissive = 0;
        uint32_t surfaceBrushedMetal = 0;
        uint32_t surfaceWood = 0;
        uint32_t surfaceWater = 0;
        bool materialSurfaceParityComparable = false;
        bool materialSurfaceParityMatches = false;
        uint32_t materialSurfaceParityMismatches = 0;
    };

    struct DrawCounts {
        uint32_t depthPrepassDraws = 0;
        uint32_t shadowDraws = 0;
        uint32_t opaqueDraws = 0;
        uint32_t visibilityBufferInstances = 0;
        uint32_t visibilityBufferMeshes = 0;
        uint32_t visibilityBufferDrawBatches = 0;
        uint32_t indirectExecuteCalls = 0;
        uint32_t indirectCommands = 0;
        uint32_t overlayDraws = 0;
        uint32_t waterDraws = 0;
        uint32_t transparentDraws = 0;
        uint32_t particleDraws = 0;
        uint32_t particleInstances = 0;
        uint32_t debugLineDraws = 0;
        uint32_t debugLineVertices = 0;
    };

    struct ResourceAccess {
        std::string name;
        std::string stateClass;
    };

    struct DescriptorUsage {
        uint32_t rtvUsed = 0;
        uint32_t dsvUsed = 0;
        uint32_t shaderVisibleUsed = 0;
        uint32_t shaderVisibleDelta = 0;
        uint32_t transientUsed = 0;
        uint32_t transientDelta = 0;
        uint32_t stagingUsed = 0;
        uint32_t stagingDelta = 0;
    };

    struct RenderGraphInfo {
        bool active = false;
        uint32_t executions = 0;
        uint32_t passRecords = 0;
        uint32_t graphPasses = 0;
        uint32_t culledPasses = 0;
        uint32_t barriers = 0;
        uint32_t fallbackExecutions = 0;
        bool transientValidationRan = false;
        uint32_t transientResources = 0;
        uint32_t placedResources = 0;
        uint32_t aliasedResources = 0;
        uint32_t aliasingBarriers = 0;
        uint64_t transientRequestedBytes = 0;
        uint64_t transientHeapUsedBytes = 0;
        uint64_t transientHeapSizeBytes = 0;
        uint64_t transientSavedBytes = 0;
    };

    struct PassRecord {
        std::string name;
        bool planned = false;
        bool executed = false;
        bool fallbackUsed = false;
        uint32_t drawCount = 0;
        double estimatedWriteMB = 0.0;
        bool fullScreen = false;
        bool historyDependent = false;
        bool rayTracing = false;
        bool renderGraph = false;
        std::string fallbackReason;
        std::string resolutionClass;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        std::vector<ResourceAccess> readResources;
        std::vector<ResourceAccess> writeResources;
        DescriptorUsage descriptors;
    };

    uint64_t absoluteFrame = 0;
    uint32_t swapchainFrameIndex = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t presentationWidth = 0;
    uint32_t presentationHeight = 0;

    StartupInfo startup;
    HealthInfo health;
    EnvironmentInfo environment;
    GraphicsPresetInfo graphicsPreset;
    FeatureFlags features;
    FeatureFlags plannedFeatures;
    FeatureFlags executedFeatures;
    RenderableClasses renderables;
    MaterialStats materials;
    LightingInfo lighting;
    CullingInfo culling;
    ParticleInfo particles;
    VegetationInfo vegetation;
    CinematicPostInfo cinematicPost;
    MotionVectorInfo motionVectors;
    TemporalMaskInfo temporalMask;
    BudgetInfo budget;
    AssetMemoryInfo assetMemory;
    RayTracingInfo rayTracing;
    RenderGraphInfo renderGraph;
    DrawCounts draws;
    std::vector<PassRecord> passes;
    std::vector<ResourceInfo> resources;
    std::vector<HistoryInfo> histories;
    std::vector<std::string> warnings;
};

} // namespace Cortex::Graphics
