#include "Scene/SemanticGraph.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Cortex::Scene {
namespace {

bool IsBlank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void SetError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

void EraseOrdered(std::vector<std::string>& values, const std::string& value) {
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
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

nlohmann::json InvalidationToJson(const SemanticInvalidation& invalidation) {
    return {
        {"taa_history", invalidation.taaHistory},
        {"rt_reflection_history", invalidation.rtReflectionHistory},
        {"rt_gi_history", invalidation.rtGIHistory},
        {"temporal_masks", invalidation.temporalMasks},
        {"dirty_region", invalidation.dirtyRegion},
        {"any", invalidation.Any()}
    };
}

} // namespace

bool SemanticProvenance::HasReproducibleSource() const {
    return !IsBlank(prompt) &&
           seed != 0 &&
           !IsBlank(generator) &&
           !IsBlank(validationReport) &&
           !IsBlank(commitId);
}

bool SemanticBudget::FitsWithin(const SemanticBudget& limit) const {
    return estimatedTextureBytes <= limit.estimatedTextureBytes &&
           texturePages <= limit.texturePages &&
           psoSignatures <= limit.psoSignatures &&
           blasBuilds <= limit.blasBuilds &&
           tlasInstances <= limit.tlasInstances &&
           descriptors <= limit.descriptors &&
           validationCameraCount <= limit.validationCameraCount;
}

bool SemanticInvalidation::Any() const {
    return taaHistory || rtReflectionHistory || rtGIHistory || temporalMasks || !IsBlank(dirtyRegion);
}

SemanticGraphDiff SemanticGraphDiff::Inverted(std::string inverseId) const {
    SemanticGraphDiff inverted;
    inverted.id = std::move(inverseId);
    inverted.ops.reserve(ops.size());

    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        SemanticGraphDiffOp op;
        switch (it->type) {
        case SemanticDiffOpType::AddObject:
            op.type = SemanticDiffOpType::RemoveObject;
            op.before = it->after;
            break;
        case SemanticDiffOpType::UpdateObject:
            op.type = SemanticDiffOpType::UpdateObject;
            op.before = it->after;
            op.after = it->before;
            break;
        case SemanticDiffOpType::RemoveObject:
            op.type = SemanticDiffOpType::AddObject;
            op.after = it->before;
            break;
        }
        inverted.ops.push_back(std::move(op));
    }

    return inverted;
}

bool SemanticSceneGraph::AddObject(const SemanticObject& object, std::string* error) {
    if (IsBlank(object.id)) {
        SetError(error, "semantic object id is required");
        return false;
    }
    if (m_objects.contains(object.id)) {
        SetError(error, "semantic object already exists: " + object.id);
        return false;
    }

    m_objects.emplace(object.id, object);
    m_order.push_back(object.id);
    return true;
}

bool SemanticSceneGraph::UpsertObject(const SemanticObject& object, std::string* error) {
    if (IsBlank(object.id)) {
        SetError(error, "semantic object id is required");
        return false;
    }
    if (!m_objects.contains(object.id)) {
        m_order.push_back(object.id);
    }
    m_objects[object.id] = object;
    return true;
}

bool SemanticSceneGraph::RemoveObject(const std::string& id, std::string* error) {
    if (!m_objects.contains(id)) {
        SetError(error, "semantic object does not exist: " + id);
        return false;
    }
    m_objects.erase(id);
    EraseOrdered(m_order, id);
    return true;
}

bool SemanticSceneGraph::ApplyDiff(const SemanticGraphDiff& diff, std::string* error) {
    for (const auto& op : diff.ops) {
        switch (op.type) {
        case SemanticDiffOpType::AddObject:
            if (!op.after) {
                SetError(error, "add diff op missing after object");
                return false;
            }
            if (!AddObject(*op.after, error)) {
                return false;
            }
            break;
        case SemanticDiffOpType::UpdateObject:
            if (!op.after) {
                SetError(error, "update diff op missing after object");
                return false;
            }
            if (op.before) {
                const auto existing = FindById(op.before->id);
                if (!existing) {
                    SetError(error, "update before object is not present: " + op.before->id);
                    return false;
                }
            }
            if (!UpsertObject(*op.after, error)) {
                return false;
            }
            break;
        case SemanticDiffOpType::RemoveObject:
            if (!op.before) {
                SetError(error, "remove diff op missing before object");
                return false;
            }
            if (!RemoveObject(op.before->id, error)) {
                return false;
            }
            break;
        }
    }
    return true;
}

