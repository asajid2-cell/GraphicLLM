#pragma once

#include "Scene/SceneTransaction.h"
#include "Scene/SemanticRuntimeCompiler.h"

namespace Cortex::Scene {

struct SceneRuntimeMutationState {
    SemanticSceneGraph graph;
    std::vector<SemanticEcsEntityJob> ecsEntityJobs;
    std::vector<SemanticRendererResourceJob> rendererResourceJobs;
    SemanticInvalidation frameInvalidation;
};

struct SceneRuntimeMutationReceipt {
    bool committed = false;
    SceneTransactionReceipt graphReceipt;
    size_t ecsJobCountBefore = 0;
    size_t rendererJobCountBefore = 0;
    SemanticInvalidation previousFrameInvalidation;
};

[[nodiscard]] SceneRuntimeMutationReceipt ApplyTransactionToRuntime(
    const SceneTransaction& transaction,
    SceneRuntimeMutationState& state,
    const SceneTransactionValidator& validator,
    SceneTransactionValidationResult* outValidation);
[[nodiscard]] bool RollbackTransactionRuntime(const SceneRuntimeMutationReceipt& receipt,
                                              SceneRuntimeMutationState& state,
                                              const SceneTransactionValidator& validator,
                                              std::string* error = nullptr);
[[nodiscard]] std::string RunSceneTransactionRuntimeSelfTestJson();

} // namespace Cortex::Scene
