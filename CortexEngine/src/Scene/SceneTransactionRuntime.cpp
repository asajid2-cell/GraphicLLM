#include "Scene/SceneTransactionRuntime.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace Cortex::Scene {
namespace {

SemanticObject MakeRuntimeObject(std::string id, std::string group, std::string type, std::string materialIntent) {
    SemanticObject object;
    object.id = std::move(id);
    object.editableGroup = std::move(group);
    object.semanticType = std::move(type);
    object.support = "world";
    object.region = "foreground";
    object.materialIntent = std::move(materialIntent);
    object.provenance.prompt = "transaction runtime fixture";
    object.provenance.seed = 328;
    object.provenance.generator = "scene_transaction_runtime";
    object.provenance.sourceAsset = "runtime_fixture";
    object.provenance.validationReport = "scene_transaction_runtime";
    object.provenance.commitId = "runtime_fixture";
    object.budget.estimatedTextureBytes = 2ull * 1024ull * 1024ull;
    object.budget.texturePages = 2;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 2;
    object.budget.validationCameraCount = 1;
    object.invalidation.taaHistory = true;
    object.invalidation.rtReflectionHistory = true;
    object.invalidation.temporalMasks = true;
    object.invalidation.dirtyRegion = "foreground";
    object.admission = SemanticAdmissionStatus::Validated;
    return object;
}

SceneTransaction MakeRuntimeTransaction() {
    auto object = MakeRuntimeObject("runtime.generated_lantern", "lighting", "light.lantern", "warm_glass");
    object.support = "runtime.floor";

    SceneTransaction transaction;
    transaction.id = "tx.runtime.generated_lantern";
    transaction.entityDiff.push_back("create:runtime.generated_lantern");
    transaction.semanticGraphDiff.id = "diff.runtime.generated_lantern";
    transaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, object});
    transaction.resourceDiff.textureBytes = object.budget.estimatedTextureBytes;
    transaction.resourceDiff.texturePages = object.budget.texturePages;
    transaction.resourceDiff.descriptorCount = object.budget.descriptors;
    transaction.resourceDiff.psoSignatures = object.budget.psoSignatures;
    transaction.resourceDiff.blasBuilds = object.budget.blasBuilds;
    transaction.resourceDiff.tlasInstances = object.budget.tlasInstances;
    transaction.resourceDiff.resourceIds = {"runtime:generated_lantern"};
    transaction.rendererBudgetDelta = object.budget;
    transaction.requiredFeatureTiers.push_back({"runtime_transaction", "baseline", true});
    transaction.historyInvalidation = object.invalidation;
    transaction.validationCameras.push_back({"runtime_foreground", "foreground", "runtime side effect validation"});
    transaction.provenance.prompt = object.provenance.prompt;
    transaction.provenance.seed = object.provenance.seed;
    transaction.provenance.generator = object.provenance.generator;
    transaction.provenance.sourceAsset = object.provenance.sourceAsset;
    transaction.provenance.validationReport = object.provenance.validationReport;
    transaction.provenance.commitId = transaction.id;
    transaction.temporalUpscalingContract.required = true;
    transaction.temporalUpscalingContract.motionVectorsValid = true;
    transaction.temporalUpscalingContract.exposureValid = true;
    transaction.temporalUpscalingContract.reactiveMaskValid = true;
    transaction.temporalUpscalingContract.generatedObjectInvalidation = true;
    transaction.temporalUpscalingContract.dynamicObjectInvalidation = true;
    transaction.semanticVisualPolicy.requireSupportValidation = true;
    transaction.semanticVisualPolicy.requireValidationCameraPerDirtyRegion = true;
    return transaction;
}

} // namespace

SceneRuntimeMutationReceipt ApplyTransactionToRuntime(const SceneTransaction& transaction,
                                                      SceneRuntimeMutationState& state,
                                                      const SceneTransactionValidator& validator,
                                                      SceneTransactionValidationResult* outValidation) {
    SceneRuntimeMutationReceipt receipt;
    receipt.ecsJobCountBefore = state.ecsEntityJobs.size();
    receipt.rendererJobCountBefore = state.rendererResourceJobs.size();
    receipt.previousFrameInvalidation = state.frameInvalidation;

    receipt.graphReceipt = validator.Commit(transaction, state.graph, outValidation);
    if (!receipt.graphReceipt.committed) {
        return receipt;
    }

    const auto compiled = CompileSemanticGraphForRuntime(state.graph);
    state.ecsEntityJobs = compiled.ecsEntityJobs;
    state.rendererResourceJobs = compiled.rendererResourceJobs;
    state.frameInvalidation = transaction.historyInvalidation;
    receipt.committed = true;
    return receipt;
}

