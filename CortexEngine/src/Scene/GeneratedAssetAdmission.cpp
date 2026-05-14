#include "Scene/GeneratedAssetAdmission.h"

#include <nlohmann/json.hpp>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

nlohmann::json ObligationsToJson(const GeneratedRuntimeAssetObligations& obligations) {
    return {
        {"texture_pages", obligations.texturePages},
        {"resident_texture_bytes", obligations.residentTextureBytes},
        {"pso_signatures", obligations.psoSignatures},
        {"rt_state_objects", obligations.rtStateObjects},
        {"blas_builds", obligations.blasBuilds},
        {"tlas_instances", obligations.tlasInstances},
        {"descriptors", obligations.descriptors},
        {"probe_count", obligations.probeCount}
    };
}

nlohmann::json ReportToJson(const GeneratedAssetAdmissionReport& report) {
    return {
        {"decision", ToString(report.decision)},
        {"asset_id", report.admittedCandidate.assetId},
        {"target_capability_tier", report.admittedCandidate.targetCapabilityTier},
        {"fallback_ready", report.admittedCandidate.fallbackReady},
        {"procedural_density_scale", report.admittedCandidate.proceduralDensityScale},
        {"streaming_ready", report.admittedCandidate.streamingReady},
        {"semantic_validation_ready", report.admittedCandidate.semanticValidationReady},
        {"rt_admission_ready", report.admittedCandidate.rtAdmissionReady},
        {"obligations", ObligationsToJson(report.admittedCandidate.obligations)},
        {"backpressure_decision", ToString(report.backpressure.decision)},
        {"errors", report.errors}
    };
}

RendererBackpressureSnapshot MakeAdmissionSnapshot() {
    RendererBackpressureSnapshot snapshot;
    snapshot.availableTextureBytes = 64ull * 1024ull * 1024ull;
    snapshot.availableGeometryBytes = 128ull * 1024ull * 1024ull;
    snapshot.availableRTStructureBytes = 64ull * 1024ull * 1024ull;
    snapshot.availablePersistentDescriptors = 64;
    snapshot.availableTransientDescriptors = 32;
    snapshot.availableTLASInstances = 64;
    snapshot.pendingBLAS = 1;
    snapshot.pendingRendererBLASJobs = 1;
    return snapshot;
}

