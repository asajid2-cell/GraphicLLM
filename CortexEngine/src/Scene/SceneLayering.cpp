#include "Scene/SceneLayering.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

SemanticObject MakeLayerObject(std::string id,
                               std::string group,
                               std::string type,
                               std::string materialIntent,
                               std::string layer,
                               SceneLayerKind kind) {
    SemanticObject object;
    object.id = std::move(id);
    object.editableGroup = std::move(group);
    object.semanticType = std::move(type);
    object.support = "world";
    object.region = "foreground";
    object.materialIntent = std::move(materialIntent);
    object.provenance.prompt = std::string("layer:") + layer;
    object.provenance.seed = 328;
    object.provenance.generator = "scene_layering";
    object.provenance.sourceAsset = ToString(kind);
    object.provenance.validationReport = "scene_layer_resolution";
    object.provenance.commitId = "layer_commit";
    object.budget.estimatedTextureBytes = 1024 * 1024;
    object.budget.texturePages = 1;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 1;
    object.budget.validationCameraCount = 1;
    object.invalidation.taaHistory = true;
    object.invalidation.temporalMasks = true;
    object.invalidation.dirtyRegion = object.region;
    object.admission = SemanticAdmissionStatus::Validated;
    return object;
}

} // namespace

const char* ToString(SceneLayerKind kind) {
    switch (kind) {
    case SceneLayerKind::AuthoredBaseline: return "authored_baseline";
    case SceneLayerKind::GeneratedProposal: return "generated_proposal";
    case SceneLayerKind::UserOverride: return "user_override";
    case SceneLayerKind::MaterialVariant: return "material_variant";
    case SceneLayerKind::ValidationAnnotation: return "validation_annotation";
    }
    return "unknown";
}

SceneLayerResolution ResolveSceneLayersToTransaction(const std::vector<SceneLayer>& layers) {
    SceneLayerResolution resolution;
    if (layers.empty()) {
        AddError(resolution.errors, "scene layers are required");
    }

    std::vector<SceneLayer> sorted = layers;
    std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
        if (left.priority == right.priority) {
            return left.id < right.id;
        }
        return left.priority < right.priority;
    });

    std::unordered_map<std::string, SemanticObject> resolved;
    std::vector<std::string> order;
    for (const auto& layer : sorted) {
        if (Blank(layer.id)) AddError(resolution.errors, "scene layer id is required");
        for (const auto& entry : layer.objects) {
            if (Blank(entry.object.id)) {
                AddError(resolution.errors, "layer object id is required");
                continue;
            }
            if (resolved.find(entry.object.id) == resolved.end()) {
                order.push_back(entry.object.id);
            }
            auto object = entry.object;
            object.provenance.sourceAsset = std::string(ToString(layer.kind)) + ":" + layer.id;
            resolved[object.id] = std::move(object);
        }
    }

    SceneTransaction transaction;
    transaction.id = "tx.layered_scene_resolution";
    transaction.semanticGraphDiff.id = "diff.layered_scene_resolution";
    for (const auto& id : order) {
        const auto found = resolved.find(id);
        if (found == resolved.end()) {
            continue;
        }
        transaction.entityDiff.push_back("resolve_layered_object:" + id);
        transaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, found->second});
        transaction.rendererBudgetDelta.estimatedTextureBytes += found->second.budget.estimatedTextureBytes;
        transaction.rendererBudgetDelta.texturePages += found->second.budget.texturePages;
        transaction.rendererBudgetDelta.psoSignatures += found->second.budget.psoSignatures;
        transaction.rendererBudgetDelta.blasBuilds += found->second.budget.blasBuilds;
        transaction.rendererBudgetDelta.tlasInstances += found->second.budget.tlasInstances;
        transaction.rendererBudgetDelta.descriptors += found->second.budget.descriptors;
        transaction.rendererBudgetDelta.validationCameraCount += found->second.budget.validationCameraCount;
    }
    transaction.resourceDiff.textureBytes = transaction.rendererBudgetDelta.estimatedTextureBytes;
    transaction.resourceDiff.texturePages = transaction.rendererBudgetDelta.texturePages;
    transaction.resourceDiff.descriptorCount = transaction.rendererBudgetDelta.descriptors;
    transaction.resourceDiff.psoSignatures = transaction.rendererBudgetDelta.psoSignatures;
    transaction.resourceDiff.blasBuilds = transaction.rendererBudgetDelta.blasBuilds;
    transaction.resourceDiff.tlasInstances = transaction.rendererBudgetDelta.tlasInstances;
    transaction.resourceDiff.resourceIds.push_back("layer_resolution:" + transaction.id);
    transaction.requiredFeatureTiers.push_back({"scene_layers", "baseline", true});
    transaction.historyInvalidation.taaHistory = true;
    transaction.historyInvalidation.temporalMasks = true;
    transaction.historyInvalidation.dirtyRegion = "foreground";
    transaction.validationCameras.push_back({"layer_resolution_foreground", "foreground", "resolved layer validation"});
    transaction.provenance.prompt = "resolved layered scene";
    transaction.provenance.seed = 328;
    transaction.provenance.generator = "scene_layering";
    transaction.provenance.sourceAsset = "layer_stack";
    transaction.provenance.validationReport = "scene_layer_resolution";
    transaction.provenance.commitId = transaction.id;

    if (transaction.semanticGraphDiff.Empty()) {
        AddError(resolution.errors, "resolved layers produced no runtime transaction diff");
    }

    resolution.accepted = resolution.errors.empty();
    resolution.transaction = std::move(transaction);
    return resolution;
}

