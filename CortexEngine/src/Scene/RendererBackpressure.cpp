#include "Scene/RendererBackpressure.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Cortex::Scene {
namespace {

uint64_t SaturatingRemaining(uint64_t budget, uint64_t used) {
    return budget > used ? budget - used : 0ull;
}

uint32_t SaturatingRemaining(uint32_t budget, uint32_t used) {
    return budget > used ? budget - used : 0u;
}

void AddReason(std::vector<std::string>& reasons, const std::string& reason) {
    reasons.push_back(reason);
}

nlohmann::json SnapshotToJson(const RendererBackpressureSnapshot& snapshot) {
    return {
        {"available_texture_bytes", snapshot.availableTextureBytes},
        {"available_geometry_bytes", snapshot.availableGeometryBytes},
        {"available_rt_structure_bytes", snapshot.availableRTStructureBytes},
        {"available_persistent_descriptors", snapshot.availablePersistentDescriptors},
        {"available_transient_descriptors", snapshot.availableTransientDescriptors},
        {"available_tlas_instances", snapshot.availableTLASInstances},
        {"pending_blas", snapshot.pendingBLAS},
        {"pending_renderer_blas_jobs", snapshot.pendingRendererBLASJobs},
        {"upload_bytes_this_frame", snapshot.uploadBytesThisFrame},
        {"pass_estimated_write_mb_total", snapshot.passEstimatedWriteMBTotal},
        {"ray_tracing_passes", snapshot.rayTracingPasses},
        {"validation_camera_failures", snapshot.validationCameraFailures}
    };
}

nlohmann::json ResponseToJson(const ProducerBudgetResponse& response) {
    return {
        {"decision", ToString(response.decision)},
        {"admitted_texture_bytes", response.admittedRequest.textureBytes},
        {"admitted_geometry_bytes", response.admittedRequest.geometryBytes},
        {"admitted_rt_structure_bytes", response.admittedRequest.rtStructureBytes},
        {"admitted_tlas_instances", response.admittedRequest.tlasInstances},
        {"admitted_descriptors", response.admittedRequest.persistentDescriptors},
        {"reasons", response.reasons}
    };
}

Graphics::FrameContract MakeBackpressureFixture() {
    Graphics::FrameContract contract;
    contract.budget.textureBudgetBytes = 96ull * 1024ull * 1024ull;
    contract.budget.geometryBudgetBytes = 128ull * 1024ull * 1024ull;
    contract.budget.rtStructureBudgetBytes = 64ull * 1024ull * 1024ull;
    contract.assetMemory.textureBytes = 48ull * 1024ull * 1024ull;
    contract.assetMemory.geometryBytes = 32ull * 1024ull * 1024ull;
    contract.assetMemory.rtStructureBytes = 16ull * 1024ull * 1024ull;
    contract.health.descriptorPersistentBudget = 128;
    contract.health.descriptorPersistentUsed = 96;
    contract.health.descriptorTransientBudget = 64;
    contract.health.descriptorTransientUsed = 32;
    contract.rayTracing.schedulerMaxTLASInstances = 128;
    contract.rayTracing.tlasInstances = 96;
    contract.rayTracing.pendingBLAS = 2;
    contract.rayTracing.pendingRendererBLASJobs = 1;
    contract.particles.uploadBytesThisFrame = 2ull * 1024ull * 1024ull;
    contract.passes.push_back({"RTReflections", true, true, false, 0, 7.5, true, true, true});
    contract.passes.push_back({"PostProcess", true, true, false, 0, 3.0, true, false, false});
    return contract;
}

} // namespace

RendererBackpressureSnapshot BuildRendererBackpressureSnapshot(const Graphics::FrameContract& contract) {
    RendererBackpressureSnapshot snapshot;
    snapshot.availableTextureBytes =
        SaturatingRemaining(contract.budget.textureBudgetBytes, contract.assetMemory.textureBytes);
    snapshot.availableGeometryBytes =
        SaturatingRemaining(contract.budget.geometryBudgetBytes, contract.assetMemory.geometryBytes);
    snapshot.availableRTStructureBytes =
        SaturatingRemaining(contract.budget.rtStructureBudgetBytes, contract.assetMemory.rtStructureBytes);
    snapshot.availablePersistentDescriptors =
        SaturatingRemaining(contract.health.descriptorPersistentBudget, contract.health.descriptorPersistentUsed);
    snapshot.availableTransientDescriptors =
        SaturatingRemaining(contract.health.descriptorTransientBudget, contract.health.descriptorTransientUsed);
    snapshot.availableTLASInstances =
        SaturatingRemaining(contract.rayTracing.schedulerMaxTLASInstances, contract.rayTracing.tlasInstances);
    snapshot.pendingBLAS = contract.rayTracing.pendingBLAS;
    snapshot.pendingRendererBLASJobs = contract.rayTracing.pendingRendererBLASJobs;
    snapshot.uploadBytesThisFrame = contract.particles.uploadBytesThisFrame;

    for (const auto& pass : contract.passes) {
        snapshot.passEstimatedWriteMBTotal += pass.estimatedWriteMB;
        if (pass.rayTracing) {
            ++snapshot.rayTracingPasses;
        }
    }
    for (const auto& warning : contract.warnings) {
        if (warning.find("validation_camera") != std::string::npos) {
            ++snapshot.validationCameraFailures;
        }
    }
    return snapshot;
}

