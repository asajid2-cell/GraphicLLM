#include "Renderer.h"

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

void Renderer::InvalidateTAAHistory(const char* reason) {
    m_temporalHistory.manager.Invalidate(TemporalHistoryId::TAAColor, reason, m_frameLifecycle.renderFrameCounter);
}

void Renderer::MarkTAAHistoryValid() {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = "taa_resolve";
    desc.accumulationAlpha = m_temporalAAState.blendFactor;
    desc.usedVelocityReprojection = true;
    desc.usedDisocclusionRejection = true;
    m_temporalHistory.manager.MarkValid(TemporalHistoryId::TAAColor, m_frameLifecycle.renderFrameCounter, desc);
}

void Renderer::InvalidateRTShadowHistory(const char* reason) {
    m_temporalHistory.manager.Invalidate(TemporalHistoryId::RTShadow, reason, m_frameLifecycle.renderFrameCounter);
}

void Renderer::MarkRTShadowHistoryValid() {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_rtDenoiseState.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_rtDenoiseState.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_rtDenoiseState.shadowAlpha;
    desc.usedDepthNormalRejection = m_rtDenoiseState.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_rtDenoiseState.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_rtDenoiseState.usedDisocclusionRejectionThisFrame;
    m_temporalHistory.manager.MarkValid(TemporalHistoryId::RTShadow, m_frameLifecycle.renderFrameCounter, desc);
}

void Renderer::InvalidateRTReflectionHistory(const char* reason) {
    m_temporalHistory.manager.Invalidate(TemporalHistoryId::RTReflection, reason, m_frameLifecycle.renderFrameCounter);
}

void Renderer::MarkRTReflectionHistoryValid() {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_rtDenoiseState.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_rtDenoiseState.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_rtDenoiseState.reflectionAlpha;
    desc.usedDepthNormalRejection = m_rtDenoiseState.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_rtDenoiseState.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_rtDenoiseState.usedDisocclusionRejectionThisFrame;
    m_temporalHistory.manager.MarkValid(TemporalHistoryId::RTReflection, m_frameLifecycle.renderFrameCounter, desc);
}

void Renderer::InvalidateRTGIHistory(const char* reason) {
    m_temporalHistory.manager.Invalidate(TemporalHistoryId::RTGI, reason, m_frameLifecycle.renderFrameCounter);
}

void Renderer::MarkRTGIHistoryValid() {
    TemporalMarkValidDesc desc{};
    desc.rejectionMode = m_rtDenoiseState.usedDisocclusionRejectionThisFrame ? "disocclusion_depth_normal_velocity" :
        (m_rtDenoiseState.usedDepthNormalRejectionThisFrame ? "depth_normal_velocity" : "copy_seed");
    desc.accumulationAlpha = m_rtDenoiseState.giAlpha;
    desc.usedDepthNormalRejection = m_rtDenoiseState.usedDepthNormalRejectionThisFrame;
    desc.usedVelocityReprojection = m_rtDenoiseState.usedVelocityThisFrame;
    desc.usedDisocclusionRejection = m_rtDenoiseState.usedDisocclusionRejectionThisFrame;
    m_temporalHistory.manager.MarkValid(TemporalHistoryId::RTGI, m_frameLifecycle.renderFrameCounter, desc);
}

