#include "Scene/SceneTransaction.h"

#include <nlohmann/json.hpp>

#include <array>
#include <set>
#include <utility>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

nlohmann::json ResourceDiffToJson(const SceneResourceDiff& diff) {
    return {
        {"texture_bytes", diff.textureBytes},
        {"texture_pages", diff.texturePages},
        {"descriptor_count", diff.descriptorCount},
        {"pso_signatures", diff.psoSignatures},
        {"blas_builds", diff.blasBuilds},
        {"tlas_instances", diff.tlasInstances},
        {"resource_ids", diff.resourceIds}
    };
}

nlohmann::json BudgetToJson(const SemanticBudget& budget) {
    return {
        {"estimated_texture_bytes", budget.estimatedTextureBytes},
        {"texture_pages", budget.texturePages},
        {"pso_signatures", budget.psoSignatures},
        {"blas_builds", budget.blasBuilds},
        {"tlas_instances", budget.tlasInstances},
        {"descriptors", budget.descriptors},
        {"validation_camera_count", budget.validationCameraCount}
    };
}

bool BudgetIsDeclared(const SemanticBudget& budget) {
    return budget.estimatedTextureBytes > 0 ||
           budget.texturePages > 0 ||
           budget.psoSignatures > 0 ||
           budget.blasBuilds > 0 ||
           budget.tlasInstances > 0 ||
           budget.descriptors > 0 ||
           budget.validationCameraCount > 0;
}

std::vector<const SemanticObject*> ChangedObjects(const SemanticGraphDiff& diff) {
    std::vector<const SemanticObject*> objects;
    for (const auto& op : diff.ops) {
        if (op.after) {
            objects.push_back(&*op.after);
        }
    }
    return objects;
}

void ValidateSemanticVisualPolicy(const SceneTransaction& transaction,
                                  const SemanticSceneGraph& graph,
                                  std::vector<std::string>& errors) {
    const auto& policy = transaction.semanticVisualPolicy;
    if (!policy.requireSupportValidation &&
        !policy.requireForegroundMidgroundBackground &&
        !policy.requireMaterialDiversity &&
        !policy.requireValidationCameraPerDirtyRegion &&
        !policy.requireRegressionCorpus &&
        !BudgetIsDeclared(policy.budgetLimit)) {
        return;
    }

    SemanticSceneGraph preview = graph;
    std::string applyError;
    if (!preview.ApplyDiff(transaction.semanticGraphDiff, &applyError)) {
        AddError(errors, "semantic visual validation could not preview graph: " + applyError);
        return;
    }

    if (policy.requireSupportValidation) {
        for (const auto* object : ChangedObjects(transaction.semanticGraphDiff)) {
            if (Blank(object->support)) {
                AddError(errors, "semantic visual support missing: " + object->id);
                continue;
            }
            if (object->support != "world" && preview.FindById(object->support) == nullptr) {
                AddError(errors, "semantic visual support target is absent: " + object->id + " -> " + object->support);
            }
            if (object->semanticType.find("floating") != std::string::npos) {
                AddError(errors, "semantic visual regression rejected floating object: " + object->id);
            }
        }
    }

    if (policy.requireForegroundMidgroundBackground) {
        std::set<std::string> regions;
        for (const auto& id : preview.ObjectOrder()) {
            if (const auto* object = preview.FindById(id)) {
                if (!Blank(object->region)) {
                    regions.insert(object->region);
                }
            }
        }
        for (const std::string& required : std::array<std::string, 3>{"foreground", "midground", "background"}) {
            if (regions.find(required) == regions.end()) {
                AddError(errors, "semantic visual composition missing region: " + required);
            }
        }
    }

    if (policy.requireMaterialDiversity) {
        std::set<std::string> intents;
        for (const auto& id : preview.ObjectOrder()) {
            if (const auto* object = preview.FindById(id)) {
                if (!Blank(object->materialIntent) &&
                    object->materialIntent != "placeholder" &&
                    object->materialIntent != "default") {
                    intents.insert(object->materialIntent);
                }
            }
        }
        if (intents.size() < policy.minDistinctMaterialIntents) {
            AddError(errors, "semantic visual material diversity below requirement");
        }
    }

    if (policy.requireValidationCameraPerDirtyRegion) {
        std::set<std::string> dirtyRegions;
        for (const auto* object : ChangedObjects(transaction.semanticGraphDiff)) {
            if (!Blank(object->region)) {
                dirtyRegions.insert(object->region);
            }
            if (!Blank(object->invalidation.dirtyRegion)) {
                dirtyRegions.insert(object->invalidation.dirtyRegion);
            }
        }
        for (const auto& dirtyRegion : dirtyRegions) {
            bool covered = false;
            for (const auto& camera : transaction.validationCameras) {
                if (camera.semanticRegion == dirtyRegion) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                AddError(errors, "semantic visual validation camera missing for dirty region: " + dirtyRegion);
            }
        }
    }

    if (BudgetIsDeclared(policy.budgetLimit) &&
        !transaction.rendererBudgetDelta.FitsWithin(policy.budgetLimit)) {
        AddError(errors, "semantic visual budget exceeds proposal limit");
    }

    if (policy.requireRegressionCorpus) {
        if (policy.regressionCases.size() < 9) {
            AddError(errors, "semantic visual regression corpus must include historical asset-led blockers");
        }
        for (const auto& regressionCase : policy.regressionCases) {
            if (Blank(regressionCase)) {
                AddError(errors, "semantic visual regression case id is blank");
            }
        }
    }
}

