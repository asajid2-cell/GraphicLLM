#pragma once

#include "Scene/GeneratedAssetAdmission.h"
#include "Scene/SceneIR.h"
#include "Scene/SceneTransactionRuntime.h"

namespace Cortex::Scene {

struct AuthoringInputRequest {
    SceneIRSource source = SceneIRSource::Text;
    std::string requestId;
    std::string targetGroup;
    std::string materialIntent;
    uint32_t arbitraryEntityCount = 0;
    bool generatedAssetProducer = false;
    GeneratedAssetCandidate generatedAsset;
};

struct AuthoringInputRouteResult {
    bool accepted = false;
    bool compiledToSceneIR = false;
    bool targetedSemanticGroup = false;
    bool runtimeTransactionApplied = false;
    bool generatedAssetAskedBudgetBeforeEmit = false;
    SceneTransaction transaction;
    std::vector<std::string> errors;
};

[[nodiscard]] AuthoringInputRouteResult RouteAuthoringInput(const AuthoringInputRequest& request,
                                                            SceneRuntimeMutationState& state,
                                                            const SceneTransactionValidator& validator,
                                                            const RendererBackpressureSnapshot& snapshot);
[[nodiscard]] std::string RunAuthoringInputRouterSelfTestJson();

} // namespace Cortex::Scene
