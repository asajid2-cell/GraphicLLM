#pragma once

#include "Scene/SemanticGraph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Scene {

struct SceneResourceDiff {
    uint64_t textureBytes = 0;
    uint32_t texturePages = 0;
    uint32_t descriptorCount = 0;
    uint32_t psoSignatures = 0;
    uint32_t blasBuilds = 0;
    uint32_t tlasInstances = 0;
    std::vector<std::string> resourceIds;
};

struct SceneFeatureTier {
    std::string feature;
    std::string tier;
    bool fallbackReady = false;
};

struct SceneValidationCamera {
    std::string id;
    std::string semanticRegion;
    std::string purpose;
};

struct SceneTransactionProvenance {
    std::string prompt;
    uint64_t seed = 0;
    std::string generator;
    std::string sourceAsset;
    std::string validationReport;
    std::string commitId;

    [[nodiscard]] bool Complete() const;
};

struct SceneTransaction {
    std::string id;
    std::vector<std::string> entityDiff;
    SemanticGraphDiff semanticGraphDiff;
    SceneResourceDiff resourceDiff;
    SemanticBudget rendererBudgetDelta;
    std::vector<SceneFeatureTier> requiredFeatureTiers;
    SemanticInvalidation historyInvalidation;
    std::vector<SceneValidationCamera> validationCameras;
    SceneTransactionProvenance provenance;
    struct TemporalUpscalingContract {
        bool required = false;
        bool motionVectorsValid = false;
        bool exposureValid = false;
        bool reactiveMaskValid = false;
        bool generatedObjectInvalidation = false;
        bool dynamicObjectInvalidation = false;
    } temporalUpscalingContract;
    struct SemanticVisualPolicy {
        bool requireSupportValidation = false;
        bool requireForegroundMidgroundBackground = false;
        bool requireMaterialDiversity = false;
        bool requireValidationCameraPerDirtyRegion = false;
        bool requireRegressionCorpus = false;
        uint32_t minDistinctMaterialIntents = 0;
        SemanticBudget budgetLimit;
        std::vector<std::string> regressionCases;
    } semanticVisualPolicy;
};

struct SceneTransactionValidationResult {
    bool accepted = false;
    std::vector<std::string> errors;
};

struct SceneTransactionReceipt {
    bool committed = false;
    std::string transactionId;
    SemanticGraphDiff rollbackDiff;
};

class SceneTransactionValidator {
public:
    explicit SceneTransactionValidator(SemanticBudget budgetLimit);

    [[nodiscard]] SceneTransactionValidationResult Validate(const SceneTransaction& transaction,
                                                            const SemanticSceneGraph& graph) const;
    [[nodiscard]] SceneTransactionValidationResult Preview(const SceneTransaction& transaction,
                                                           const SemanticSceneGraph& graph,
                                                           std::vector<SemanticRuntimeObjectPlan>* outPlan) const;
    [[nodiscard]] SceneTransactionReceipt Commit(const SceneTransaction& transaction,
                                                 SemanticSceneGraph& graph,
                                                 SceneTransactionValidationResult* outValidation) const;
    [[nodiscard]] bool Rollback(const SceneTransactionReceipt& receipt,
                                SemanticSceneGraph& graph,
                                std::string* error = nullptr) const;

private:
    SemanticBudget m_budgetLimit;
};

[[nodiscard]] std::string RunSceneTransactionSelfTestJson();
[[nodiscard]] std::string RunSemanticVisualValidationSelfTestJson();

} // namespace Cortex::Scene
