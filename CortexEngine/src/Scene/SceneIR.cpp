#include "Scene/SceneIR.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

SemanticBudget DefaultIRBudget(uint32_t targetCount) {
    SemanticBudget budget;
    budget.estimatedTextureBytes = 1024ull * 1024ull * std::max<uint32_t>(1, targetCount);
    budget.texturePages = std::max<uint32_t>(1, targetCount);
    budget.psoSignatures = 1;
    budget.blasBuilds = targetCount;
    budget.tlasInstances = targetCount;
    budget.descriptors = std::max<uint32_t>(1, targetCount);
    budget.validationCameraCount = 1;
    return budget;
}

SemanticObject MakeSeedObject(std::string id, std::string group, std::string materialIntent) {
    SemanticObject object;
    object.id = std::move(id);
    object.editableGroup = std::move(group);
    object.semanticType = "prop.material_target";
    object.support = "rain.floor";
    object.region = "foreground";
    object.materialIntent = std::move(materialIntent);
    object.provenance.prompt = "seed semantic object";
    object.provenance.seed = 9;
    object.provenance.generator = "scene_ir_self_test";
    object.provenance.sourceAsset = "hand_authored/rain_glass_pavilion";
    object.provenance.validationReport = "seed_validation";
    object.provenance.commitId = "seed";
    object.budget = DefaultIRBudget(1);
    object.invalidation.taaHistory = true;
    object.invalidation.rtReflectionHistory = true;
    object.invalidation.temporalMasks = true;
    object.invalidation.dirtyRegion = "foreground";
    object.admission = SemanticAdmissionStatus::Accepted;
    return object;
}

SceneIRCommand MakeCommon(SceneIRSource source,
                          std::string requestId,
                          std::string targetGroup,
                          std::string materialIntent) {
    SceneIRCommand command;
    command.source = source;
    command.op = SceneIROpType::ModifyMaterialIntent;
    command.requestId = std::move(requestId);
    command.targetGroup = std::move(targetGroup);
    command.materialIntent = std::move(materialIntent);
    command.prompt = "set " + command.targetGroup + " material intent to " + command.materialIntent;
    command.seed = 77;
    command.generator = std::string("scene_ir_") + ToString(source);
    return command;
}

std::string CanonicalTransactionShape(const SceneTransaction& transaction) {
    return std::to_string(transaction.semanticGraphDiff.ops.size()) + "|" +
           std::to_string(transaction.validationCameras.size()) + "|" +
           std::to_string(transaction.rendererBudgetDelta.validationCameraCount) + "|" +
           (transaction.historyInvalidation.Any() ? "dirty" : "clean");
}

} // namespace