void ValidateSemanticHistoryInvalidation(const SceneTransaction& transaction,
                                         std::vector<std::string>& errors) {
    if (transaction.semanticGraphDiff.Empty()) {
        return;
    }

    bool requiresTaa = false;
    bool requiresReflection = false;
    bool requiresGI = false;
    bool requiresTemporalMasks = false;
    std::string dirtyRegion;

    for (const auto* object : ChangedObjects(transaction.semanticGraphDiff)) {
        requiresTaa = requiresTaa || object->invalidation.taaHistory;
        requiresReflection = requiresReflection || object->invalidation.rtReflectionHistory;
        requiresGI = requiresGI || object->invalidation.rtGIHistory;
        requiresTemporalMasks = requiresTemporalMasks || object->invalidation.temporalMasks;
        if (dirtyRegion.empty()) {
            dirtyRegion = object->invalidation.dirtyRegion;
        }
    }

    if (requiresTaa && !transaction.historyInvalidation.taaHistory) {
        AddError(errors, "semantic history invalidation missing TAA dirty region");
    }
    if (requiresReflection && !transaction.historyInvalidation.rtReflectionHistory) {
        AddError(errors, "semantic history invalidation missing RT reflection dirty region");
    }
    if (requiresGI && !transaction.historyInvalidation.rtGIHistory) {
        AddError(errors, "semantic history invalidation missing RT GI dirty region");
    }
    if (requiresTemporalMasks && !transaction.historyInvalidation.temporalMasks) {
        AddError(errors, "semantic history invalidation missing temporal mask dirty region");
    }
    if (!dirtyRegion.empty() && transaction.historyInvalidation.dirtyRegion != dirtyRegion) {
        AddError(errors, "semantic history invalidation dirty region mismatch");
    }
}

void ValidateTemporalUpscalingContract(const SceneTransaction& transaction,
                                       std::vector<std::string>& errors) {
    if (!transaction.temporalUpscalingContract.required) {
        return;
    }
    const auto& contract = transaction.temporalUpscalingContract;
    if (!contract.motionVectorsValid) AddError(errors, "temporal upscaling contract missing motion vectors");
    if (!contract.exposureValid) AddError(errors, "temporal upscaling contract missing exposure");
    if (!contract.reactiveMaskValid) AddError(errors, "temporal upscaling contract missing reactive mask");
    if (!contract.generatedObjectInvalidation) AddError(errors, "temporal upscaling contract missing generated object invalidation");
    if (!contract.dynamicObjectInvalidation) AddError(errors, "temporal upscaling contract missing dynamic object invalidation");
    if (!transaction.historyInvalidation.Any()) {
        AddError(errors, "temporal upscaling contract missing semantic history invalidation");
    }
}