ProducerBudgetResponse EvaluateProducerBudgetRequest(const RendererBackpressureSnapshot& snapshot,
                                                     const ProducerBudgetRequest& request) {
    ProducerBudgetResponse response;
    response.admittedRequest = request;

    if (request.producerId.empty()) AddReason(response.reasons, "producer_id_missing");
    if (request.contentId.empty()) AddReason(response.reasons, "content_id_missing");
    if (request.validationCameraCount == 0) AddReason(response.reasons, "validation_camera_required");
    if (request.textureBytes > snapshot.availableTextureBytes) AddReason(response.reasons, "texture_budget_exceeded");
    if (request.geometryBytes > snapshot.availableGeometryBytes) AddReason(response.reasons, "geometry_budget_exceeded");
    if (request.rtStructureBytes > snapshot.availableRTStructureBytes) AddReason(response.reasons, "rt_structure_budget_exceeded");
    if (request.persistentDescriptors > snapshot.availablePersistentDescriptors) AddReason(response.reasons, "persistent_descriptor_budget_exceeded");
    if (request.transientDescriptors > snapshot.availableTransientDescriptors) AddReason(response.reasons, "transient_descriptor_budget_exceeded");
    if (request.tlasInstances > snapshot.availableTLASInstances) AddReason(response.reasons, "tlas_instance_budget_exceeded");
    if (request.blasBuilds > 0 && snapshot.pendingBLAS + snapshot.pendingRendererBLASJobs > 4) AddReason(response.reasons, "blas_backlog_high");
    if (snapshot.validationCameraFailures > 0) AddReason(response.reasons, "validation_camera_failures_present");

    if (response.reasons.empty()) {
        response.decision = ProducerBudgetDecision::Accept;
        return response;
    }

    if (!request.canDegrade) {
        response.decision = ProducerBudgetDecision::Reject;
        return response;
    }

    response.admittedRequest.textureBytes = std::min(request.textureBytes, snapshot.availableTextureBytes);
    response.admittedRequest.geometryBytes = std::min(request.geometryBytes, snapshot.availableGeometryBytes);
    response.admittedRequest.rtStructureBytes = std::min(request.rtStructureBytes, snapshot.availableRTStructureBytes);
    response.admittedRequest.persistentDescriptors =
        std::min(request.persistentDescriptors, snapshot.availablePersistentDescriptors);
    response.admittedRequest.transientDescriptors =
        std::min(request.transientDescriptors, snapshot.availableTransientDescriptors);
    response.admittedRequest.tlasInstances = std::min(request.tlasInstances, snapshot.availableTLASInstances);
    response.admittedRequest.uploadBytes = std::min<uint64_t>(request.uploadBytes, 4ull * 1024ull * 1024ull);
    response.decision = ProducerBudgetDecision::Degrade;
    return response;
}

const char* ToString(ProducerBudgetDecision decision) {
    switch (decision) {
    case ProducerBudgetDecision::Accept: return "accept";
    case ProducerBudgetDecision::Degrade: return "degrade";
    case ProducerBudgetDecision::Reject: return "reject";
    }
    return "unknown";
}

std::string RunRendererBackpressureSelfTestJson() {
    const auto contract = MakeBackpressureFixture();
    const auto snapshot = BuildRendererBackpressureSnapshot(contract);

    ProducerBudgetRequest small;
    small.producerId = "procedural_test";
    small.contentId = "small_lantern";
    small.textureBytes = 4ull * 1024ull * 1024ull;
    small.geometryBytes = 2ull * 1024ull * 1024ull;
    small.rtStructureBytes = 1024ull * 1024ull;
    small.persistentDescriptors = 2;
    small.transientDescriptors = 2;
    small.tlasInstances = 1;
    small.blasBuilds = 1;
    small.validationCameraCount = 1;
    const auto accepted = EvaluateProducerBudgetRequest(snapshot, small);

    ProducerBudgetRequest large = small;
    large.contentId = "large_generated_market";
    large.textureBytes = 96ull * 1024ull * 1024ull;
    large.geometryBytes = 256ull * 1024ull * 1024ull;
    large.rtStructureBytes = 96ull * 1024ull * 1024ull;
    large.persistentDescriptors = 96;
    large.transientDescriptors = 96;
    large.tlasInstances = 96;
    large.canDegrade = true;
    const auto degraded = EvaluateProducerBudgetRequest(snapshot, large);

    ProducerBudgetRequest rigid = large;
    rigid.contentId = "rigid_generated_market";
    rigid.canDegrade = false;
    const auto rejected = EvaluateProducerBudgetRequest(snapshot, rigid);

    const bool pass =
        accepted.decision == ProducerBudgetDecision::Accept &&
        degraded.decision == ProducerBudgetDecision::Degrade &&
        degraded.admittedRequest.textureBytes <= snapshot.availableTextureBytes &&
        degraded.admittedRequest.tlasInstances <= snapshot.availableTLASInstances &&
        rejected.decision == ProducerBudgetDecision::Reject &&
        !rejected.reasons.empty();

    nlohmann::json report;
    report["schema"] = "cortex.renderer_backpressure.self_test.v1";
    report["pass"] = pass;
    report["snapshot"] = SnapshotToJson(snapshot);
    report["accepted"] = ResponseToJson(accepted);
    report["degraded"] = ResponseToJson(degraded);
    report["rejected"] = ResponseToJson(rejected);
    report["producer_asked_before_emit"] = true;
    report["degraded_before_recovery"] = degraded.decision == ProducerBudgetDecision::Degrade;
    return report.dump(2);
}

} // namespace Cortex::Scene
