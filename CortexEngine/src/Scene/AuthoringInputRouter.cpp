#include "Scene/AuthoringInputRouter.h"

#include <nlohmann/json.hpp>
#include <utility>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

SceneIRCommand MakeCommand(const AuthoringInputRequest& request) {
    SceneIRCommand command;
    command.source = request.source;
    command.op = SceneIROpType::ModifyMaterialIntent;
    command.requestId = request.requestId;
    command.targetGroup = request.targetGroup;
    command.materialIntent = request.materialIntent;
    command.prompt = "route authoring input to " + request.targetGroup;
    command.seed = 328;
    command.generator = std::string("authoring_router_") + ToString(request.source);
    return command;
}

SemanticObject MakeRouterObject(std::string id, std::string materialIntent) {
    SemanticObject object;
    object.id = std::move(id);
    object.editableGroup = "tabletop";
    object.semanticType = "prop.material_target";
    object.support = "world";
    object.region = "foreground";
    object.materialIntent = std::move(materialIntent);
    object.provenance.prompt = "authoring router fixture";
    object.provenance.seed = 328;
    object.provenance.generator = "authoring_input_router";
    object.provenance.sourceAsset = "router_fixture";
    object.provenance.validationReport = "authoring_input_router";
    object.provenance.commitId = "router_fixture";
    object.budget.estimatedTextureBytes = 1024 * 1024;
    object.budget.texturePages = 1;
    object.budget.psoSignatures = 1;
    object.budget.blasBuilds = 1;
    object.budget.tlasInstances = 1;
    object.budget.descriptors = 1;
    object.budget.validationCameraCount = 1;
    object.invalidation.taaHistory = true;
    object.invalidation.rtReflectionHistory = true;
    object.invalidation.temporalMasks = true;
    object.invalidation.dirtyRegion = object.region;
    object.admission = SemanticAdmissionStatus::Accepted;
    return object;
}

RendererBackpressureSnapshot MakeRouterSnapshot() {
    RendererBackpressureSnapshot snapshot;
    snapshot.availableTextureBytes = 64ull * 1024ull * 1024ull;
    snapshot.availableGeometryBytes = 128ull * 1024ull * 1024ull;
    snapshot.availableRTStructureBytes = 64ull * 1024ull * 1024ull;
    snapshot.availablePersistentDescriptors = 64;
    snapshot.availableTransientDescriptors = 32;
    snapshot.availableTLASInstances = 64;
    return snapshot;
}

GeneratedAssetCandidate MakeRouterAsset() {
    GeneratedAssetCandidate candidate;
    candidate.assetId = "router_generated_asset";
    candidate.sourceGenerator = "authoring_router_procedural";
    candidate.targetCapabilityTier = "baseline_dxr_optional";
    candidate.fallbackReady = true;
    candidate.canDegrade = true;
    candidate.streamingReady = true;
    candidate.semanticValidationReady = true;
    candidate.rtAdmissionReady = true;
    candidate.obligations.texturePages = 4;
    candidate.obligations.residentTextureBytes = 4ull * 1024ull * 1024ull;
    candidate.obligations.psoSignatures = 1;
    candidate.obligations.rtStateObjects = 1;
    candidate.obligations.blasBuilds = 1;
    candidate.obligations.tlasInstances = 1;
    candidate.obligations.descriptors = 2;
    candidate.obligations.probeCount = 1;
    candidate.semanticBudget.estimatedTextureBytes = candidate.obligations.residentTextureBytes;
    candidate.semanticBudget.texturePages = candidate.obligations.texturePages;
    candidate.semanticBudget.psoSignatures = candidate.obligations.psoSignatures;
    candidate.semanticBudget.blasBuilds = candidate.obligations.blasBuilds;
    candidate.semanticBudget.tlasInstances = candidate.obligations.tlasInstances;
    candidate.semanticBudget.descriptors = candidate.obligations.descriptors;
    candidate.semanticBudget.validationCameraCount = 1;
    return candidate;
}

} // namespace

AuthoringInputRouteResult RouteAuthoringInput(const AuthoringInputRequest& request,
                                              SceneRuntimeMutationState& state,
                                              const SceneTransactionValidator& validator,
                                              const RendererBackpressureSnapshot& snapshot) {
    AuthoringInputRouteResult result;
    if (Blank(request.requestId)) AddError(result.errors, "authoring request id is required");
    if (request.arbitraryEntityCount > 32) AddError(result.errors, "unconstrained large LLM entity list rejected");
    if (!result.errors.empty()) {
        return result;
    }

    if (request.generatedAssetProducer) {
        const auto admission = AdmitGeneratedAsset(request.generatedAsset, snapshot);
        result.generatedAssetAskedBudgetBeforeEmit = true;
        if (admission.decision == GeneratedAssetAdmissionDecision::Reject) {
            result.errors = admission.errors;
            return result;
        }
        result.transaction = BuildGeneratedAssetTransaction(admission);
        result.accepted = true;
        return result;
    }

    SceneIRResolver resolver;
    const auto ir = resolver.Resolve(MakeCommand(request), state.graph);
    result.compiledToSceneIR = ir.accepted;
    result.targetedSemanticGroup = ir.accepted && !request.targetGroup.empty();
    if (!ir.accepted) {
        result.errors = ir.errors;
        return result;
    }

    SceneTransactionValidationResult validation;
    const auto receipt = ApplyTransactionToRuntime(ir.transaction, state, validator, &validation);
    result.runtimeTransactionApplied = receipt.committed;
    result.transaction = ir.transaction;
    if (!validation.accepted) {
        result.errors = validation.errors;
        return result;
    }
    result.accepted = receipt.committed;
    return result;
}