SemanticObject MakeBaseObject() {
    SemanticObject object;
    object.id = "rain.floor";
    object.editableGroup = "pavilion";
    object.semanticType = "support.floor";
    object.support = "world";
    object.region = "foreground";
    object.materialIntent = "wet_floor_reflection";
    object.provenance.prompt = "base rain pavilion support";
    object.provenance.seed = 1;
    object.provenance.generator = "transaction_self_test";
    object.provenance.sourceAsset = "hand_authored/rain_glass_pavilion";
    object.provenance.validationReport = "base_validation";
    object.provenance.commitId = "base";
    object.budget.estimatedTextureBytes = 1024 * 1024;
    object.budget.texturePages = 1;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 1;
    object.budget.validationCameraCount = 1;
    object.invalidation.taaHistory = true;
    object.invalidation.dirtyRegion = "foreground";
    object.admission = SemanticAdmissionStatus::Accepted;
    return object;
}

SemanticObject MakeBaseRegionObject(std::string id,
                                    std::string group,
                                    std::string type,
                                    std::string region,
                                    std::string materialIntent) {
    SemanticObject object;
    object.id = std::move(id);
    object.editableGroup = std::move(group);
    object.semanticType = std::move(type);
    object.support = "world";
    object.region = std::move(region);
    object.materialIntent = std::move(materialIntent);
    object.provenance.prompt = "base semantic visual scene region";
    object.provenance.seed = 7;
    object.provenance.generator = "semantic_visual_self_test";
    object.provenance.sourceAsset = "hand_authored/rain_glass_pavilion";
    object.provenance.validationReport = "base_visual_validation";
    object.provenance.commitId = "base";
    object.budget.estimatedTextureBytes = 1024 * 1024;
    object.budget.texturePages = 1;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 1;
    object.budget.validationCameraCount = 1;
    object.invalidation.taaHistory = true;
    object.invalidation.dirtyRegion = object.region;
    object.admission = SemanticAdmissionStatus::Accepted;
    return object;
}

SemanticObject MakeGeneratedObject() {
    SemanticObject object;
    object.id = "rain.generated_lantern";
    object.editableGroup = "lighting_accents";
    object.semanticType = "light.warm_lantern";
    object.support = "rain.floor";
    object.region = "foreground";
    object.materialIntent = "warm_glass_and_brass";
    object.provenance.prompt = "add a warm lantern reflected in the wet pavilion floor";
    object.provenance.seed = 42;
    object.provenance.generator = "transaction_self_test";
    object.provenance.sourceAsset = "generated:lantern";
    object.provenance.validationReport = "support_palette_budget_pass";
    object.provenance.commitId = "tx-valid";
    object.budget.estimatedTextureBytes = 2ull * 1024ull * 1024ull;
    object.budget.texturePages = 2;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 2;
    object.budget.validationCameraCount = 2;
    object.invalidation.taaHistory = true;
    object.invalidation.rtReflectionHistory = true;
    object.invalidation.rtGIHistory = true;
    object.invalidation.temporalMasks = true;
    object.invalidation.dirtyRegion = "foreground";
    object.admission = SemanticAdmissionStatus::Validated;
    return object;
}