void Renderer::UpdateFrameContractHistories() {
    auto historySize = [](ID3D12Resource* resource, uint32_t& width, uint32_t& height) {
        width = 0;
        height = 0;
        if (!resource) {
            return false;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            return false;
        }
        width = static_cast<uint32_t>(desc.Width);
        height = desc.Height;
        return true;
    };

    m_frameDiagnostics.contract.contract.histories.clear();
    auto addHistory = [&](TemporalHistoryId id,
                          ID3D12Resource* resource,
                          bool featureActive) {
        const auto& temporal = m_temporalHistory.manager.Get(id);
        FrameContract::HistoryInfo info{};
        info.name = temporal.name ? temporal.name : "";
        info.valid = temporal.valid && resource != nullptr && featureActive;
        info.resourceValid = historySize(resource, info.width, info.height);
        info.lastValidFrame = temporal.lastValidFrame;
        info.ageFrames = (info.valid && m_frameLifecycle.renderFrameCounter >= temporal.lastValidFrame)
            ? (m_frameLifecycle.renderFrameCounter - temporal.lastValidFrame)
            : 0;
        info.lastResetReason = temporal.lastResetReason;
        info.seeded = temporal.seeded;
        info.lastInvalidatedFrame = temporal.lastInvalidatedFrame;
        info.rejectionMode = temporal.rejectionMode;
        info.accumulationAlpha = temporal.accumulationAlpha;
        info.usesDepthNormalRejection = temporal.usedDepthNormalRejection;
        info.usesVelocityReprojection = temporal.usedVelocityReprojection;
        info.usesDisocclusionRejection = temporal.usedDisocclusionRejection;
        if (!featureActive) {
            info.invalidReason = "feature_inactive";
        } else if (!info.resourceValid) {
            info.invalidReason = "resource_missing";
        } else if (!temporal.valid) {
            info.invalidReason = temporal.invalidReason.empty() ? "not_seeded" : temporal.invalidReason;
        }
        m_frameDiagnostics.contract.contract.histories.push_back(std::move(info));
    };

    addHistory(TemporalHistoryId::TAAColor, m_temporalScreenState.historyColor.Get(), m_frameDiagnostics.contract.contract.features.taaEnabled);
    addHistory(TemporalHistoryId::RTShadow, m_rtShadowTargets.history.Get(), m_frameDiagnostics.contract.contract.features.rayTracingEnabled);
    addHistory(TemporalHistoryId::RTReflection, m_rtReflectionTargets.history.Get(),
               m_frameDiagnostics.contract.contract.features.rayTracingEnabled && m_frameDiagnostics.contract.contract.features.rtReflectionsEnabled);
    addHistory(TemporalHistoryId::RTGI, m_rtGITargets.history.Get(),
               m_frameDiagnostics.contract.contract.features.rayTracingEnabled && m_frameDiagnostics.contract.contract.features.rtGIEnabled);

    m_frameDiagnostics.contract.contract.draws = m_frameDiagnostics.contract.drawCounts;
    m_frameDiagnostics.contract.contract.motionVectors = m_frameDiagnostics.contract.motionVectors;
    m_frameDiagnostics.contract.temporalMask.built = m_temporalMaskState.builtThisFrame;
    m_frameDiagnostics.contract.contract.temporalMask = m_frameDiagnostics.contract.temporalMask;
    m_frameDiagnostics.contract.contract.passes = m_frameDiagnostics.contract.passRecords;
    m_frameDiagnostics.contract.contract.renderGraph = m_frameDiagnostics.renderGraph.info;
    m_frameDiagnostics.contract.contract.renderGraph.passRecords = 0;
    for (const auto& pass : m_frameDiagnostics.contract.passRecords) {
        if (pass.renderGraph) {
            ++m_frameDiagnostics.contract.contract.renderGraph.passRecords;
        }
    }
    if (m_frameDiagnostics.contract.contract.renderGraph.passRecords > 0 && !m_frameDiagnostics.contract.contract.renderGraph.active) {
        m_frameDiagnostics.contract.contract.warnings.push_back("render_graph_pass_records_without_active_graph");
    }
    if (m_frameDiagnostics.contract.contract.renderGraph.fallbackExecutions > 0) {
        m_frameDiagnostics.contract.contract.warnings.push_back(
            "render_graph_fallback_executions:" +
            std::to_string(m_frameDiagnostics.contract.contract.renderGraph.fallbackExecutions));
    }

    if (m_framePlanning.sceneSnapshot.IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        const uint32_t snapshotDepthWriting =
            static_cast<uint32_t>(m_framePlanning.sceneSnapshot.depthWritingIndices.size());
        if (m_visibilityBufferState.renderedThisFrame &&
            m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances != snapshotDepthWriting) {
            m_frameDiagnostics.contract.contract.warnings.push_back(
                "visibility_buffer_instance_count_mismatch: snapshot_depth_writing=" +
                std::to_string(snapshotDepthWriting) +
                " vb_instances=" +
                std::to_string(m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances));
        }
        if (m_services.gpuCulling && m_services.gpuCulling->GetTotalInstances() > 0 &&
            m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances > 0 &&
            m_services.gpuCulling->GetTotalInstances() != m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances) {
            m_frameDiagnostics.contract.contract.warnings.push_back(
                "gpu_culling_instance_count_mismatch: gpu_instances=" +
                std::to_string(m_services.gpuCulling->GetTotalInstances()) +
                " vb_instances=" +
                std::to_string(m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances));
        }
    }

    // Refresh end-of-frame dynamic visibility state. The full snapshot is
    // taken before pass execution, but these fields are only known after the
    // visibility path, HZB builder, and culling dispatches have run.
    m_frameDiagnostics.contract.contract.culling.gpuCullingEnabled = m_gpuCullingState.enabled;
    m_frameDiagnostics.contract.contract.culling.cullingFrozen =
        m_gpuCullingState.freeze || (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr);
    m_frameDiagnostics.contract.contract.culling.visibilityBufferPlanned = m_visibilityBufferState.plannedThisFrame;
    m_frameDiagnostics.contract.contract.culling.visibilityBufferRendered = m_visibilityBufferState.renderedThisFrame;
    m_frameDiagnostics.contract.contract.culling.hzbResourceValid = m_hzbResources.texture != nullptr;
    m_frameDiagnostics.contract.contract.culling.hzbValid = m_hzbResources.valid;
    m_frameDiagnostics.contract.contract.culling.hzbCaptureValid = m_hzbResources.captureValid;
    m_frameDiagnostics.contract.contract.culling.hzbOcclusionUsedByVisibilityBuffer = m_visibilityBufferState.hzbOcclusionUsedThisFrame;
    m_frameDiagnostics.contract.contract.culling.hzbOcclusionUsedByGpuCulling = m_gpuCullingState.hzbOcclusionUsedThisFrame;
    m_frameDiagnostics.contract.contract.culling.hzbWidth = m_hzbResources.width;
    m_frameDiagnostics.contract.contract.culling.hzbHeight = m_hzbResources.height;
    m_frameDiagnostics.contract.contract.culling.hzbMipCount = m_hzbResources.mipCount;
    m_frameDiagnostics.contract.contract.culling.hzbCaptureFrame = m_hzbResources.captureFrameCounter;
    m_frameDiagnostics.contract.contract.culling.hzbAgeFrames =
        (m_hzbResources.captureValid && m_frameLifecycle.renderFrameCounter >= m_hzbResources.captureFrameCounter)
            ? (m_frameLifecycle.renderFrameCounter - m_hzbResources.captureFrameCounter)
            : 0;
    if (m_services.gpuCulling) {
        const auto stats = m_services.gpuCulling->GetDebugStats();
        m_frameDiagnostics.contract.contract.culling.statsValid = stats.valid;
        m_frameDiagnostics.contract.contract.culling.tested = stats.tested;
        m_frameDiagnostics.contract.contract.culling.frustumCulled = stats.frustumCulled;
        m_frameDiagnostics.contract.contract.culling.occluded = stats.occluded;
        m_frameDiagnostics.contract.contract.culling.visible = stats.visible;
    }

    m_frameDiagnostics.contract.contract.rayTracing.supported = m_rtRuntimeState.supported;
    m_frameDiagnostics.contract.contract.rayTracing.enabled = m_rtRuntimeState.enabled;
    m_frameDiagnostics.contract.contract.rayTracing.warmingUp = IsRTWarmingUp();
    ApplyBudgetPlanToContract(m_frameDiagnostics.contract.contract.budget, m_framePlanning.budgetPlan);
    ApplyRTPlanToContract(m_frameDiagnostics.contract.contract.rayTracing, m_framePlanning.rtPlan);
    m_frameDiagnostics.contract.contract.rayTracing.denoiserExecuted = m_rtDenoiseState.executedThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.denoiserPasses = m_rtDenoiseState.passCountThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.denoiserUsesDepthNormalRejection = m_rtDenoiseState.usedDepthNormalRejectionThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.denoiserUsesVelocityReprojection = m_rtDenoiseState.usedVelocityThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.denoiserUsesDisocclusionRejection = m_rtDenoiseState.usedDisocclusionRejectionThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.shadowDenoiseAlpha = m_rtDenoiseState.shadowAlpha;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionDenoiseAlpha = m_rtDenoiseState.reflectionAlpha;
    m_frameDiagnostics.contract.contract.rayTracing.giDenoiseAlpha = m_rtDenoiseState.giAlpha;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionRequestedDenoiseAlpha = m_rtDenoiseState.reflectionHistoryAlpha;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionCompositionStrength = m_rtDenoiseState.reflectionCompositionStrength;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionDispatchReady = m_rtReflectionReadiness.ready;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasPipeline = m_rtReflectionReadiness.hasPipeline;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasTLAS = m_rtReflectionReadiness.hasTLAS;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasMaterialBuffer = m_rtReflectionReadiness.hasMaterialBuffer;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasOutput = m_rtReflectionReadiness.hasOutput;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasDepth = m_rtReflectionReadiness.hasDepth;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasNormalRoughness = m_rtReflectionReadiness.hasNormalRoughness;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasMaterialExt2 = m_rtReflectionReadiness.hasMaterialExt2;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasEnvironmentTable = m_rtReflectionReadiness.hasEnvironmentTable;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasFrameConstants = m_rtReflectionReadiness.hasFrameConstants;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHasDispatchDescriptors = m_rtReflectionReadiness.hasDispatchDescriptors;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionDispatchWidth = m_rtReflectionReadiness.dispatchWidth;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionDispatchHeight = m_rtReflectionReadiness.dispatchHeight;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionReadinessReason = m_rtReflectionReadiness.reason;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalStatsCaptured = m_rtReflectionSignalState.rawCapturedThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalValid = m_rtReflectionSignalState.raw.valid;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalSampleFrame = m_rtReflectionSignalState.raw.sampleFrame;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalPixelCount = m_rtReflectionSignalState.raw.pixelCount;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalAvgLuma = m_rtReflectionSignalState.raw.avgLuma;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalMaxLuma = m_rtReflectionSignalState.raw.maxLuma;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalNonZeroRatio = m_rtReflectionSignalState.raw.nonZeroRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalBrightRatio = m_rtReflectionSignalState.raw.brightRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalOutlierRatio = m_rtReflectionSignalState.raw.outlierRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionSignalReadbackLatencyFrames = m_rtReflectionSignalState.raw.readbackLatencyFrames;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalStatsCaptured =
        m_rtReflectionSignalState.historyCapturedThisFrame;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalValid = m_rtReflectionSignalState.history.valid;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalSampleFrame = m_rtReflectionSignalState.history.sampleFrame;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalPixelCount = m_rtReflectionSignalState.history.pixelCount;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalAvgLuma = m_rtReflectionSignalState.history.avgLuma;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalMaxLuma = m_rtReflectionSignalState.history.maxLuma;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalNonZeroRatio = m_rtReflectionSignalState.history.nonZeroRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalBrightRatio = m_rtReflectionSignalState.history.brightRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalOutlierRatio = m_rtReflectionSignalState.history.outlierRatio;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalAvgLumaDelta = m_rtReflectionSignalState.history.avgLumaDelta;
    m_frameDiagnostics.contract.contract.rayTracing.reflectionHistorySignalReadbackLatencyFrames =
        m_rtReflectionSignalState.history.readbackLatencyFrames;
    m_frameDiagnostics.contract.contract.rayTracing.pendingBLAS = m_services.rayTracingContext ? m_services.rayTracingContext->GetPendingBLASCount() : 0;
    m_frameDiagnostics.contract.contract.rayTracing.pendingRendererBLASJobs = m_assetRuntime.gpuJobs.pendingBLASJobs;
    m_frameDiagnostics.contract.contract.rayTracing.tlasInstances =
        m_services.rayTracingContext ? m_services.rayTracingContext->GetLastTLASInstanceCount() : 0;
    m_frameDiagnostics.contract.contract.rayTracing.materialRecords =
        m_services.rayTracingContext ? m_services.rayTracingContext->GetLastRTMaterialCount() : 0;
    m_frameDiagnostics.contract.contract.rayTracing.materialBufferBytes =
        m_services.rayTracingContext ? m_services.rayTracingContext->GetRTMaterialBufferBytes() : 0;
    if (m_services.rayTracingContext) {
        const auto& tlasStats = m_services.rayTracingContext->GetLastTLASBuildStats();
        m_frameDiagnostics.contract.contract.rayTracing.tlasCandidates = tlasStats.candidates;
        m_frameDiagnostics.contract.contract.rayTracing.tlasSkippedInvalid = tlasStats.skippedInvisibleOrInvalid;
        m_frameDiagnostics.contract.contract.rayTracing.tlasMissingGeometry = tlasStats.missingGeometry;
        m_frameDiagnostics.contract.contract.rayTracing.tlasDistanceCulled = tlasStats.distanceCulled;
        m_frameDiagnostics.contract.contract.rayTracing.tlasBLASBuildRequested = tlasStats.blasBuildRequested;
        m_frameDiagnostics.contract.contract.rayTracing.tlasBLASBuildBudgetDeferred = tlasStats.blasBuildBudgetDeferred;
        m_frameDiagnostics.contract.contract.rayTracing.tlasBLASTotalBudgetSkipped = tlasStats.blasTotalBudgetSkipped;
        m_frameDiagnostics.contract.contract.rayTracing.tlasBLASBuildFailed = tlasStats.blasBuildFailed;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceDefault = tlasStats.surfaceDefault;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceGlass = tlasStats.surfaceGlass;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceMirror = tlasStats.surfaceMirror;
        m_frameDiagnostics.contract.contract.rayTracing.surfacePlastic = tlasStats.surfacePlastic;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceMasonry = tlasStats.surfaceMasonry;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceEmissive = tlasStats.surfaceEmissive;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceBrushedMetal = tlasStats.surfaceBrushedMetal;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceWood = tlasStats.surfaceWood;
        m_frameDiagnostics.contract.contract.rayTracing.surfaceWater = tlasStats.surfaceWater;
    }
    if (m_framePlanning.sceneSnapshot.IsValidForFrame(m_frameLifecycle.renderFrameCounter) &&
        m_frameDiagnostics.contract.contract.rayTracing.tlasCandidates > 0 &&
        m_frameDiagnostics.contract.contract.rayTracing.tlasCandidates != m_framePlanning.sceneSnapshot.rtCandidateIndices.size()) {
        m_frameDiagnostics.contract.contract.warnings.push_back(
            "rt_tlas_candidate_count_mismatch: snapshot_rt_candidates=" +
            std::to_string(m_framePlanning.sceneSnapshot.rtCandidateIndices.size()) +
            " tlas_candidates=" +
            std::to_string(m_frameDiagnostics.contract.contract.rayTracing.tlasCandidates));
    }
}

} // namespace Cortex::Graphics