SceneIRResolution SceneIRResolver::Resolve(const SceneIRCommand& command,
                                           const SemanticSceneGraph& graph) const {
    SceneIRResolution resolution;
    auto& errors = resolution.errors;

    if (Blank(command.requestId)) errors.push_back("scene IR request id is required");
    if (Blank(command.prompt)) errors.push_back("scene IR prompt/source text is required");
    if (command.seed == 0) errors.push_back("scene IR seed is required");
    if (Blank(command.generator)) errors.push_back("scene IR generator is required");

    std::vector<const SemanticObject*> targets;
    if (!Blank(command.targetSemanticId)) {
        if (const auto* object = graph.FindById(command.targetSemanticId)) {
            targets.push_back(object);
        } else {
            errors.push_back("target semantic id was not found: " + command.targetSemanticId);
        }
    } else if (!Blank(command.targetGroup)) {
        targets = graph.FindByGroup(command.targetGroup);
        if (targets.empty()) {
            errors.push_back("target semantic group was not found: " + command.targetGroup);
        }
    } else if (command.op != SceneIROpType::CreateObject) {
        errors.push_back("scene IR target semantic id or group is required");
    }

    SceneTransaction transaction;
    transaction.id = "tx.ir." + command.requestId;
    transaction.semanticGraphDiff.id = "diff.ir." + command.requestId;
    transaction.provenance.prompt = command.prompt;
    transaction.provenance.seed = command.seed;
    transaction.provenance.generator = command.generator;
    transaction.provenance.sourceAsset = "scene_ir";
    transaction.provenance.validationReport = "scene_ir_resolution";
    transaction.provenance.commitId = transaction.id;

    if (command.op == SceneIROpType::CreateObject) {
        SemanticObject object;
        object.id = command.targetSemanticId.empty() ? ("generated." + command.requestId) : command.targetSemanticId;
        object.editableGroup = command.targetGroup.empty() ? "generated" : command.targetGroup;
        object.semanticType = command.semanticType.empty() ? "generated.object" : command.semanticType;
        object.support = command.support.empty() ? "world" : command.support;
        object.region = command.region.empty() ? "foreground" : command.region;
        object.materialIntent = command.materialIntent.empty() ? "default_pbr" : command.materialIntent;
        object.provenance.prompt = command.prompt;
        object.provenance.seed = command.seed;
        object.provenance.generator = command.generator;
        object.provenance.sourceAsset = "scene_ir";
        object.provenance.validationReport = "scene_ir_resolution";
        object.provenance.commitId = transaction.id;
        object.budget = DefaultIRBudget(1);
        object.invalidation.taaHistory = true;
        object.invalidation.rtReflectionHistory = true;
        object.invalidation.temporalMasks = true;
        object.invalidation.dirtyRegion = object.region;
        object.admission = SemanticAdmissionStatus::Proposed;
        transaction.entityDiff.push_back("create:" + object.id);
        transaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, object});
        targets.push_back(&object);
    } else if (command.op == SceneIROpType::ModifyMaterialIntent) {
        if (Blank(command.materialIntent)) {
            errors.push_back("material intent is required for material modification");
        }
        for (const auto* target : targets) {
            if (!target) continue;
            auto updated = *target;
            updated.materialIntent = command.materialIntent;
            updated.invalidation.taaHistory = true;
            updated.invalidation.rtReflectionHistory = true;
            updated.invalidation.temporalMasks = true;
            updated.invalidation.dirtyRegion = updated.region;
            updated.provenance.prompt = command.prompt;
            updated.provenance.seed = command.seed;
            updated.provenance.generator = command.generator;
            updated.provenance.validationReport = "scene_ir_resolution";
            updated.provenance.commitId = transaction.id;
            transaction.entityDiff.push_back("update_material:" + updated.id);
            transaction.semanticGraphDiff.ops.push_back({SemanticDiffOpType::UpdateObject, *target, updated});
        }
    }

    const uint32_t targetCount = static_cast<uint32_t>(std::max<size_t>(1, transaction.semanticGraphDiff.ops.size()));
    transaction.resourceDiff.textureBytes = 1024ull * 1024ull * targetCount;
    transaction.resourceDiff.texturePages = targetCount;
    transaction.resourceDiff.descriptorCount = targetCount;
    transaction.resourceDiff.psoSignatures = 1;
    transaction.resourceDiff.blasBuilds = targetCount;
    transaction.resourceDiff.tlasInstances = targetCount;
    transaction.resourceDiff.resourceIds.push_back("semantic_ir:" + command.requestId);
    transaction.rendererBudgetDelta = DefaultIRBudget(targetCount);
    transaction.requiredFeatureTiers.push_back({"semantic_ir", "baseline", true});
    transaction.historyInvalidation.taaHistory = true;
    transaction.historyInvalidation.rtReflectionHistory = true;
    transaction.historyInvalidation.temporalMasks = true;
    transaction.historyInvalidation.dirtyRegion = targets.empty() ? "unknown" : targets.front()->region;
    transaction.validationCameras.push_back({"scene_ir_validation_" + command.requestId,
                                             targets.empty() ? "unknown" : targets.front()->region,
                                             "semantic IR resolution validation"});

    if (transaction.semanticGraphDiff.Empty()) {
        errors.push_back("scene IR did not resolve to a semantic graph diff");
    }

    resolution.accepted = errors.empty();
    resolution.transaction = std::move(transaction);
    return resolution;
}

SceneIRCommand MakeTextSceneIR(std::string requestId,
                               std::string targetGroup,
                               std::string materialIntent) {
    return MakeCommon(SceneIRSource::Text, std::move(requestId), std::move(targetGroup), std::move(materialIntent));
}

SceneIRCommand MakeSpeechSceneIR(std::string requestId,
                                 std::string targetGroup,
                                 std::string materialIntent) {
    return MakeCommon(SceneIRSource::Speech, std::move(requestId), std::move(targetGroup), std::move(materialIntent));
}