const SemanticObject* SemanticSceneGraph::FindById(const std::string& id) const {
    const auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : &it->second;
}

std::vector<const SemanticObject*> SemanticSceneGraph::FindByGroup(const std::string& group) const {
    std::vector<const SemanticObject*> result;
    for (const auto& id : m_order) {
        const auto it = m_objects.find(id);
        if (it != m_objects.end() && it->second.editableGroup == group) {
            result.push_back(&it->second);
        }
    }
    return result;
}

std::vector<const SemanticObject*> SemanticSceneGraph::FindByRegion(const std::string& region) const {
    std::vector<const SemanticObject*> result;
    for (const auto& id : m_order) {
        const auto it = m_objects.find(id);
        if (it != m_objects.end() && it->second.region == region) {
            result.push_back(&it->second);
        }
    }
    return result;
}

std::vector<SemanticRuntimeObjectPlan> SemanticSceneGraph::CompileRuntimePlan() const {
    std::vector<SemanticRuntimeObjectPlan> plan;
    plan.reserve(m_order.size());

    for (const auto& id : m_order) {
        const auto it = m_objects.find(id);
        if (it == m_objects.end()) {
            continue;
        }

        const auto& object = it->second;
        plan.push_back({
            object.id,
            object.editableGroup,
            object.semanticType,
            object.region,
            object.materialIntent,
            object.budget,
            object.invalidation,
            object.linkedEntity
        });
    }

    return plan;
}

std::vector<std::string> SemanticSceneGraph::ValidateV0Objects() const {
    std::vector<std::string> errors;
    for (const auto& id : m_order) {
        const auto it = m_objects.find(id);
        if (it == m_objects.end()) {
            errors.push_back("ordered semantic id missing from object map: " + id);
            continue;
        }
        const auto& object = it->second;
        if (IsBlank(object.id)) errors.push_back("object id is blank");
        if (IsBlank(object.editableGroup)) errors.push_back(object.id + ": editable group is required");
        if (IsBlank(object.semanticType)) errors.push_back(object.id + ": semantic type is required");
        if (IsBlank(object.support)) errors.push_back(object.id + ": support relation is required");
        if (IsBlank(object.region)) errors.push_back(object.id + ": region is required");
        if (IsBlank(object.materialIntent)) errors.push_back(object.id + ": material intent is required");
        if (!object.provenance.HasReproducibleSource()) errors.push_back(object.id + ": reproducible provenance is incomplete");
        if (object.budget.validationCameraCount == 0) errors.push_back(object.id + ": validation camera budget is required");
        if (!object.invalidation.Any()) errors.push_back(object.id + ": invalidation mask is required");
    }
    return errors;
}

const char* ToString(SemanticAdmissionStatus status) {
    switch (status) {
    case SemanticAdmissionStatus::Draft: return "draft";
    case SemanticAdmissionStatus::Proposed: return "proposed";
    case SemanticAdmissionStatus::Validated: return "validated";
    case SemanticAdmissionStatus::Accepted: return "accepted";
    case SemanticAdmissionStatus::Rejected: return "rejected";
    }
    return "unknown";
}

const char* ToString(SemanticDiffOpType type) {
    switch (type) {
    case SemanticDiffOpType::AddObject: return "add_object";
    case SemanticDiffOpType::UpdateObject: return "update_object";
    case SemanticDiffOpType::RemoveObject: return "remove_object";
    }
    return "unknown";
}