GeneratedAssetCandidate MakeCandidate(std::string id) {
    GeneratedAssetCandidate candidate;
    candidate.assetId = std::move(id);
    candidate.sourceGenerator = "asset_admission_self_test";
    candidate.targetCapabilityTier = "baseline_dxr_optional";
    candidate.fallbackReady = true;
    candidate.canDegrade = true;
    candidate.streamingReady = true;
    candidate.semanticValidationReady = true;
    candidate.rtAdmissionReady = true;
    candidate.obligations.texturePages = 8;
    candidate.obligations.residentTextureBytes = 8ull * 1024ull * 1024ull;
    candidate.obligations.psoSignatures = 2;
    candidate.obligations.rtStateObjects = 1;
    candidate.obligations.blasBuilds = 1;
    candidate.obligations.tlasInstances = 1;
    candidate.obligations.descriptors = 4;
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

GeneratedAssetAdmissionReport AdmitGeneratedAsset(const GeneratedAssetCandidate& candidate,
                                                  const RendererBackpressureSnapshot& snapshot) {
    GeneratedAssetAdmissionReport report;
    report.admittedCandidate = candidate;

    if (Blank(candidate.assetId)) AddError(report.errors, "asset_id_missing");
    if (Blank(candidate.sourceGenerator)) AddError(report.errors, "source_generator_missing");
    if (Blank(candidate.targetCapabilityTier)) AddError(report.errors, "target_capability_tier_missing");
    if (!candidate.fallbackReady) AddError(report.errors, "fallback_not_ready");
    if (candidate.obligations.texturePages == 0) AddError(report.errors, "texture_pages_missing");
    if (candidate.obligations.residentTextureBytes == 0) AddError(report.errors, "resident_texture_bytes_missing");
    if (candidate.obligations.psoSignatures == 0) AddError(report.errors, "pso_signatures_missing");
    if (candidate.obligations.probeCount == 0) AddError(report.errors, "probe_count_missing");
    if (candidate.obligations.blasBuilds == 0 && candidate.obligations.tlasInstances > 0) {
        AddError(report.errors, "tlas_without_blas_obligation");
    }
    if (candidate.semanticBudget.validationCameraCount == 0) {
        AddError(report.errors, "validation_camera_budget_missing");
    }
    if (candidate.proceduralDensityScale > 1.0f) {
        if (!candidate.streamingReady) AddError(report.errors, "density_requires_streaming_ready");
        if (!candidate.semanticValidationReady) AddError(report.errors, "density_requires_semantic_validation");
        if (!candidate.rtAdmissionReady) AddError(report.errors, "density_requires_rt_admission");
    }

    ProducerBudgetRequest request;
    request.producerId = candidate.sourceGenerator;
    request.contentId = candidate.assetId;
    request.textureBytes = candidate.obligations.residentTextureBytes;
    request.geometryBytes = candidate.obligations.blasBuilds * 1024ull * 1024ull;
    request.rtStructureBytes = candidate.obligations.blasBuilds * 2ull * 1024ull * 1024ull;
    request.persistentDescriptors = candidate.obligations.descriptors;
    request.transientDescriptors = candidate.obligations.psoSignatures;
    request.tlasInstances = candidate.obligations.tlasInstances;
    request.blasBuilds = candidate.obligations.blasBuilds;
    request.validationCameraCount = candidate.semanticBudget.validationCameraCount;
    request.canDegrade = candidate.canDegrade;
    report.backpressure = EvaluateProducerBudgetRequest(snapshot, request);

    if (!report.errors.empty()) {
        report.decision = GeneratedAssetAdmissionDecision::Reject;
        return report;
    }

    if (report.backpressure.decision == ProducerBudgetDecision::Reject) {
        report.decision = GeneratedAssetAdmissionDecision::Reject;
        report.errors.insert(report.errors.end(), report.backpressure.reasons.begin(), report.backpressure.reasons.end());
        return report;
    }

    if (report.backpressure.decision == ProducerBudgetDecision::Degrade) {
        report.decision = GeneratedAssetAdmissionDecision::Degrade;
        report.admittedCandidate.obligations.residentTextureBytes = report.backpressure.admittedRequest.textureBytes;
        report.admittedCandidate.obligations.descriptors = report.backpressure.admittedRequest.persistentDescriptors;
        report.admittedCandidate.obligations.tlasInstances = report.backpressure.admittedRequest.tlasInstances;
        report.admittedCandidate.semanticBudget.estimatedTextureBytes =
            report.admittedCandidate.obligations.residentTextureBytes;
        report.admittedCandidate.semanticBudget.descriptors =
            report.admittedCandidate.obligations.descriptors;
        report.admittedCandidate.semanticBudget.tlasInstances =
            report.admittedCandidate.obligations.tlasInstances;
        return report;
    }

    report.decision = GeneratedAssetAdmissionDecision::Accept;
    return report;
}

SceneTransaction BuildGeneratedAssetTransaction(const GeneratedAssetAdmissionReport& report) {
    SceneTransaction transaction;
    transaction.id = "tx.generated_asset." + report.admittedCandidate.assetId;
    transaction.entityDiff.push_back("admit_asset:" + report.admittedCandidate.assetId);
    transaction.resourceDiff.textureBytes = report.admittedCandidate.obligations.residentTextureBytes;
    transaction.resourceDiff.texturePages = report.admittedCandidate.obligations.texturePages;
    transaction.resourceDiff.descriptorCount = report.admittedCandidate.obligations.descriptors;
    transaction.resourceDiff.psoSignatures = report.admittedCandidate.obligations.psoSignatures;
    transaction.resourceDiff.blasBuilds = report.admittedCandidate.obligations.blasBuilds;
    transaction.resourceDiff.tlasInstances = report.admittedCandidate.obligations.tlasInstances;
    transaction.resourceDiff.resourceIds = {
        "asset:" + report.admittedCandidate.assetId,
        "pso:" + report.admittedCandidate.assetId,
        "blas:" + report.admittedCandidate.assetId
    };
    transaction.rendererBudgetDelta = report.admittedCandidate.semanticBudget;
    transaction.requiredFeatureTiers.push_back({
        "generated_asset",
        report.admittedCandidate.targetCapabilityTier,
        report.admittedCandidate.fallbackReady
    });
    transaction.historyInvalidation.taaHistory = true;
    transaction.historyInvalidation.rtReflectionHistory = report.admittedCandidate.obligations.rtStateObjects > 0;
    transaction.historyInvalidation.temporalMasks = true;
    transaction.historyInvalidation.dirtyRegion = "generated_asset";
    transaction.validationCameras.push_back({
        "generated_asset_admission_" + report.admittedCandidate.assetId,
        "generated_asset",
        "generated asset admission validation"
    });
    transaction.provenance.prompt = "generated asset admission";
    transaction.provenance.seed = 328;
    transaction.provenance.generator = report.admittedCandidate.sourceGenerator;
    transaction.provenance.sourceAsset = report.admittedCandidate.assetId;
    transaction.provenance.validationReport = "generated_asset_admission";
    transaction.provenance.commitId = transaction.id;
    return transaction;
}

const char* ToString(GeneratedAssetAdmissionDecision decision) {
    switch (decision) {
    case GeneratedAssetAdmissionDecision::Accept: return "accept";
    case GeneratedAssetAdmissionDecision::Degrade: return "degrade";
    case GeneratedAssetAdmissionDecision::Reject: return "reject";
    }
    return "unknown";
}

std::string RunGeneratedAssetAdmissionSelfTestJson() {
    const auto snapshot = MakeAdmissionSnapshot();
    const auto accepted = AdmitGeneratedAsset(MakeCandidate("valid_lantern_asset"), snapshot);

    auto noFallbackCandidate = MakeCandidate("missing_fallback_asset");
    noFallbackCandidate.fallbackReady = false;
    const auto noFallback = AdmitGeneratedAsset(noFallbackCandidate, snapshot);

    auto largeCandidate = MakeCandidate("large_asset");
    largeCandidate.obligations.residentTextureBytes = 128ull * 1024ull * 1024ull;
    largeCandidate.obligations.descriptors = 128;
    largeCandidate.obligations.tlasInstances = 128;
    largeCandidate.semanticBudget.estimatedTextureBytes = largeCandidate.obligations.residentTextureBytes;
    largeCandidate.semanticBudget.descriptors = largeCandidate.obligations.descriptors;
    largeCandidate.semanticBudget.tlasInstances = largeCandidate.obligations.tlasInstances;
    const auto degraded = AdmitGeneratedAsset(largeCandidate, snapshot);

    auto densityCandidate = MakeCandidate("dense_procedural_asset");
    densityCandidate.proceduralDensityScale = 2.0f;
    densityCandidate.streamingReady = false;
    densityCandidate.semanticValidationReady = true;
    densityCandidate.rtAdmissionReady = false;
    const auto densityRejected = AdmitGeneratedAsset(densityCandidate, snapshot);

    auto densityReadyCandidate = MakeCandidate("dense_ready_asset");
    densityReadyCandidate.proceduralDensityScale = 2.0f;
    const auto densityAccepted = AdmitGeneratedAsset(densityReadyCandidate, snapshot);

    const auto transaction = BuildGeneratedAssetTransaction(accepted);
    const bool transactionHasRuntimeAssets =
        transaction.resourceDiff.texturePages > 0 &&
        transaction.resourceDiff.psoSignatures > 0 &&
        transaction.resourceDiff.blasBuilds > 0 &&
        transaction.resourceDiff.tlasInstances > 0 &&
        !transaction.requiredFeatureTiers.empty() &&
        transaction.requiredFeatureTiers.front().fallbackReady;

    const bool pass =
        accepted.decision == GeneratedAssetAdmissionDecision::Accept &&
        noFallback.decision == GeneratedAssetAdmissionDecision::Reject &&
        degraded.decision == GeneratedAssetAdmissionDecision::Degrade &&
        densityRejected.decision == GeneratedAssetAdmissionDecision::Reject &&
        densityAccepted.decision == GeneratedAssetAdmissionDecision::Accept &&
        transactionHasRuntimeAssets;

    nlohmann::json report;
    report["schema"] = "cortex.generated_asset_admission.self_test.v1";
    report["pass"] = pass;
    report["accepted"] = ReportToJson(accepted);
    report["missing_fallback"] = ReportToJson(noFallback);
    report["degraded"] = ReportToJson(degraded);
    report["density_rejected"] = ReportToJson(densityRejected);
    report["density_accepted"] = ReportToJson(densityAccepted);
    report["transaction_has_runtime_assets"] = transactionHasRuntimeAssets;
    report["transaction"] = {
        {"resource_ids", transaction.resourceDiff.resourceIds},
        {"texture_pages", transaction.resourceDiff.texturePages},
        {"pso_signatures", transaction.resourceDiff.psoSignatures},
        {"blas_builds", transaction.resourceDiff.blasBuilds},
        {"tlas_instances", transaction.resourceDiff.tlasInstances},
        {"probe_count", accepted.admittedCandidate.obligations.probeCount},
        {"feature_tier", transaction.requiredFeatureTiers.empty() ? "" : transaction.requiredFeatureTiers.front().tier},
        {"fallback_ready", !transaction.requiredFeatureTiers.empty() && transaction.requiredFeatureTiers.front().fallbackReady}
    };
    return report.dump(2);
}

} // namespace Cortex::Scene
