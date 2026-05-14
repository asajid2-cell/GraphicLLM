#pragma once

#include "Scene/RendererBackpressure.h"
#include "Scene/SceneTransaction.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Scene {

enum class GeneratedAssetAdmissionDecision : uint8_t {
    Accept = 0,
    Degrade,
    Reject
};

struct GeneratedRuntimeAssetObligations {
    uint32_t texturePages = 0;
    uint64_t residentTextureBytes = 0;
    uint32_t psoSignatures = 0;
    uint32_t rtStateObjects = 0;
    uint32_t blasBuilds = 0;
    uint32_t tlasInstances = 0;
    uint32_t descriptors = 0;
    uint32_t probeCount = 0;
};

struct GeneratedAssetCandidate {
    std::string assetId;
    std::string sourceGenerator;
    std::string targetCapabilityTier;
    bool fallbackReady = false;
    bool canDegrade = false;
    float proceduralDensityScale = 1.0f;
    bool streamingReady = false;
    bool semanticValidationReady = false;
    bool rtAdmissionReady = false;
    GeneratedRuntimeAssetObligations obligations;
    SemanticBudget semanticBudget;
};

struct GeneratedAssetAdmissionReport {
    GeneratedAssetAdmissionDecision decision = GeneratedAssetAdmissionDecision::Reject;
    GeneratedAssetCandidate admittedCandidate;
    ProducerBudgetResponse backpressure;
    std::vector<std::string> errors;
};

[[nodiscard]] GeneratedAssetAdmissionReport AdmitGeneratedAsset(
    const GeneratedAssetCandidate& candidate,
    const RendererBackpressureSnapshot& snapshot);
[[nodiscard]] SceneTransaction BuildGeneratedAssetTransaction(
    const GeneratedAssetAdmissionReport& report);
[[nodiscard]] const char* ToString(GeneratedAssetAdmissionDecision decision);
[[nodiscard]] std::string RunGeneratedAssetAdmissionSelfTestJson();

} // namespace Cortex::Scene
