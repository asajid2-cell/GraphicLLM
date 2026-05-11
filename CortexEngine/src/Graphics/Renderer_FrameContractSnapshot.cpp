#include "Renderer.h"

#include "Core/StartupPreflight.h"
#include "Graphics/FrameContractResources.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/Components.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

namespace Cortex::Graphics {

namespace {
void ApplyRTPlanToContract(FrameContract::RayTracingInfo& info, const RTFramePlan& plan) {
    info.schedulerEnabled = plan.enabled;
    info.schedulerBuildTLAS = plan.buildTLAS;
    info.dispatchShadows = plan.dispatchShadows;
    info.dispatchReflections = plan.dispatchReflections;
    info.dispatchGI = plan.dispatchGI;
    info.denoiseShadows = plan.denoiseShadows;
    info.denoiseReflections = plan.denoiseReflections;
    info.denoiseGI = plan.denoiseGI;
    info.budgetProfile = plan.budget.profileName;
    info.schedulerDisabledReason = plan.disabledReason;
    info.schedulerTLASCandidates = plan.tlasCandidateCount;
    info.schedulerMaxTLASInstances = plan.budget.maxTLASInstances;
    info.reflectionWidth = plan.budget.reflectionWidth;
    info.reflectionHeight = plan.budget.reflectionHeight;
    info.giWidth = plan.budget.giWidth;
    info.giHeight = plan.budget.giHeight;
    info.reflectionUpdateCadence = plan.budget.reflectionUpdateCadence;
    info.giUpdateCadence = plan.budget.giUpdateCadence;
    info.reflectionFramePhase = plan.reflectionFramePhase;
    info.giFramePhase = plan.giFramePhase;
    info.dedicatedVideoMemoryBytes = plan.budget.dedicatedVideoMemoryBytes;
    info.maxBLASBuildBytesPerFrame = plan.budget.maxBLASBuildBytesPerFrame;
    info.maxBLASTotalBytes = plan.budget.maxBLASTotalBytes;
}

void ApplyBudgetPlanToContract(FrameContract::BudgetInfo& info, const RendererBudgetPlan& plan) {
    info.profileName = plan.profileName;
    info.forced = plan.forced;
    info.dedicatedVideoMemoryBytes = plan.dedicatedVideoMemoryBytes;
    info.targetRenderScale = plan.targetRenderScale;
    info.maxRenderWidth = plan.maxRenderWidth;
    info.maxRenderHeight = plan.maxRenderHeight;
    info.ssaoDivisor = plan.ssaoDivisor;
    info.shadowMapSize = plan.shadowMapSize;
    info.bloomLevels = plan.bloomLevels;
    info.iblResidentEnvironmentLimit = plan.iblResidentEnvironmentLimit;
    info.materialTextureMaxDimension = plan.materialTextureMaxDimension;
    info.materialTextureBudgetFloorDimension = plan.materialTextureBudgetFloorDimension;
    info.textureBudgetBytes = plan.textureBudgetBytes;
    info.environmentBudgetBytes = plan.environmentBudgetBytes;
    info.geometryBudgetBytes = plan.geometryBudgetBytes;
    info.rtStructureBudgetBytes = plan.rtStructureBudgetBytes;
    info.rtResolutionScale = plan.rtResolutionScale;
    info.reflectionUpdateCadence = plan.reflectionUpdateCadence;
    info.giUpdateCadence = plan.giUpdateCadence;
}

} // namespace

void Renderer::UpdateFrameContractSnapshot(Scene::ECS_Registry* registry,
                                           const FrameFeaturePlan& featurePlan) {
    FrameContract contract{};
    contract.absoluteFrame = m_frameLifecycle.renderFrameCounter;
    contract.swapchainFrameIndex = m_frameRuntime.frameIndex;
    contract.renderWidth = GetInternalRenderWidth();
    contract.renderHeight = GetInternalRenderHeight();
    contract.presentationWidth = m_services.window ? m_services.window->GetWidth() : 0;
    contract.presentationHeight = m_services.window ? m_services.window->GetHeight() : 0;

    const auto& preflight = Cortex::GetStartupPreflightResult();
    contract.startup.preflightRan = preflight.ran;
    contract.startup.preflightPassed = preflight.canLaunch;
    contract.startup.safeMode = preflight.usedSafeMode;
    contract.startup.dxrRequested = preflight.dxrRequested;
    contract.startup.environmentManifestPresent = preflight.environmentManifestPresent;
    contract.startup.environmentFallbackAvailable = preflight.environmentFallbackAvailable;
    contract.startup.issueCount = static_cast<uint32_t>(preflight.issues.size());
    contract.startup.warningCount = preflight.WarningCount();
    contract.startup.errorCount = preflight.ErrorCount();
    contract.startup.configProfile = preflight.configProfile;
    contract.startup.workingDirectory = preflight.workingDirectory.string();

    const auto health = BuildHealthState();
    contract.health.adapterName = health.adapterName;
    contract.health.qualityPreset = health.qualityPreset;
    contract.health.rayTracingRequested = health.rayTracingRequested;
    contract.health.rayTracingEffective = health.rayTracingEffective;
    contract.health.environmentLoaded = health.environmentLoaded;
    contract.health.environmentFallback = health.environmentFallback;
    contract.health.frameWarnings = health.frameWarnings;
    contract.health.assetFallbacks = health.assetFallbacks;
    contract.health.descriptorPersistentUsed = health.descriptorPersistentUsed;
    contract.health.descriptorPersistentBudget = health.descriptorPersistentBudget;
    contract.health.descriptorTransientUsed = health.descriptorTransientUsed;
    contract.health.descriptorTransientBudget = health.descriptorTransientBudget;
    contract.health.estimatedVRAMBytes = health.estimatedVRAMBytes;
    contract.health.lastWarningCode = health.lastWarningCode;
    contract.health.lastWarningMessage = health.lastWarningMessage;

    contract.environment.active = health.activeEnvironment;
    contract.environment.requested = m_environmentState.requestedEnvironment;
    contract.environment.loaded = health.environmentLoaded;
    contract.environment.fallback = health.environmentFallback;
    contract.environment.fallbackReason = m_environmentState.fallbackReason;
    contract.environment.manifestPresent = preflight.environmentManifestPresent;
    contract.environment.iblLimitEnabled = m_environmentState.limitEnabled;
    contract.environment.backgroundVisible = m_environmentState.backgroundVisible;
    contract.environment.backgroundExposure = m_environmentState.backgroundExposure;
    contract.environment.backgroundBlur = m_environmentState.backgroundBlur;
    contract.environment.rotationDegrees = m_environmentState.rotationDegrees;
    contract.environment.residentCount = health.residentEnvironments;
    contract.environment.pendingCount = health.pendingEnvironments;
    contract.environment.residentLimit = m_framePlanning.budgetPlan.iblResidentEnvironmentLimit;
    contract.environment.residentBytes = health.environmentBytes;
    contract.environment.localReflectionProbeCount =
        static_cast<uint32_t>(m_visibilityBufferState.reflectionProbes.size());
    contract.environment.localReflectionProbeSkipped = m_visibilityBufferState.reflectionProbeSkipped;
    contract.environment.localReflectionProbeTableValid = m_visibilityBufferState.reflectionProbeTableValid;
    if (const auto* env = m_environmentState.ActiveEnvironment()) {
        contract.environment.runtimePath = env->path;
        contract.environment.budgetClass = env->budgetClass;
        contract.environment.maxRuntimeDimension = env->maxRuntimeDimension;
        if (env->diffuseIrradiance) {
            contract.environment.activeWidth = env->diffuseIrradiance->GetWidth();
            contract.environment.activeHeight = env->diffuseIrradiance->GetHeight();
        }
    }
    contract.graphicsPreset.id = health.graphicsPresetId.empty() ? health.qualityPreset : health.graphicsPresetId;
    contract.graphicsPreset.dirtyFromUI = health.graphicsPresetDirtyFromUI;
    contract.graphicsPreset.renderScale = m_qualityRuntimeState.renderScale;

    contract.plannedFeatures = featurePlan.planned;
    contract.executedFeatures = featurePlan.active;
    contract.features = featurePlan.active;
    contract.lighting.rigId = m_lightingState.activeRigId;
    contract.lighting.rigSource = m_lightingState.activeRigSource;
    contract.lighting.safeRigOnLowVRAM = m_lightingState.useSafeRigOnLowVRAM;
    contract.lighting.safeRigVariantActive = m_lightingState.safeRigVariantActive;
    contract.lighting.exposure = m_qualityRuntimeState.exposure;
    contract.lighting.sunIntensity = m_lightingState.directionalIntensity;
    contract.lighting.iblDiffuseIntensity = m_environmentState.diffuseIntensity;
    contract.lighting.iblSpecularIntensity = m_environmentState.specularIntensity;
    contract.lighting.bloomIntensity = m_bloomResources.intensity;
    contract.lighting.ssaoRadius = m_ssaoResources.radius;
    contract.lighting.ssaoBias = m_ssaoResources.bias;
    contract.lighting.ssaoIntensity = m_ssaoResources.intensity;
    contract.screenSpace.ssrEnabled = m_ssrResources.enabled;
    contract.screenSpace.ssrMaxDistance = m_ssrResources.maxDistance;
    contract.screenSpace.ssrThickness = m_ssrResources.thickness;
    contract.screenSpace.ssrStrength = m_ssrResources.strength;
    contract.screenSpace.ssaoEnabled = m_ssaoResources.enabled;
    contract.screenSpace.ssaoRadius = m_ssaoResources.radius;
    contract.screenSpace.ssaoBias = m_ssaoResources.bias;
    contract.screenSpace.ssaoIntensity = m_ssaoResources.intensity;
    contract.lighting.fogDensity = m_fogState.density;
    contract.lighting.fogHeight = m_fogState.height;
    contract.lighting.fogFalloff = m_fogState.falloff;
    contract.lighting.godRayIntensity = m_postProcessState.godRayIntensity;
    contract.lighting.areaLightSizeScale = m_lightingState.areaLightSizeScale;
    contract.lighting.shadowBias = m_shadowResources.bias;
    contract.lighting.shadowPCFRadius = m_shadowResources.pcfRadius;
    contract.lighting.cascadeSplitLambda = m_shadowResources.cascadeSplitLambda;

    if (m_framePlanning.sceneSnapshot.IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        contract.renderables = m_framePlanning.sceneSnapshot.renderables;
        contract.materials = m_framePlanning.sceneSnapshot.materials;
    }

    if (registry) {
        auto lightView = registry->View<Scene::LightComponent>();
        for (auto entity : lightView) {
            const auto& light = lightView.get<Scene::LightComponent>(entity);
            ++contract.lighting.lightCount;
            if (light.castsShadows) {
                ++contract.lighting.shadowCastingLightCount;
            }
            contract.lighting.totalLightIntensity += light.intensity;
            contract.lighting.maxLightIntensity =
                std::max(contract.lighting.maxLightIntensity, light.intensity);
        }
    }

    auto addResource = [&](const char* name, ID3D12Resource* resource, uint32_t expectedWidth, uint32_t expectedHeight) {
        FrameContract::ResourceInfo info{};
        info.name = name ? name : "";
        info.expectedWidth = expectedWidth;
        info.expectedHeight = expectedHeight;
        info.valid = resource != nullptr;
        if (resource) {
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                info.width = static_cast<uint32_t>(desc.Width);
                info.height = desc.Height;
                info.bytes = static_cast<uint64_t>(info.width) *
                             static_cast<uint64_t>(info.height) *
                             BytesPerPixelForContract(desc.Format);
            } else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
                info.bytes = desc.Width;
            }
        }
        if (expectedWidth > 0 && expectedHeight > 0) {
            info.sizeMatchesContract =
                info.valid && info.width == expectedWidth && info.height == expectedHeight;
        }
        contract.resources.push_back(std::move(info));
    };

    const uint32_t halfWidth = std::max(1u, contract.renderWidth / 2u);
    const uint32_t halfHeight = std::max(1u, contract.renderHeight / 2u);
    const uint32_t ssaoDivisor = std::max(1u, m_framePlanning.budgetPlan.ssaoDivisor);
    const uint32_t ssaoWidth = std::max(1u, contract.renderWidth / ssaoDivisor);
    const uint32_t ssaoHeight = std::max(1u, contract.renderHeight / ssaoDivisor);

    addResource("depth", m_depthResources.buffer.Get(), contract.renderWidth, contract.renderHeight);
    addResource("hdr_color", m_mainTargets.hdrColor.Get(), contract.renderWidth, contract.renderHeight);
    addResource("gbuffer_normal_roughness", m_mainTargets.gbufferNormalRoughness.Get(), contract.renderWidth, contract.renderHeight);
    if (m_services.visibilityBuffer) {
        addResource("vb_gbuffer_albedo", m_services.visibilityBuffer->GetAlbedoBuffer(), contract.renderWidth, contract.renderHeight);
        addResource("vb_gbuffer_normal_roughness", m_services.visibilityBuffer->GetNormalRoughnessBuffer(), contract.renderWidth, contract.renderHeight);
        addResource("vb_gbuffer_emissive_metallic", m_services.visibilityBuffer->GetEmissiveMetallicBuffer(), contract.renderWidth, contract.renderHeight);
        addResource("vb_gbuffer_material_ext0", m_services.visibilityBuffer->GetMaterialExt0Buffer(), contract.renderWidth, contract.renderHeight);
        addResource("vb_gbuffer_material_ext1", m_services.visibilityBuffer->GetMaterialExt1Buffer(), contract.renderWidth, contract.renderHeight);
        addResource("vb_gbuffer_material_ext2", m_services.visibilityBuffer->GetMaterialExt2Buffer(), contract.renderWidth, contract.renderHeight);
    }
    addResource("ssao", m_ssaoResources.texture.Get(), ssaoWidth, ssaoHeight);
    addResource("ssr_color", m_ssrResources.color.Get(), contract.renderWidth, contract.renderHeight);
    addResource("velocity", m_temporalScreenState.velocityBuffer.Get(), contract.renderWidth, contract.renderHeight);
    addResource("temporal_rejection_mask", m_temporalMaskState.texture.Get(), contract.renderWidth, contract.renderHeight);
    addResource("temporal_rejection_mask_stats", m_temporalMaskState.statsBuffer.Get(), 0, 0);
    addResource("taa_history", m_temporalScreenState.historyColor.Get(), contract.renderWidth, contract.renderHeight);
    addResource("taa_intermediate", m_temporalScreenState.taaIntermediate.Get(), contract.renderWidth, contract.renderHeight);
    addResource("rt_shadow_mask", m_rtShadowTargets.mask.Get(), contract.renderWidth, contract.renderHeight);
    addResource("rt_shadow_history", m_rtShadowTargets.history.Get(), contract.renderWidth, contract.renderHeight);
    const uint32_t expectedReflectionWidth = m_framePlanning.rtPlan.budget.reflectionWidth > 0
        ? m_framePlanning.rtPlan.budget.reflectionWidth
        : halfWidth;
    const uint32_t expectedReflectionHeight = m_framePlanning.rtPlan.budget.reflectionHeight > 0
        ? m_framePlanning.rtPlan.budget.reflectionHeight
        : halfHeight;
    const uint32_t expectedGIWidth = m_framePlanning.rtPlan.budget.giWidth > 0
        ? m_framePlanning.rtPlan.budget.giWidth
        : halfWidth;
    const uint32_t expectedGIHeight = m_framePlanning.rtPlan.budget.giHeight > 0
        ? m_framePlanning.rtPlan.budget.giHeight
        : halfHeight;
    addResource("rt_reflection", m_rtReflectionTargets.color.Get(), expectedReflectionWidth, expectedReflectionHeight);
    addResource("rt_reflection_signal_stats", m_rtReflectionSignalState.rawResources.statsBuffer.Get(), 0, 0);
    addResource("rt_reflection_history", m_rtReflectionTargets.history.Get(), expectedReflectionWidth, expectedReflectionHeight);
    addResource("rt_reflection_history_signal_stats", m_rtReflectionSignalState.historyResources.statsBuffer.Get(), 0, 0);
    addResource("rt_gi", m_rtGITargets.color.Get(), expectedGIWidth, expectedGIHeight);
    addResource("rt_gi_history", m_rtGITargets.history.Get(), expectedGIWidth, expectedGIHeight);
    addResource("shadow_map",
                m_shadowResources.map.Get(),
                static_cast<uint32_t>(m_shadowResources.mapSize),
                static_cast<uint32_t>(m_shadowResources.mapSize));
    for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
        const uint32_t div = 1u << (level + 1u);
        const uint32_t bloomWidth = std::max(1u, contract.renderWidth / div);
        const uint32_t bloomHeight = std::max(1u, contract.renderHeight / div);
        addResource(("bloom_a_" + std::to_string(level)).c_str(), m_bloomResources.texA[level].Get(), bloomWidth, bloomHeight);
        addResource(("bloom_b_" + std::to_string(level)).c_str(), m_bloomResources.texB[level].Get(), bloomWidth, bloomHeight);
    }

    contract.culling.gpuCullingEnabled = m_gpuCullingState.enabled;
    contract.culling.cullingFrozen = m_gpuCullingState.freeze || (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr);
    contract.culling.visibilityBufferPlanned = m_visibilityBufferState.plannedThisFrame;
    contract.culling.visibilityBufferRendered = m_visibilityBufferState.renderedThisFrame;
    contract.culling.hzbResourceValid = m_hzbResources.texture != nullptr;
    contract.culling.hzbValid = m_hzbResources.valid;
    contract.culling.hzbCaptureValid = m_hzbResources.captureValid;
    contract.culling.hzbOcclusionUsedByVisibilityBuffer = m_visibilityBufferState.hzbOcclusionUsedThisFrame;
    contract.culling.hzbOcclusionUsedByGpuCulling = m_gpuCullingState.hzbOcclusionUsedThisFrame;
    contract.culling.hzbWidth = m_hzbResources.width;
    contract.culling.hzbHeight = m_hzbResources.height;
    contract.culling.hzbMipCount = m_hzbResources.mipCount;
    contract.culling.hzbCaptureFrame = m_hzbResources.captureFrameCounter;
    contract.culling.hzbAgeFrames =
        (m_hzbResources.captureValid && m_frameLifecycle.renderFrameCounter >= m_hzbResources.captureFrameCounter)
            ? (m_frameLifecycle.renderFrameCounter - m_hzbResources.captureFrameCounter)
            : 0;
    if (m_services.gpuCulling) {
        const auto stats = m_services.gpuCulling->GetDebugStats();
        contract.culling.statsValid = stats.valid;
        contract.culling.tested = stats.tested;
        contract.culling.frustumCulled = stats.frustumCulled;
        contract.culling.occluded = stats.occluded;
        contract.culling.visible = stats.visible;
    }

    contract.particles.enabled = m_particleState.enabledForScene;
    contract.particles.planned = featurePlan.runParticles;
    contract.particles.executed = m_particleState.frameExecuted;
    contract.particles.instanceMapFailed = m_particleState.instanceMapFailed;
    contract.particles.capped = m_particleState.frameCapped;
    contract.particles.densityScale = m_particleState.densityScale;
    contract.particles.qualityScale = m_particleState.qualityScale;
    contract.particles.bloomContribution = m_particleState.bloomContribution;
    contract.particles.softDepthFade = m_particleState.softDepthFade;
    contract.particles.windInfluence = m_particleState.windInfluence;
    contract.particles.effectPreset = m_particleState.effectPreset;
    contract.particles.presetMatchedEmitters = m_particleState.framePresetMatchedEmitters;
    contract.particles.presetMismatchedEmitters = m_particleState.framePresetMismatchedEmitters;
    contract.particles.emitterCount = m_particleState.frameEmitterCount;
    contract.particles.liveParticles = m_particleState.frameLiveParticles;
    contract.particles.submittedInstances = m_particleState.frameSubmittedInstances;
    contract.particles.frustumCulled = m_particleState.frameFrustumCulled;
    contract.particles.maxInstances = m_particleState.frameMaxInstances;
    contract.particles.instanceCapacity = m_particleState.instanceCapacity;
    contract.particles.instanceBufferBytes = m_particleState.InstanceBufferBytes();

    contract.water.levelY = m_waterState.levelY;
    contract.water.waveAmplitude = m_waterState.waveAmplitude;
    contract.water.waveLength = m_waterState.waveLength;
    contract.water.waveSpeed = m_waterState.waveSpeed;
    contract.water.secondaryAmplitude = m_waterState.secondaryAmplitude;
    contract.water.steepness = m_waterState.steepness;
    contract.water.roughness = m_waterState.roughness;
    contract.water.fresnelStrength = m_waterState.fresnelStrength;

    contract.vegetation.enabled = m_vegetationState.enabled;
    contract.vegetation.meshPipelineReady = static_cast<bool>(m_vegetationState.meshPipeline);
    contract.vegetation.billboardPipelineReady = static_cast<bool>(m_vegetationState.billboardPipeline);
    contract.vegetation.grassPipelineReady = static_cast<bool>(m_vegetationState.grassCardPipeline);
    contract.vegetation.shadowPipelineReady = static_cast<bool>(m_vegetationState.meshShadowPipeline);
    contract.vegetation.atlasLoaded = static_cast<bool>(m_vegetationState.atlas);
    contract.vegetation.totalInstances = m_vegetationState.stats.totalInstances;
    contract.vegetation.visibleInstances = m_vegetationState.stats.visibleInstances;
    contract.vegetation.meshInstances = m_vegetationState.meshInstances.count;
    contract.vegetation.billboardInstances = m_vegetationState.billboardInstances.count;
    contract.vegetation.grassInstances = m_vegetationState.grassInstances.count;
    contract.vegetation.meshCapacity = m_vegetationState.meshInstances.capacity;
    contract.vegetation.billboardCapacity = m_vegetationState.billboardInstances.capacity;
    contract.vegetation.grassCapacity = m_vegetationState.grassInstances.capacity;

    contract.cinematicPost.enabled = m_postProcessState.cinematicEnabled;
    contract.cinematicPost.postProcessPlanned = featurePlan.runPostProcess;
    contract.cinematicPost.postProcessExecuted = m_frameDiagnostics.timings.postMs > 0.0f;
    contract.cinematicPost.bloomPlanned = featurePlan.runBloom;
    contract.cinematicPost.bloomExecuted = m_frameDiagnostics.timings.bloomMs > 0.0f;
    contract.cinematicPost.bloomIntensity = m_bloomResources.intensity;
    contract.cinematicPost.bloomThreshold = m_bloomResources.threshold;
    contract.cinematicPost.bloomSoftKnee = m_bloomResources.softKnee;
    contract.cinematicPost.bloomMaxContribution = m_bloomResources.maxContribution;
    contract.cinematicPost.contrast = m_postProcessState.contrast;
    contract.cinematicPost.saturation = m_postProcessState.saturation;
    contract.cinematicPost.colorGradePreset = m_postProcessState.colorGradePreset;
    contract.cinematicPost.toneMapperPreset = m_postProcessState.toneMapperPreset;
    contract.cinematicPost.vignette = m_postProcessState.EffectiveVignette();
    contract.cinematicPost.lensDirt = m_postProcessState.EffectiveLensDirt();
    contract.cinematicPost.motionBlur = m_postProcessState.EffectiveMotionBlur();
    contract.cinematicPost.depthOfField = m_postProcessState.EffectiveDepthOfField();
    contract.cinematicPost.warm = m_postProcessState.warm;
    contract.cinematicPost.cool = m_postProcessState.cool;
    contract.cinematicPost.godRayIntensity = m_postProcessState.godRayIntensity;

    contract.motionVectors = m_frameDiagnostics.contract.motionVectors;
    contract.temporalMask = m_frameDiagnostics.contract.temporalMask;

    contract.rayTracing.supported = m_rtRuntimeState.supported;
    contract.rayTracing.enabled = m_rtRuntimeState.enabled;
    contract.rayTracing.warmingUp = IsRTWarmingUp();
    ApplyBudgetPlanToContract(contract.budget, m_framePlanning.budgetPlan);
    const auto assetMem = m_assetRuntime.registry.GetMemoryBreakdown();
    contract.assetMemory.textureBytes = assetMem.textureBytes;
    contract.assetMemory.environmentBytes = assetMem.environmentBytes;
    contract.assetMemory.geometryBytes = assetMem.geometryBytes;
    contract.assetMemory.rtStructureBytes = assetMem.rtStructureBytes;
    contract.assetMemory.textureBudgetExceeded =
        contract.budget.textureBudgetBytes > 0 && assetMem.textureBytes > contract.budget.textureBudgetBytes;
    contract.assetMemory.environmentBudgetExceeded =
        contract.budget.environmentBudgetBytes > 0 && assetMem.environmentBytes > contract.budget.environmentBudgetBytes;
    contract.assetMemory.geometryBudgetExceeded =
        contract.budget.geometryBudgetBytes > 0 && assetMem.geometryBytes > contract.budget.geometryBudgetBytes;
    contract.assetMemory.rtStructureBudgetExceeded =
        contract.budget.rtStructureBudgetBytes > 0 && assetMem.rtStructureBytes > contract.budget.rtStructureBudgetBytes;
    ApplyRTPlanToContract(contract.rayTracing, m_framePlanning.rtPlan);
    contract.rayTracing.denoiserExecuted = m_rtDenoiseState.executedThisFrame;
    contract.rayTracing.denoiserPasses = m_rtDenoiseState.passCountThisFrame;
    contract.rayTracing.denoiserUsesDepthNormalRejection = m_rtDenoiseState.usedDepthNormalRejectionThisFrame;
    contract.rayTracing.denoiserUsesVelocityReprojection = m_rtDenoiseState.usedVelocityThisFrame;
    contract.rayTracing.denoiserUsesDisocclusionRejection = m_rtDenoiseState.usedDisocclusionRejectionThisFrame;
    contract.rayTracing.shadowDenoiseAlpha = m_rtDenoiseState.shadowAlpha;
    contract.rayTracing.reflectionDenoiseAlpha = m_rtDenoiseState.reflectionAlpha;
    contract.rayTracing.giDenoiseAlpha = m_rtDenoiseState.giAlpha;
    contract.rayTracing.reflectionRequestedDenoiseAlpha = m_rtDenoiseState.reflectionHistoryAlpha;
    contract.rayTracing.reflectionCompositionStrength = m_rtDenoiseState.reflectionCompositionStrength;
    contract.rayTracing.reflectionRoughnessThreshold = m_rtDenoiseState.reflectionRoughnessThreshold;
    contract.rayTracing.reflectionHistoryMaxBlend = m_rtDenoiseState.reflectionHistoryMaxBlend;
    contract.rayTracing.reflectionFireflyClampLuma = m_rtDenoiseState.reflectionFireflyClampLuma;
    contract.rayTracing.reflectionSignalScale = m_rtDenoiseState.reflectionSignalScale;
    contract.rayTracing.giStrength = m_rtDenoiseState.giStrength;
    contract.rayTracing.giRayDistance = m_rtDenoiseState.giRayDistance;
    contract.rayTracing.reflectionDispatchReady = m_rtReflectionReadiness.ready;
    contract.rayTracing.reflectionHasPipeline = m_rtReflectionReadiness.hasPipeline;
    contract.rayTracing.reflectionHasTLAS = m_rtReflectionReadiness.hasTLAS;
    contract.rayTracing.reflectionHasMaterialBuffer = m_rtReflectionReadiness.hasMaterialBuffer;
    contract.rayTracing.reflectionHasOutput = m_rtReflectionReadiness.hasOutput;
    contract.rayTracing.reflectionHasDepth = m_rtReflectionReadiness.hasDepth;
    contract.rayTracing.reflectionHasNormalRoughness = m_rtReflectionReadiness.hasNormalRoughness;
    contract.rayTracing.reflectionHasMaterialExt2 = m_rtReflectionReadiness.hasMaterialExt2;
    contract.rayTracing.reflectionHasEnvironmentTable = m_rtReflectionReadiness.hasEnvironmentTable;
    contract.rayTracing.reflectionHasFrameConstants = m_rtReflectionReadiness.hasFrameConstants;
    contract.rayTracing.reflectionHasDispatchDescriptors = m_rtReflectionReadiness.hasDispatchDescriptors;
    contract.rayTracing.reflectionDispatchWidth = m_rtReflectionReadiness.dispatchWidth;
    contract.rayTracing.reflectionDispatchHeight = m_rtReflectionReadiness.dispatchHeight;
    contract.rayTracing.reflectionReadinessReason = m_rtReflectionReadiness.reason;
    contract.rayTracing.reflectionSignalStatsCaptured = m_rtReflectionSignalState.rawCapturedThisFrame;
    contract.rayTracing.reflectionSignalValid = m_rtReflectionSignalState.raw.valid;
    contract.rayTracing.reflectionSignalSampleFrame = m_rtReflectionSignalState.raw.sampleFrame;
    contract.rayTracing.reflectionSignalPixelCount = m_rtReflectionSignalState.raw.pixelCount;
    contract.rayTracing.reflectionSignalAvgLuma = m_rtReflectionSignalState.raw.avgLuma;
    contract.rayTracing.reflectionSignalMaxLuma = m_rtReflectionSignalState.raw.maxLuma;
    contract.rayTracing.reflectionSignalNonZeroRatio = m_rtReflectionSignalState.raw.nonZeroRatio;
    contract.rayTracing.reflectionSignalBrightRatio = m_rtReflectionSignalState.raw.brightRatio;
    contract.rayTracing.reflectionSignalOutlierRatio = m_rtReflectionSignalState.raw.outlierRatio;
    contract.rayTracing.reflectionSignalReadbackLatencyFrames = m_rtReflectionSignalState.raw.readbackLatencyFrames;
    contract.rayTracing.reflectionHistorySignalStatsCaptured =
        m_rtReflectionSignalState.historyCapturedThisFrame;
    contract.rayTracing.reflectionHistorySignalValid = m_rtReflectionSignalState.history.valid;
    contract.rayTracing.reflectionHistorySignalSampleFrame = m_rtReflectionSignalState.history.sampleFrame;
    contract.rayTracing.reflectionHistorySignalPixelCount = m_rtReflectionSignalState.history.pixelCount;
    contract.rayTracing.reflectionHistorySignalAvgLuma = m_rtReflectionSignalState.history.avgLuma;
    contract.rayTracing.reflectionHistorySignalMaxLuma = m_rtReflectionSignalState.history.maxLuma;
    contract.rayTracing.reflectionHistorySignalNonZeroRatio = m_rtReflectionSignalState.history.nonZeroRatio;
    contract.rayTracing.reflectionHistorySignalBrightRatio = m_rtReflectionSignalState.history.brightRatio;
    contract.rayTracing.reflectionHistorySignalOutlierRatio = m_rtReflectionSignalState.history.outlierRatio;
    contract.rayTracing.reflectionHistorySignalAvgLumaDelta = m_rtReflectionSignalState.history.avgLumaDelta;
    contract.rayTracing.reflectionHistorySignalReadbackLatencyFrames =
        m_rtReflectionSignalState.history.readbackLatencyFrames;
    contract.rayTracing.pendingBLAS = m_services.rayTracingContext ? m_services.rayTracingContext->GetPendingBLASCount() : 0;
    contract.rayTracing.pendingRendererBLASJobs = m_assetRuntime.gpuJobs.pendingBLASJobs;
    contract.rayTracing.tlasInstances = m_services.rayTracingContext ? m_services.rayTracingContext->GetLastTLASInstanceCount() : 0;
    contract.rayTracing.materialRecords = m_services.rayTracingContext ? m_services.rayTracingContext->GetLastRTMaterialCount() : 0;
    contract.rayTracing.materialBufferBytes = m_services.rayTracingContext ? m_services.rayTracingContext->GetRTMaterialBufferBytes() : 0;
    if (m_services.rayTracingContext) {
        const auto& tlasStats = m_services.rayTracingContext->GetLastTLASBuildStats();
        contract.rayTracing.tlasCandidates = tlasStats.candidates;
        contract.rayTracing.tlasSkippedInvalid = tlasStats.skippedInvisibleOrInvalid;
        contract.rayTracing.tlasMissingGeometry = tlasStats.missingGeometry;
        contract.rayTracing.tlasDistanceCulled = tlasStats.distanceCulled;
        contract.rayTracing.tlasBLASBuildRequested = tlasStats.blasBuildRequested;
        contract.rayTracing.tlasBLASBuildBudgetDeferred = tlasStats.blasBuildBudgetDeferred;
        contract.rayTracing.tlasBLASTotalBudgetSkipped = tlasStats.blasTotalBudgetSkipped;
        contract.rayTracing.tlasBLASBuildFailed = tlasStats.blasBuildFailed;
        contract.rayTracing.surfaceDefault = tlasStats.surfaceDefault;
        contract.rayTracing.surfaceGlass = tlasStats.surfaceGlass;
        contract.rayTracing.surfaceMirror = tlasStats.surfaceMirror;
        contract.rayTracing.surfacePlastic = tlasStats.surfacePlastic;
        contract.rayTracing.surfaceMasonry = tlasStats.surfaceMasonry;
        contract.rayTracing.surfaceEmissive = tlasStats.surfaceEmissive;
        contract.rayTracing.surfaceBrushedMetal = tlasStats.surfaceBrushedMetal;
        contract.rayTracing.surfaceWood = tlasStats.surfaceWood;
        contract.rayTracing.surfaceWater = tlasStats.surfaceWater;
    }
    contract.draws = m_frameDiagnostics.contract.drawCounts;
    if (contract.particles.executed && contract.draws.particleInstances > 0) {
        contract.particles.submittedInstances = contract.draws.particleInstances;
    }
    contract.passes = m_frameDiagnostics.contract.passRecords;
    contract.renderGraph = m_frameDiagnostics.renderGraph.info;
    contract.renderGraph.passRecords = 0;
    for (const auto& pass : m_frameDiagnostics.contract.passRecords) {
        if (pass.renderGraph) {
            ++contract.renderGraph.passRecords;
        }
    }

    m_frameDiagnostics.contract.contract = std::move(contract);
    UpdateFrameContractHistories();
    ValidateFrameContract();
}

} // namespace Cortex::Graphics