std::string RunSceneLayeringSelfTestJson() {
    SceneLayer authored;
    authored.id = "authored";
    authored.kind = SceneLayerKind::AuthoredBaseline;
    authored.priority = 0;
    authored.objects.push_back({MakeLayerObject("rain.table", "tabletop", "support.table", "wet_wood", authored.id, authored.kind),
                                authored.id,
                                authored.kind});

    SceneLayer generated;
    generated.id = "generated";
    generated.kind = SceneLayerKind::GeneratedProposal;
    generated.priority = 10;
    generated.objects.push_back({MakeLayerObject("rain.lantern", "lighting", "light.warm_lantern", "warm_brass", generated.id, generated.kind),
                                 generated.id,
                                 generated.kind});

    SceneLayer materialVariant;
    materialVariant.id = "material_variant";
    materialVariant.kind = SceneLayerKind::MaterialVariant;
    materialVariant.priority = 20;
    materialVariant.objects.push_back({MakeLayerObject("rain.table", "tabletop", "support.table", "polished_wet_wood", materialVariant.id, materialVariant.kind),
                                       materialVariant.id,
                                       materialVariant.kind});

    SceneLayer validation;
    validation.id = "validation";
    validation.kind = SceneLayerKind::ValidationAnnotation;
    validation.priority = 30;
    validation.objects.push_back({MakeLayerObject("rain.validation_marker", "validation", "annotation.camera", "validation_overlay", validation.id, validation.kind),
                                  validation.id,
                                  validation.kind});

    const auto resolved = ResolveSceneLayersToTransaction({generated, validation, authored, materialVariant});

    const bool runtimeReceivesTransaction =
        resolved.accepted &&
        resolved.transaction.id == "tx.layered_scene_resolution" &&
        !resolved.transaction.semanticGraphDiff.Empty();
    bool overrideApplied = false;
    for (const auto& op : resolved.transaction.semanticGraphDiff.ops) {
        if (op.after && op.after->id == "rain.table" && op.after->materialIntent == "polished_wet_wood") {
            overrideApplied = true;
        }
    }

    const bool pass =
        runtimeReceivesTransaction &&
        overrideApplied &&
        resolved.transaction.semanticGraphDiff.ops.size() == 3 &&
        resolved.transaction.provenance.Complete();

    nlohmann::json report;
    report["schema"] = "cortex.scene_layering.self_test.v1";
    report["pass"] = pass;
    report["accepted"] = resolved.accepted;
    report["runtime_receives_transaction"] = runtimeReceivesTransaction;
    report["override_applied"] = overrideApplied;
    report["resolved_object_count"] = resolved.transaction.semanticGraphDiff.ops.size();
    report["provenance_complete"] = resolved.transaction.provenance.Complete();
    report["transaction_id"] = resolved.transaction.id;
    report["errors"] = resolved.errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