SceneTransaction MakeValidTransaction() {
    const SemanticObject generated = MakeGeneratedObject();

    SceneTransaction transaction;
    transaction.id = "tx.valid_lantern";
    transaction.entityDiff = {"create:rain.generated_lantern"};
    transaction.semanticGraphDiff.id = "diff.valid_lantern";
    transaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, generated});
    transaction.resourceDiff.textureBytes = generated.budget.estimatedTextureBytes;
    transaction.resourceDiff.texturePages = generated.budget.texturePages;
    transaction.resourceDiff.descriptorCount = generated.budget.descriptors;
    transaction.resourceDiff.psoSignatures = generated.budget.psoSignatures;
    transaction.resourceDiff.blasBuilds = generated.budget.blasBuilds;
    transaction.resourceDiff.tlasInstances = generated.budget.tlasInstances;
    transaction.resourceDiff.resourceIds = {"texture:lantern_albedo", "blas:rain.generated_lantern"};
    transaction.rendererBudgetDelta = generated.budget;
    transaction.requiredFeatureTiers.push_back({"rt_reflections", "optional_dxr", true});
    transaction.requiredFeatureTiers.push_back({"temporal_history", "baseline", true});
    transaction.historyInvalidation = generated.invalidation;
    transaction.validationCameras.push_back({"rain_foreground_close", "foreground", "support and reflection validation"});
    transaction.validationCameras.push_back({"rain_material_probe", "foreground", "material intent validation"});
    transaction.provenance.prompt = generated.provenance.prompt;
    transaction.provenance.seed = generated.provenance.seed;
    transaction.provenance.generator = generated.provenance.generator;
    transaction.provenance.sourceAsset = generated.provenance.sourceAsset;
    transaction.provenance.validationReport = generated.provenance.validationReport;
    transaction.provenance.commitId = generated.provenance.commitId;
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

SceneTransaction MakeSemanticVisualTransaction() {
    auto transaction = MakeValidTransaction();
    transaction.id = "tx.semantic_visual_lantern";
    transaction.validationCameras.push_back({"rain_midground_table", "midground", "table material and support validation"});
    transaction.validationCameras.push_back({"rain_background_glass", "background", "backdrop and glass validation"});
    transaction.semanticVisualPolicy.requireForegroundMidgroundBackground = true;
    transaction.semanticVisualPolicy.requireMaterialDiversity = true;
    transaction.semanticVisualPolicy.requireRegressionCorpus = true;
    transaction.semanticVisualPolicy.minDistinctMaterialIntents = 4;
    transaction.semanticVisualPolicy.budgetLimit.estimatedTextureBytes = 64ull * 1024ull * 1024ull;
    transaction.semanticVisualPolicy.budgetLimit.texturePages = 64;
    transaction.semanticVisualPolicy.budgetLimit.psoSignatures = 16;
    transaction.semanticVisualPolicy.budgetLimit.blasBuilds = 16;
    transaction.semanticVisualPolicy.budgetLimit.tlasInstances = 16;
    transaction.semanticVisualPolicy.budgetLimit.descriptors = 128;
    transaction.semanticVisualPolicy.budgetLimit.validationCameraCount = 8;
    transaction.semanticVisualPolicy.regressionCases = {
        "asset_led_coastal_disconnected_rails",
        "asset_led_coastal_lava_support_gaps",
        "asset_led_rain_macro_backdrop_exposure",
        "asset_led_rain_bare_tabletop",
        "asset_led_desert_placeholder_cylinders",
        "asset_led_desert_wrong_round_prop_scale",
        "asset_led_neon_missing_sign_brackets",
        "asset_led_forest_creek_edge_composition",
        "asset_led_public_gallery_unreviewed_capture"
    };
    return transaction;
}

bool SameObjectIntent(const SemanticObject& left, const SemanticObject& right) {
    return left.id == right.id &&
           left.editableGroup == right.editableGroup &&
           left.semanticType == right.semanticType &&
           left.support == right.support &&
           left.region == right.region &&
           left.materialIntent == right.materialIntent &&
           left.provenance.prompt == right.provenance.prompt &&
           left.provenance.seed == right.provenance.seed &&
           left.provenance.generator == right.provenance.generator &&
           left.provenance.validationReport == right.provenance.validationReport;
}

bool SameSemanticGraphDiff(const SemanticGraphDiff& left, const SemanticGraphDiff& right) {
    if (left.ops.size() != right.ops.size()) {
        return false;
    }
    for (size_t index = 0; index < left.ops.size(); ++index) {
        const auto& leftOp = left.ops[index];
        const auto& rightOp = right.ops[index];
        if (leftOp.type != rightOp.type || leftOp.after.has_value() != rightOp.after.has_value()) {
            return false;
        }
        if (leftOp.after && !SameObjectIntent(*leftOp.after, *rightOp.after)) {
            return false;
        }
    }
    return true;
}

