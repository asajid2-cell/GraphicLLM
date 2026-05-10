#pragma once

#include <cstdint>
#include <string>

#include "Graphics/BudgetPlanner.h"

namespace Cortex::Graphics {

enum class RTBudgetProfile : uint32_t {
    UltraLow2GB = 0,
    Low4GB,
    Balanced8GB,
    High
};

struct RTBudget {
    RTBudgetProfile profile = RTBudgetProfile::Balanced8GB;
    std::string profileName = "8gb_balanced";
    uint64_t dedicatedVideoMemoryBytes = 0;
    uint64_t maxBLASBuildBytesPerFrame = 256ull * 1024ull * 1024ull;
    uint64_t maxBLASTotalBytes = 1024ull * 1024ull * 1024ull;
    uint32_t maxTLASInstances = 4096;
    uint32_t reflectionWidth = 0;
    uint32_t reflectionHeight = 0;
    uint32_t giWidth = 0;
    uint32_t giHeight = 0;
    uint32_t reflectionUpdateCadence = 1;
    uint32_t giUpdateCadence = 1;
    bool denoiseShadows = true;
    bool denoiseReflections = true;
    bool denoiseGI = true;
};

struct RTSchedulerInputs {
    uint64_t frameNumber = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    uint32_t currentReflectionWidth = 0;
    uint32_t currentReflectionHeight = 0;
    uint32_t currentGIWidth = 0;
    uint32_t currentGIHeight = 0;
    uint32_t tlasCandidateCount = 0;
    uint32_t pendingRendererBLASJobs = 0;
    uint32_t pendingContextBLAS = 0;
    RendererBudgetPlan rendererBudget{};
    bool rendererBudgetValid = false;
    bool requested = false;
    bool supported = false;
    bool contextReady = false;
    bool warmingUp = false;
    bool shadowPipelineReady = false;
    bool reflectionFeatureRequested = false;
    bool reflectionPipelineReady = false;
    bool reflectionResourceReady = false;
    bool giFeatureRequested = false;
    bool giPipelineReady = false;
    bool giResourceReady = false;
    bool depthReady = false;
    bool shadowMaskReady = false;
};

struct RTFramePlan {
    RTBudget budget{};
    RendererBudgetPlan rendererBudget{};
    uint64_t frameNumber = 0;
    bool requested = false;
    bool supported = false;
    bool enabled = false;
    bool warmingUp = false;
    bool buildTLAS = false;
    bool dispatchShadows = false;
    bool dispatchReflections = false;
    bool dispatchGI = false;
    bool denoiseShadows = false;
    bool denoiseReflections = false;
    bool denoiseGI = false;
    uint32_t reflectionFramePhase = 0;
    uint32_t giFramePhase = 0;
    uint32_t tlasCandidateCount = 0;
    uint32_t pendingRendererBLASJobs = 0;
    uint32_t pendingContextBLAS = 0;
    std::string disabledReason;
};

class RTScheduler {
public:
    [[nodiscard]] static RTBudget BuildBudget(uint64_t dedicatedVideoMemoryBytes,
                                              uint32_t renderWidth,
                                              uint32_t renderHeight,
                                              uint32_t currentReflectionWidth,
                                              uint32_t currentReflectionHeight,
                                              uint32_t currentGIWidth,
                                              uint32_t currentGIHeight);
    [[nodiscard]] static RTFramePlan BuildFramePlan(const RTSchedulerInputs& inputs);
};

} // namespace Cortex::Graphics
