#pragma once

#include "Graphics/FrameContract.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Scene {

enum class ProducerBudgetDecision : uint8_t {
    Accept = 0,
    Degrade,
    Reject
};

struct RendererBackpressureSnapshot {
    uint64_t availableTextureBytes = 0;
    uint64_t availableGeometryBytes = 0;
    uint64_t availableRTStructureBytes = 0;
    uint32_t availablePersistentDescriptors = 0;
    uint32_t availableTransientDescriptors = 0;
    uint32_t availableTLASInstances = 0;
    uint32_t pendingBLAS = 0;
    uint32_t pendingRendererBLASJobs = 0;
    uint64_t uploadBytesThisFrame = 0;
    double passEstimatedWriteMBTotal = 0.0;
    uint32_t rayTracingPasses = 0;
    uint32_t validationCameraFailures = 0;
};

struct ProducerBudgetRequest {
    std::string producerId;
    std::string contentId;
    uint64_t textureBytes = 0;
    uint64_t geometryBytes = 0;
    uint64_t rtStructureBytes = 0;
    uint32_t persistentDescriptors = 0;
    uint32_t transientDescriptors = 0;
    uint32_t tlasInstances = 0;
    uint32_t blasBuilds = 0;
    uint64_t uploadBytes = 0;
    double estimatedWriteMB = 0.0;
    uint32_t validationCameraCount = 0;
    bool canDegrade = false;
};

struct ProducerBudgetResponse {
    ProducerBudgetDecision decision = ProducerBudgetDecision::Reject;
    ProducerBudgetRequest admittedRequest;
    std::vector<std::string> reasons;
};

[[nodiscard]] RendererBackpressureSnapshot BuildRendererBackpressureSnapshot(
    const Graphics::FrameContract& contract);
[[nodiscard]] ProducerBudgetResponse EvaluateProducerBudgetRequest(
    const RendererBackpressureSnapshot& snapshot,
    const ProducerBudgetRequest& request);
[[nodiscard]] const char* ToString(ProducerBudgetDecision decision);
[[nodiscard]] std::string RunRendererBackpressureSelfTestJson();

} // namespace Cortex::Scene