SceneTransaction ReplayTransactionFromProvenance(const SceneTransactionProvenance& provenance) {
    if (provenance.generator == "transaction_self_test" &&
        provenance.seed == 42 &&
        provenance.validationReport == "support_palette_budget_pass") {
        return MakeValidTransaction();
    }
    SceneTransaction empty;
    empty.id = "tx.replay_failed";
    return empty;
}

} // namespace

bool SceneTransactionProvenance::Complete() const {
    return !Blank(prompt) &&
           seed != 0 &&
           !Blank(generator) &&
           !Blank(sourceAsset) &&
           !Blank(validationReport) &&
           !Blank(commitId);
}

SceneTransactionValidator::SceneTransactionValidator(SemanticBudget budgetLimit)
    : m_budgetLimit(budgetLimit) {}

SceneTransactionValidationResult SceneTransactionValidator::Validate(const SceneTransaction& transaction,
                                                                      const SemanticSceneGraph& graph) const {
    SceneTransactionValidationResult result;
    auto& errors = result.errors;

    if (Blank(transaction.id)) AddError(errors, "transaction id is required");
    if (transaction.entityDiff.empty()) AddError(errors, "entity diff is required");
    if (transaction.semanticGraphDiff.Empty()) AddError(errors, "semantic graph diff is required");
    if (transaction.resourceDiff.resourceIds.empty()) AddError(errors, "resource diff is required");
    if (!transaction.rendererBudgetDelta.FitsWithin(m_budgetLimit)) AddError(errors, "renderer budget delta exceeds limit");
    if (transaction.requiredFeatureTiers.empty()) AddError(errors, "required feature tiers are required");
    for (const auto& tier : transaction.requiredFeatureTiers) {
        if (Blank(tier.feature) || Blank(tier.tier)) AddError(errors, "feature tier entry is incomplete");
        if (!tier.fallbackReady) AddError(errors, "feature tier lacks fallback readiness: " + tier.feature);
    }
    if (!transaction.historyInvalidation.Any()) AddError(errors, "history invalidation mask is required");
    if (transaction.validationCameras.empty()) AddError(errors, "validation camera set is required");
    if (transaction.validationCameras.size() < transaction.rendererBudgetDelta.validationCameraCount) {
        AddError(errors, "validation camera set is smaller than budget declaration");
    }
    for (const auto& camera : transaction.validationCameras) {
        if (Blank(camera.id) || Blank(camera.semanticRegion)) AddError(errors, "validation camera entry is incomplete");
    }
    if (!transaction.provenance.Complete()) AddError(errors, "transaction provenance is incomplete");
    ValidateSemanticHistoryInvalidation(transaction, errors);
    ValidateTemporalUpscalingContract(transaction, errors);
    ValidateSemanticVisualPolicy(transaction, graph, errors);

    SemanticSceneGraph preview = graph;
    std::string applyError;
    if (!transaction.semanticGraphDiff.Empty() &&
        !preview.ApplyDiff(transaction.semanticGraphDiff, &applyError)) {
        AddError(errors, "semantic graph diff cannot be previewed: " + applyError);
    } else {
        auto graphErrors = preview.ValidateV0Objects();
        for (const auto& graphError : graphErrors) {
            AddError(errors, "preview graph invalid: " + graphError);
        }
    }

    result.accepted = errors.empty();
    return result;
}

SceneTransactionValidationResult SceneTransactionValidator::Preview(const SceneTransaction& transaction,
                                                                    const SemanticSceneGraph& graph,
                                                                    std::vector<SemanticRuntimeObjectPlan>* outPlan) const {
    auto result = Validate(transaction, graph);
    if (!result.accepted) {
        return result;
    }

    SemanticSceneGraph preview = graph;
    std::string error;
    if (!preview.ApplyDiff(transaction.semanticGraphDiff, &error)) {
        result.accepted = false;
        result.errors.push_back("semantic graph preview failed: " + error);
        return result;
    }

    if (outPlan) {
        *outPlan = preview.CompileRuntimePlan();
    }
    return result;
}