SceneIRCommand MakeUISceneIR(std::string requestId,
                             std::string targetGroup,
                             std::string materialIntent) {
    return MakeCommon(SceneIRSource::UI, std::move(requestId), std::move(targetGroup), std::move(materialIntent));
}

SceneIRCommand MakeProceduralSceneIR(std::string requestId,
                                     std::string targetGroup,
                                     std::string materialIntent) {
    return MakeCommon(SceneIRSource::Procedural, std::move(requestId), std::move(targetGroup), std::move(materialIntent));
}

const char* ToString(SceneIRSource source) {
    switch (source) {
    case SceneIRSource::Text: return "text";
    case SceneIRSource::Speech: return "speech";
    case SceneIRSource::UI: return "ui";
    case SceneIRSource::Procedural: return "procedural";
    }
    return "unknown";
}

const char* ToString(SceneIROpType op) {
    switch (op) {
    case SceneIROpType::CreateObject: return "create_object";
    case SceneIROpType::ModifyMaterialIntent: return "modify_material_intent";
    case SceneIROpType::SelectSemanticGroup: return "select_semantic_group";
    case SceneIROpType::FocusCamera: return "focus_camera";
    }
    return "unknown";
}

std::string RunSceneIRSelfTestJson() {
    SemanticSceneGraph graph;
    std::string error;
    std::vector<std::string> errors;
    if (!graph.AddObject(MakeSeedObject("rain.table", "tabletop", "wet_wood"), &error)) {
        errors.push_back(error);
    }
    if (!graph.AddObject(MakeSeedObject("rain.glass", "tabletop", "clear_glass"), &error)) {
        errors.push_back(error);
    }

    SceneIRResolver resolver;
    const std::string materialIntent = "wet_chrome_reflection_accents";
    std::vector<SceneIRCommand> commands;
    commands.push_back(MakeTextSceneIR("text", "tabletop", materialIntent));
    commands.push_back(MakeSpeechSceneIR("speech", "tabletop", materialIntent));
    commands.push_back(MakeUISceneIR("ui", "tabletop", materialIntent));
    commands.push_back(MakeProceduralSceneIR("procedural", "tabletop", materialIntent));

    std::vector<SceneIRResolution> resolutions;
    std::vector<std::string> shapes;
    for (const auto& command : commands) {
        auto resolution = resolver.Resolve(command, graph);
        if (!resolution.accepted) {
            errors.insert(errors.end(), resolution.errors.begin(), resolution.errors.end());
        }
        shapes.push_back(CanonicalTransactionShape(resolution.transaction));
        resolutions.push_back(std::move(resolution));
    }

    const bool allSourcesAccepted =
        resolutions.size() == 4 &&
        std::all_of(resolutions.begin(), resolutions.end(), [](const auto& resolution) { return resolution.accepted; });
    const bool equivalentShape =
        shapes.size() == 4 &&
        std::all_of(shapes.begin(), shapes.end(), [&](const auto& shape) { return shape == shapes.front(); });
    const bool groupTargeted =
        !resolutions.empty() &&
        resolutions.front().transaction.semanticGraphDiff.ops.size() == 2;

    auto bad = MakeTextSceneIR("bad_missing_group", "missing_group", materialIntent);
    auto badResolution = resolver.Resolve(bad, graph);
    const bool badRejected = !badResolution.accepted && !badResolution.errors.empty();

    const bool pass = errors.empty() && allSourcesAccepted && equivalentShape && groupTargeted && badRejected;

    nlohmann::json report;
    report["schema"] = "cortex.scene_ir.self_test.v1";
    report["pass"] = pass;
    report["all_sources_accepted"] = allSourcesAccepted;
    report["equivalent_transaction_shape"] = equivalentShape;
    report["group_targeted"] = groupTargeted;
    report["bad_target_rejected"] = badRejected;
    report["resolved_sources"] = nlohmann::json::array();
    for (size_t i = 0; i < commands.size(); ++i) {
        report["resolved_sources"].push_back({
            {"source", ToString(commands[i].source)},
            {"accepted", resolutions[i].accepted},
            {"op_count", resolutions[i].transaction.semanticGraphDiff.ops.size()},
            {"transaction_shape", shapes[i]}
        });
    }
    report["bad_target_errors"] = badResolution.errors;
    report["errors"] = errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