std::string RunSemanticGraphSelfTestJson() {
    SemanticSceneGraph graph;
    std::vector<std::string> errors;

    auto makeObject = [](std::string id, std::string group, std::string type, std::string region) {
        SemanticObject object;
        object.id = std::move(id);
        object.editableGroup = std::move(group);
        object.semanticType = std::move(type);
        object.support = "pavilion_floor";
        object.region = std::move(region);
        object.materialIntent = "wet_chrome_glass";
        object.provenance.prompt = "make the rain pavilion tabletop read as a material vignette";
        object.provenance.seed = 328;
        object.provenance.generator = "semantic_graph_self_test";
        object.provenance.sourceAsset = "hand_authored/rain_glass_pavilion";
        object.provenance.validationReport = "semantic_graph_self_test";
        object.provenance.commitId = "runtime_self_test";
        object.budget.estimatedTextureBytes = 4ull * 1024ull * 1024ull;
        object.budget.texturePages = 4;
        object.budget.psoSignatures = 1;
        object.budget.blasBuilds = 1;
        object.budget.tlasInstances = 1;
        object.budget.descriptors = 3;
        object.budget.validationCameraCount = 2;
        object.invalidation.taaHistory = true;
        object.invalidation.rtReflectionHistory = true;
        object.invalidation.temporalMasks = true;
        object.invalidation.dirtyRegion = object.region;
        object.admission = SemanticAdmissionStatus::Proposed;
        return object;
    };

    std::string error;
    SemanticObject table = makeObject("rain.table", "tabletop", "prop.table", "foreground");
    SemanticObject glass = makeObject("rain.glass", "tabletop", "prop.glassware", "foreground");
    if (!graph.AddObject(table, &error)) errors.push_back(error);
    if (!graph.AddObject(glass, &error)) errors.push_back(error);

    auto updatedTable = table;
    updatedTable.materialIntent = "wet_wood_chrome_reflections";
    updatedTable.admission = SemanticAdmissionStatus::Validated;
    SemanticObject lantern = makeObject("rain.lantern", "lighting_accents", "light.warm_lantern", "midground");
    lantern.support = "tabletop";
    lantern.invalidation.rtGIHistory = true;

    SemanticGraphDiff diff;
    diff.id = "self_test_diff";
    diff.ops.push_back({SemanticDiffOpType::UpdateObject, table, updatedTable});
    diff.ops.push_back({SemanticDiffOpType::AddObject, std::nullopt, lantern});
    if (!graph.ApplyDiff(diff, &error)) errors.push_back(error);

    const bool updatedMaterial =
        graph.FindById("rain.table") &&
        graph.FindById("rain.table")->materialIntent == "wet_wood_chrome_reflections";
    const bool groupLookup = graph.FindByGroup("tabletop").size() == 2;
    const bool regionLookup = graph.FindByRegion("foreground").size() == 2;
    const auto runtimePlan = graph.CompileRuntimePlan();
    auto v0Errors = graph.ValidateV0Objects();
    errors.insert(errors.end(), v0Errors.begin(), v0Errors.end());

    auto inverse = diff.Inverted("self_test_inverse");
    const bool undoApplied = graph.ApplyDiff(inverse, &error);
    if (!undoApplied) {
        errors.push_back(error);
    }
    const bool undoRestored =
        graph.ObjectCount() == 2 &&
        graph.FindById("rain.table") &&
        graph.FindById("rain.table")->materialIntent == "wet_chrome_glass" &&
        graph.FindById("rain.lantern") == nullptr;

    const bool pass =
        errors.empty() &&
        updatedMaterial &&
        groupLookup &&
        regionLookup &&
        runtimePlan.size() == 3 &&
        undoApplied &&
        undoRestored;

    nlohmann::json result;
    result["schema"] = "cortex.semantic_graph.self_test.v1";
    result["pass"] = pass;
    result["object_count_after_undo"] = graph.ObjectCount();
    result["group_lookup_tabletop"] = graph.FindByGroup("tabletop").size();
    result["region_lookup_foreground"] = graph.FindByRegion("foreground").size();
    result["runtime_plan_count_before_undo"] = runtimePlan.size();
    result["diff"] = {
        {"id", diff.id},
        {"op_count", diff.ops.size()},
        {"inverted_id", inverse.id},
        {"inverted_op_count", inverse.ops.size()},
        {"updated_material", updatedMaterial},
        {"undo_applied", undoApplied},
        {"undo_restored", undoRestored}
    };
    result["required_v0_fields"] = {
        {"object_identity", true},
        {"editable_group", true},
        {"semantic_type", true},
        {"support_relation", true},
        {"region", true},
        {"material_intent", true},
        {"provenance", table.provenance.HasReproducibleSource()},
        {"budget", table.budget.validationCameraCount > 0},
        {"invalidation", table.invalidation.Any()},
        {"admission_status", std::string(ToString(table.admission))}
    };
    result["sample_budget"] = BudgetToJson(table.budget);
    result["sample_invalidation"] = InvalidationToJson(table.invalidation);
    result["errors"] = errors;

    return result.dump(2);
}

} // namespace Cortex::Scene