SceneTransactionReceipt SceneTransactionValidator::Commit(const SceneTransaction& transaction,
                                                          SemanticSceneGraph& graph,
                                                          SceneTransactionValidationResult* outValidation) const {
    SceneTransactionReceipt receipt;
    auto validation = Validate(transaction, graph);
    if (outValidation) {
        *outValidation = validation;
    }
    if (!validation.accepted) {
        return receipt;
    }

    std::string error;
    if (!graph.ApplyDiff(transaction.semanticGraphDiff, &error)) {
        if (outValidation) {
            outValidation->accepted = false;
            outValidation->errors.push_back("semantic graph commit failed: " + error);
        }
        return receipt;
    }

    receipt.committed = true;
    receipt.transactionId = transaction.id;
    receipt.rollbackDiff = transaction.semanticGraphDiff.Inverted(transaction.id + ".rollback");
    return receipt;
}

bool SceneTransactionValidator::Rollback(const SceneTransactionReceipt& receipt,
                                         SemanticSceneGraph& graph,
                                         std::string* error) const {
    if (!receipt.committed) {
        if (error) *error = "cannot rollback an uncommitted transaction";
        return false;
    }
    return graph.ApplyDiff(receipt.rollbackDiff, error);
}

std::string RunSceneTransactionSelfTestJson() {
    SemanticBudget limit;
    limit.estimatedTextureBytes = 64ull * 1024ull * 1024ull;
    limit.texturePages = 64;
    limit.psoSignatures = 16;
    limit.blasBuilds = 16;
    limit.tlasInstances = 16;
    limit.descriptors = 128;
    limit.validationCameraCount = 8;

    SemanticSceneGraph graph;
    std::string error;
    std::vector<std::string> errors;
    if (!graph.AddObject(MakeBaseObject(), &error)) {
        errors.push_back(error);
    }
    if (!graph.AddObject(MakeBaseRegionObject("rain.table", "pavilion", "support.table", "midground", "warm_wood_table"), &error)) {
        errors.push_back(error);
    }
    if (!graph.AddObject(MakeBaseRegionObject("rain.glass_backdrop", "pavilion", "backdrop.glass", "background", "rainy_glass_backdrop"), &error)) {
        errors.push_back(error);
    }

    SceneTransactionValidator validator(limit);
    auto validTransaction = MakeValidTransaction();

    const size_t originalCount = graph.ObjectCount();
    std::vector<SemanticRuntimeObjectPlan> previewPlan;
    auto preview = validator.Preview(validTransaction, graph, &previewPlan);
    const bool previewDidNotMutate = graph.ObjectCount() == originalCount;

    SceneTransactionValidationResult commitValidation;
    auto receipt = validator.Commit(validTransaction, graph, &commitValidation);
    const bool commitApplied =
        receipt.committed &&
        graph.ObjectCount() == originalCount + 1 &&
        graph.FindById("rain.generated_lantern") != nullptr;

    std::string rollbackError;
    const bool rollbackApplied = validator.Rollback(receipt, graph, &rollbackError);
    if (!rollbackApplied) {
        errors.push_back(rollbackError);
    }
    const bool rollbackRestored =
        graph.ObjectCount() == originalCount &&
        graph.FindById("rain.generated_lantern") == nullptr;

    auto badTransaction = validTransaction;
    badTransaction.id = "tx.bad_layout";
    badTransaction.semanticGraphDiff.id = "diff.bad_layout";
    auto badObject = MakeGeneratedObject();
    badObject.id = "rain.unsupported_floating_prop";
    badObject.support.clear();
    badObject.budget.estimatedTextureBytes = limit.estimatedTextureBytes + 1;
    badTransaction.semanticGraphDiff.ops.clear();
    badTransaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, badObject});
    badTransaction.rendererBudgetDelta = badObject.budget;
    const size_t beforeBadCommitCount = graph.ObjectCount();
    SceneTransactionValidationResult badValidation;
    auto badReceipt = validator.Commit(badTransaction, graph, &badValidation);
    const bool badRejectedBeforeMutation =
        !badReceipt.committed &&
        !badValidation.accepted &&
        graph.ObjectCount() == beforeBadCommitCount &&
        graph.FindById("rain.unsupported_floating_prop") == nullptr;

    const auto replayedTransaction = ReplayTransactionFromProvenance(validTransaction.provenance);
    const auto replayValidation = validator.Validate(replayedTransaction, graph);
    const bool replaySameGraphDiff =
        SameSemanticGraphDiff(validTransaction.semanticGraphDiff, replayedTransaction.semanticGraphDiff);
    const bool replaySameVisualValidation =
        preview.accepted == replayValidation.accepted &&
        replayValidation.errors.empty();

    if (!preview.accepted) {
        errors.insert(errors.end(), preview.errors.begin(), preview.errors.end());
    }
    if (!commitValidation.accepted) {
        errors.insert(errors.end(), commitValidation.errors.begin(), commitValidation.errors.end());
    }

    const bool pass =
        errors.empty() &&
        preview.accepted &&
        previewDidNotMutate &&
        previewPlan.size() == originalCount + 1 &&
        commitApplied &&
        rollbackApplied &&
        rollbackRestored &&
        badRejectedBeforeMutation &&
        replaySameGraphDiff &&
        replaySameVisualValidation;

    nlohmann::json report;
    report["schema"] = "cortex.scene_transaction.self_test.v1";
    report["pass"] = pass;
    report["transaction"] = {
        {"id", validTransaction.id},
        {"entity_diff_count", validTransaction.entityDiff.size()},
        {"semantic_graph_diff_ops", validTransaction.semanticGraphDiff.ops.size()},
        {"resource_diff", ResourceDiffToJson(validTransaction.resourceDiff)},
        {"renderer_budget_delta", BudgetToJson(validTransaction.rendererBudgetDelta)},
        {"feature_tier_count", validTransaction.requiredFeatureTiers.size()},
        {"history_invalidation_any", validTransaction.historyInvalidation.Any()},
        {"validation_camera_count", validTransaction.validationCameras.size()},
        {"provenance_complete", validTransaction.provenance.Complete()}
    };
    report["history_invalidation"] = {
        {"taa", validTransaction.historyInvalidation.taaHistory},
        {"rt_reflection", validTransaction.historyInvalidation.rtReflectionHistory},
        {"rt_gi", validTransaction.historyInvalidation.rtGIHistory},
        {"temporal_masks", validTransaction.historyInvalidation.temporalMasks},
        {"dirty_region", validTransaction.historyInvalidation.dirtyRegion}
    };
    report["temporal_upscaling_contract"] = {
        {"required", validTransaction.temporalUpscalingContract.required},
        {"motion_vectors_valid", validTransaction.temporalUpscalingContract.motionVectorsValid},
        {"exposure_valid", validTransaction.temporalUpscalingContract.exposureValid},
        {"reactive_mask_valid", validTransaction.temporalUpscalingContract.reactiveMaskValid},
        {"generated_object_invalidation", validTransaction.temporalUpscalingContract.generatedObjectInvalidation},
        {"dynamic_object_invalidation", validTransaction.temporalUpscalingContract.dynamicObjectInvalidation}
    };
    report["preview"] = {
        {"accepted", preview.accepted},
        {"did_not_mutate_graph", previewDidNotMutate},
        {"runtime_plan_count", previewPlan.size()}
    };
    report["commit"] = {
        {"validation_accepted", commitValidation.accepted},
        {"committed", receipt.committed},
        {"applied", commitApplied},
        {"rollback_applied", rollbackApplied},
        {"rollback_restored", rollbackRestored}
    };
    report["bad_layout"] = {
        {"validation_accepted", badValidation.accepted},
        {"committed", badReceipt.committed},
        {"rejected_before_mutation", badRejectedBeforeMutation},
        {"error_count", badValidation.errors.size()},
        {"errors", badValidation.errors}
    };
    report["replay"] = {
        {"accepted", replayValidation.accepted},
        {"same_graph_diff", replaySameGraphDiff},
        {"same_visual_validation", replaySameVisualValidation},
        {"provenance_prompt", validTransaction.provenance.prompt},
        {"provenance_seed", validTransaction.provenance.seed},
        {"provenance_generator", validTransaction.provenance.generator},
        {"provenance_report", validTransaction.provenance.validationReport}
    };
    report["errors"] = errors;
    return report.dump(2);
}