bool RollbackTransactionRuntime(const SceneRuntimeMutationReceipt& receipt,
                                SceneRuntimeMutationState& state,
                                const SceneTransactionValidator& validator,
                                std::string* error) {
    if (!receipt.committed) {
        if (error) *error = "cannot rollback uncommitted runtime transaction";
        return false;
    }
    if (!validator.Rollback(receipt.graphReceipt, state.graph, error)) {
        return false;
    }
    state.ecsEntityJobs.resize(receipt.ecsJobCountBefore);
    state.rendererResourceJobs.resize(receipt.rendererJobCountBefore);
    state.frameInvalidation = receipt.previousFrameInvalidation;
    return true;
}

std::string RunSceneTransactionRuntimeSelfTestJson() {
    SemanticBudget limit;
    limit.estimatedTextureBytes = 64ull * 1024ull * 1024ull;
    limit.texturePages = 64;
    limit.psoSignatures = 16;
    limit.blasBuilds = 16;
    limit.tlasInstances = 16;
    limit.descriptors = 128;
    limit.validationCameraCount = 8;
    SceneTransactionValidator validator(limit);

    SceneRuntimeMutationState state;
    std::string error;
    std::vector<std::string> errors;
    if (!state.graph.AddObject(MakeRuntimeObject("runtime.floor", "room", "support.floor", "wet_floor"), &error)) {
        errors.push_back(error);
    }

    const auto transaction = MakeRuntimeTransaction();
    SceneTransactionValidationResult validation;
    const auto receipt = ApplyTransactionToRuntime(transaction, state, validator, &validation);
    const bool runtimeSideEffects =
        receipt.committed &&
        validation.accepted &&
        state.graph.FindById("runtime.generated_lantern") != nullptr &&
        state.ecsEntityJobs.size() == 2 &&
        state.rendererResourceJobs.size() == 2 &&
        state.frameInvalidation.Any();

    auto bad = transaction;
    bad.id = "tx.runtime.bad_layout";
    auto badObject = MakeRuntimeObject("runtime.bad_floating", "bad", "prop.floating_panel", "placeholder");
    badObject.support.clear();
    bad.semanticGraphDiff.ops.clear();
    bad.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, badObject});
    const auto ecsBeforeBad = state.ecsEntityJobs.size();
    const auto resourcesBeforeBad = state.rendererResourceJobs.size();
    SceneTransactionValidationResult badValidation;
    const auto badReceipt = ApplyTransactionToRuntime(bad, state, validator, &badValidation);
    const bool badRejectedBeforeSideEffects =
        !badReceipt.committed &&
        !badValidation.accepted &&
        state.ecsEntityJobs.size() == ecsBeforeBad &&
        state.rendererResourceJobs.size() == resourcesBeforeBad &&
        state.graph.FindById("runtime.bad_floating") == nullptr;

    std::string rollbackError;
    const bool rollback = RollbackTransactionRuntime(receipt, state, validator, &rollbackError);
    if (!rollback) {
        errors.push_back(rollbackError);
    }
    const bool rollbackRestored =
        rollback &&
        state.graph.FindById("runtime.generated_lantern") == nullptr &&
        state.ecsEntityJobs.empty() &&
        state.rendererResourceJobs.empty() &&
        !state.frameInvalidation.Any();

    const bool pass = errors.empty() && runtimeSideEffects && badRejectedBeforeSideEffects && rollbackRestored;

    nlohmann::json report;
    report["schema"] = "cortex.scene_transaction_runtime.self_test.v1";
    report["pass"] = pass;
    report["runtime_side_effects"] = runtimeSideEffects;
    report["bad_rejected_before_side_effects"] = badRejectedBeforeSideEffects;
    report["rollback_restored"] = rollbackRestored;
    report["ecs_jobs_after_commit"] = ecsBeforeBad;
    report["renderer_jobs_after_commit"] = resourcesBeforeBad;
    report["frame_invalidation_any"] = state.frameInvalidation.Any();
    report["errors"] = errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