std::string RunAuthoringInputRouterSelfTestJson() {
    SemanticBudget limit;
    limit.estimatedTextureBytes = 64ull * 1024ull * 1024ull;
    limit.texturePages = 64;
    limit.psoSignatures = 16;
    limit.blasBuilds = 16;
    limit.tlasInstances = 16;
    limit.descriptors = 128;
    limit.validationCameraCount = 8;
    SceneTransactionValidator validator(limit);
    const auto snapshot = MakeRouterSnapshot();

    std::vector<std::string> errors;
    SceneRuntimeMutationState textState;
    std::string error;
    if (!textState.graph.AddObject(MakeRouterObject("router.table", "wet_wood"), &error)) errors.push_back(error);
    if (!textState.graph.AddObject(MakeRouterObject("router.glass", "clear_glass"), &error)) errors.push_back(error);

    auto text = AuthoringInputRequest{SceneIRSource::Text, "text", "tabletop", "wet_chrome_reflection"};
    auto speech = AuthoringInputRequest{SceneIRSource::Speech, "speech", "tabletop", "wet_chrome_reflection"};
    auto ui = AuthoringInputRequest{SceneIRSource::UI, "ui", "tabletop", "wet_chrome_reflection"};
    auto procedural = AuthoringInputRequest{SceneIRSource::Procedural, "procedural", "tabletop", "wet_chrome_reflection"};

    auto textResult = RouteAuthoringInput(text, textState, validator, snapshot);
    SceneRuntimeMutationState speechState = textState;
    SceneRuntimeMutationState uiState = textState;
    SceneRuntimeMutationState proceduralState = textState;
    auto speechResult = RouteAuthoringInput(speech, speechState, validator, snapshot);
    auto uiResult = RouteAuthoringInput(ui, uiState, validator, snapshot);
    auto proceduralResult = RouteAuthoringInput(procedural, proceduralState, validator, snapshot);

    SceneRuntimeMutationState llmState = textState;
    auto largeLlm = text;
    largeLlm.requestId = "large_llm";
    largeLlm.arbitraryEntityCount = 1000;
    const auto largeRejected = RouteAuthoringInput(largeLlm, llmState, validator, snapshot);

    SceneRuntimeMutationState assetState = textState;
    AuthoringInputRequest assetRequest;
    assetRequest.source = SceneIRSource::Procedural;
    assetRequest.requestId = "asset";
    assetRequest.generatedAssetProducer = true;
    assetRequest.generatedAsset = MakeRouterAsset();
    const auto assetResult = RouteAuthoringInput(assetRequest, assetState, validator, snapshot);

    const bool allSourcesAccepted =
        textResult.accepted && speechResult.accepted && uiResult.accepted && proceduralResult.accepted;
    const bool allCompiledToIR =
        textResult.compiledToSceneIR && speechResult.compiledToSceneIR && uiResult.compiledToSceneIR && proceduralResult.compiledToSceneIR;
    const bool groupTargeted =
        textResult.targetedSemanticGroup && speechResult.targetedSemanticGroup && uiResult.targetedSemanticGroup && proceduralResult.targetedSemanticGroup;
    const bool largeRejectedBeforeMutation =
        !largeRejected.accepted && !largeRejected.errors.empty() && llmState.ecsEntityJobs.size() == textState.ecsEntityJobs.size();
    const bool producerAskedBudget =
        assetResult.accepted && assetResult.generatedAssetAskedBudgetBeforeEmit;
    const bool pass = errors.empty() && allSourcesAccepted && allCompiledToIR && groupTargeted && largeRejectedBeforeMutation && producerAskedBudget;

    nlohmann::json report;
    report["schema"] = "cortex.authoring_input_router.self_test.v1";
    report["pass"] = pass;
    report["all_sources_accepted"] = allSourcesAccepted;
    report["all_compiled_to_scene_ir"] = allCompiledToIR;
    report["all_targeted_semantic_groups"] = groupTargeted;
    report["large_llm_rejected_before_mutation"] = largeRejectedBeforeMutation;
    report["generated_asset_asked_budget_before_emit"] = producerAskedBudget;
    report["errors"] = errors;
    report["large_llm_errors"] = largeRejected.errors;
    return report.dump(2);
}

} // namespace Cortex::Scene