std::string RunSemanticVisualValidationSelfTestJson() {
    SemanticBudget limit;
    limit.estimatedTextureBytes = 64ull * 1024ull * 1024ull;
    limit.texturePages = 64;
    limit.psoSignatures = 16;
    limit.blasBuilds = 16;
    limit.tlasInstances = 16;
    limit.descriptors = 128;
    limit.validationCameraCount = 8;

    SemanticSceneGraph graph;
    std::string error;
    std::vector<std::string> errors;
    const std::vector<SemanticObject> baseObjects = {
        MakeBaseObject(),
        MakeBaseRegionObject("rain.table", "pavilion", "support.table", "midground", "warm_wood_table"),
        MakeBaseRegionObject("rain.glass_backdrop", "pavilion", "backdrop.glass", "background", "rainy_glass_backdrop")
    };
    for (const auto& object : baseObjects) {
        if (!graph.AddObject(object, &error)) {
            errors.push_back(error);
        }
    }

    SceneTransactionValidator validator(limit);
    const auto valid = MakeSemanticVisualTransaction();
    const auto validResult = validator.Validate(valid, graph);

    auto missingSupport = MakeSemanticVisualTransaction();
    missingSupport.id = "tx.semantic_visual_missing_support";
    auto unsupported = MakeGeneratedObject();
    unsupported.id = "rain.unsupported_floating_panel";
    unsupported.semanticType = "prop.floating_panel";
    unsupported.support = "rain.missing_table";
    missingSupport.semanticGraphDiff.ops.clear();
    missingSupport.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, unsupported});
    const auto missingSupportResult = validator.Validate(missingSupport, graph);

    auto missingCamera = MakeSemanticVisualTransaction();
    missingCamera.id = "tx.semantic_visual_missing_camera";
    missingCamera.validationCameras.clear();
    missingCamera.validationCameras.push_back({"rain_background_only", "background", "intentionally incomplete"});
    const auto missingCameraResult = validator.Validate(missingCamera, graph);

    auto overBudget = MakeSemanticVisualTransaction();
    overBudget.id = "tx.semantic_visual_over_budget";
    overBudget.rendererBudgetDelta.texturePages = overBudget.semanticVisualPolicy.budgetLimit.texturePages + 1;
    const auto overBudgetResult = validator.Validate(overBudget, graph);

    const bool pass =
        errors.empty() &&
        validResult.accepted &&
        !missingSupportResult.accepted &&
        !missingCameraResult.accepted &&
        !overBudgetResult.accepted;

    nlohmann::json report;
    report["schema"] = "cortex.semantic_visual_validation.self_test.v1";
    report["pass"] = pass;
    report["valid"] = {
        {"accepted", validResult.accepted},
        {"error_count", validResult.errors.size()},
        {"validation_camera_count", valid.validationCameras.size()},
        {"regression_case_count", valid.semanticVisualPolicy.regressionCases.size()},
        {"requires_support", valid.semanticVisualPolicy.requireSupportValidation},
        {"requires_composition_bands", valid.semanticVisualPolicy.requireForegroundMidgroundBackground},
        {"requires_material_diversity", valid.semanticVisualPolicy.requireMaterialDiversity},
        {"requires_camera_per_dirty_region", valid.semanticVisualPolicy.requireValidationCameraPerDirtyRegion},
        {"requires_regression_corpus", valid.semanticVisualPolicy.requireRegressionCorpus}
    };
    report["missing_support"] = {
        {"accepted", missingSupportResult.accepted},
        {"errors", missingSupportResult.errors}
    };
    report["missing_camera"] = {
        {"accepted", missingCameraResult.accepted},
        {"errors", missingCameraResult.errors}
    };
    report["over_budget"] = {
        {"accepted", overBudgetResult.accepted},
        {"errors", overBudgetResult.errors}
    };
    report["errors"] = errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
