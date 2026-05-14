#include "Scene/SemanticRuntimeCompiler.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace Cortex::Scene {
namespace {

SemanticObject MakeCompilerObject(std::string id,
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
    object.provenance.prompt = "semantic runtime compiler fixture";
    object.provenance.seed = 328;
    object.provenance.generator = "semantic_runtime_compiler";
    object.provenance.sourceAsset = "compiler_fixture";
    object.provenance.validationReport = "semantic_runtime_compiler";
    object.provenance.commitId = "compiler_fixture";
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
    object.invalidation.dirtyRegion = object.region;
    object.admission = SemanticAdmissionStatus::Validated;
    return object;
}

} // namespace

CompiledSemanticRuntimeScene CompileSemanticGraphForRuntime(const SemanticSceneGraph& graph) {
    CompiledSemanticRuntimeScene compiled;
    for (const auto& plan : graph.CompileRuntimePlan()) {
        if (plan.semanticId.empty()) {
            compiled.errors.push_back("runtime plan missing semantic id");
            continue;
        }

        SemanticEcsEntityJob entityJob;
        entityJob.semanticId = plan.semanticId;
        entityJob.editableGroup = plan.editableGroup;
        entityJob.semanticType = plan.semanticType;
        entityJob.materialIntent = plan.materialIntent;
        compiled.ecsEntityJobs.push_back(std::move(entityJob));

        SemanticRendererResourceJob resourceJob;
        resourceJob.semanticId = plan.semanticId;
        resourceJob.textureBytes = plan.budget.estimatedTextureBytes;
        resourceJob.texturePages = plan.budget.texturePages;
        resourceJob.psoSignatures = plan.budget.psoSignatures;
        resourceJob.blasBuilds = plan.budget.blasBuilds;
        resourceJob.tlasInstances = plan.budget.tlasInstances;
        resourceJob.invalidation = plan.invalidation;
        compiled.rendererResourceJobs.push_back(std::move(resourceJob));
    }
    if (compiled.ecsEntityJobs.empty()) {
        compiled.errors.push_back("semantic graph produced no ECS entity jobs");
    }
    if (compiled.rendererResourceJobs.empty()) {
        compiled.errors.push_back("semantic graph produced no renderer resource jobs");
    }
    return compiled;
}

std::string RunSemanticRuntimeCompilerSelfTestJson() {
    SemanticSceneGraph graph;
    std::string error;
    std::vector<std::string> errors;
    const auto floor = MakeCompilerObject("compiler.floor", "room", "support.floor", "foreground", "wet_floor");
    auto lantern = MakeCompilerObject("compiler.lantern", "lighting", "light.lantern", "foreground", "warm_glass");
    lantern.support = floor.id;
    if (!graph.AddObject(floor, &error)) errors.push_back(error);
    if (!graph.AddObject(lantern, &error)) errors.push_back(error);

    const auto compiled = CompileSemanticGraphForRuntime(graph);
    const bool hasEcsJobs = compiled.ecsEntityJobs.size() == 2;
    const bool hasResourceJobs = compiled.rendererResourceJobs.size() == 2;
    bool hasInvalidation = false;
    for (const auto& job : compiled.rendererResourceJobs) {
        hasInvalidation = hasInvalidation || job.invalidation.Any();
    }

    const bool pass =
        errors.empty() &&
        compiled.errors.empty() &&
        hasEcsJobs &&
        hasResourceJobs &&
        hasInvalidation;

    nlohmann::json report;
    report["schema"] = "cortex.semantic_runtime_compiler.self_test.v1";
    report["pass"] = pass;
    report["ecs_entity_jobs"] = compiled.ecsEntityJobs.size();
    report["renderer_resource_jobs"] = compiled.rendererResourceJobs.size();
    report["has_invalidation_hints"] = hasInvalidation;
    report["errors"] = errors;
    report["compiler_errors"] = compiled.errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
