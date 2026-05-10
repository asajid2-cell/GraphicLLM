#include "Graphics/RTScheduler.h"

#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

namespace {

uint32_t HalfOrFallback(uint32_t current, uint32_t renderDimension) {
    if (current > 0) {
        return current;
    }
    return std::max(1u, renderDimension / 2u);
}

RTBudgetProfile ToRTProfile(RendererBudgetProfile profile) {
    switch (profile) {
    case RendererBudgetProfile::UltraLow2GB: return RTBudgetProfile::UltraLow2GB;
    case RendererBudgetProfile::Low4GB: return RTBudgetProfile::Low4GB;
    case RendererBudgetProfile::High: return RTBudgetProfile::High;
    case RendererBudgetProfile::Balanced8GB:
    default: return RTBudgetProfile::Balanced8GB;
    }
}

void ClampReducedResolution(RTBudget& budget, uint32_t maxWidth, uint32_t maxHeight) {
    budget.reflectionWidth = std::min(budget.reflectionWidth, maxWidth);
    budget.reflectionHeight = std::min(budget.reflectionHeight, maxHeight);
    budget.giWidth = std::min(budget.giWidth, maxWidth);
    budget.giHeight = std::min(budget.giHeight, maxHeight);
}

void ApplyRendererBudget(RTBudget& budget,
                         const RendererBudgetPlan& rendererBudget,
                         uint32_t renderWidth,
                         uint32_t renderHeight) {
    budget.profile = ToRTProfile(rendererBudget.profile);
    budget.profileName = rendererBudget.profileName;
    budget.dedicatedVideoMemoryBytes = rendererBudget.dedicatedVideoMemoryBytes;
    budget.maxBLASBuildBytesPerFrame = rendererBudget.maxBLASBuildBytesPerFrame;
    budget.maxBLASTotalBytes = rendererBudget.maxBLASTotalBytes;
    budget.maxTLASInstances = rendererBudget.maxTLASInstances;
    budget.reflectionUpdateCadence = rendererBudget.reflectionUpdateCadence;
    budget.giUpdateCadence = rendererBudget.giUpdateCadence;
    budget.denoiseShadows = rendererBudget.denoiseShadows;
    budget.denoiseReflections = rendererBudget.denoiseReflections;
    budget.denoiseGI = rendererBudget.denoiseGI;

    const uint32_t maxRTWidth =
        std::max(1u, static_cast<uint32_t>(std::floor(static_cast<float>(std::max(1u, renderWidth)) *
                                                     rendererBudget.rtResolutionScale)));
    const uint32_t maxRTHeight =
        std::max(1u, static_cast<uint32_t>(std::floor(static_cast<float>(std::max(1u, renderHeight)) *
                                                     rendererBudget.rtResolutionScale)));
    ClampReducedResolution(budget, maxRTWidth, maxRTHeight);
}

} // namespace

RTBudget RTScheduler::BuildBudget(uint64_t dedicatedVideoMemoryBytes,
                                  uint32_t renderWidth,
                                  uint32_t renderHeight,
                                  uint32_t currentReflectionWidth,
                                  uint32_t currentReflectionHeight,
                                  uint32_t currentGIWidth,
                                  uint32_t currentGIHeight) {
    RTBudget budget{};
    budget.dedicatedVideoMemoryBytes = dedicatedVideoMemoryBytes;
    budget.reflectionWidth = HalfOrFallback(currentReflectionWidth, renderWidth);
    budget.reflectionHeight = HalfOrFallback(currentReflectionHeight, renderHeight);
    budget.giWidth = HalfOrFallback(currentGIWidth, renderWidth);
    budget.giHeight = HalfOrFallback(currentGIHeight, renderHeight);

    const RendererBudgetPlan rendererBudget =
        BudgetPlanner::BuildPlan(dedicatedVideoMemoryBytes, renderWidth, renderHeight);
    ApplyRendererBudget(budget, rendererBudget, renderWidth, renderHeight);
    return budget;
}

RTFramePlan RTScheduler::BuildFramePlan(const RTSchedulerInputs& inputs) {
    RTFramePlan plan{};
    plan.frameNumber = inputs.frameNumber;
    plan.requested = inputs.requested;
    plan.supported = inputs.supported;
    plan.warmingUp = inputs.warmingUp;
    plan.tlasCandidateCount = inputs.tlasCandidateCount;
    plan.pendingRendererBLASJobs = inputs.pendingRendererBLASJobs;
    plan.pendingContextBLAS = inputs.pendingContextBLAS;
    plan.rendererBudget = inputs.rendererBudgetValid
        ? inputs.rendererBudget
        : BudgetPlanner::BuildPlan(inputs.dedicatedVideoMemoryBytes, inputs.renderWidth, inputs.renderHeight);

    plan.budget.dedicatedVideoMemoryBytes = inputs.dedicatedVideoMemoryBytes;
    plan.budget.reflectionWidth = HalfOrFallback(inputs.currentReflectionWidth, inputs.renderWidth);
    plan.budget.reflectionHeight = HalfOrFallback(inputs.currentReflectionHeight, inputs.renderHeight);
    plan.budget.giWidth = HalfOrFallback(inputs.currentGIWidth, inputs.renderWidth);
    plan.budget.giHeight = HalfOrFallback(inputs.currentGIHeight, inputs.renderHeight);
    ApplyRendererBudget(plan.budget, plan.rendererBudget, inputs.renderWidth, inputs.renderHeight);

    if (!inputs.requested) {
        plan.disabledReason = "not_requested";
        return plan;
    }
    if (!inputs.supported) {
        plan.disabledReason = "dxr_not_supported";
        return plan;
    }
    if (!inputs.contextReady) {
        plan.disabledReason = "context_missing";
        return plan;
    }

    plan.enabled = true;
    plan.buildTLAS = inputs.tlasCandidateCount > 0;
    plan.dispatchShadows =
        inputs.shadowPipelineReady &&
        inputs.depthReady &&
        inputs.shadowMaskReady &&
        plan.buildTLAS;

    const uint32_t reflectionCadence = std::max(1u, plan.budget.reflectionUpdateCadence);
    const uint32_t giCadence = std::max(1u, plan.budget.giUpdateCadence);
    plan.reflectionFramePhase = static_cast<uint32_t>(inputs.frameNumber % reflectionCadence);
    plan.giFramePhase = static_cast<uint32_t>(inputs.frameNumber % giCadence);

    plan.dispatchReflections =
        inputs.reflectionFeatureRequested &&
        inputs.reflectionPipelineReady &&
        inputs.reflectionResourceReady &&
        inputs.depthReady &&
        plan.buildTLAS &&
        plan.reflectionFramePhase == 0;
    plan.dispatchGI =
        inputs.giFeatureRequested &&
        inputs.giPipelineReady &&
        inputs.giResourceReady &&
        inputs.depthReady &&
        plan.buildTLAS &&
        plan.giFramePhase == 0;

    plan.denoiseShadows = plan.budget.denoiseShadows && plan.dispatchShadows;
    plan.denoiseReflections = plan.budget.denoiseReflections && plan.dispatchReflections;
    plan.denoiseGI = plan.budget.denoiseGI && plan.dispatchGI;
    return plan;
}

} // namespace Cortex::Graphics
