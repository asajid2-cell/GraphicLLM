#pragma once

#include "Graphics/FrameContract.h"
#include "Graphics/MaterialModel.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace Cortex::Graphics {

enum class FullSceneShaderPromotionState : uint8_t {
    Planned = 0,
    Instrumented,
    ShadowOutput,
    Candidate,
    PacketPassed,
    CrossFamilyPassed,
    DefaultReady
};

inline const char* ToString(FullSceneShaderPromotionState state) {
    switch (state) {
    case FullSceneShaderPromotionState::Planned: return "planned";
    case FullSceneShaderPromotionState::Instrumented: return "instrumented";
    case FullSceneShaderPromotionState::ShadowOutput: return "shadow_output";
    case FullSceneShaderPromotionState::Candidate: return "candidate";
    case FullSceneShaderPromotionState::PacketPassed: return "packet_passed";
    case FullSceneShaderPromotionState::CrossFamilyPassed: return "cross_family_passed";
    case FullSceneShaderPromotionState::DefaultReady: return "default_ready";
    }
    return "planned";
}

struct FullSceneShaderDomainEvidence {
    std::string id;
    bool enabled = false;
    bool ready = false;
    FullSceneShaderPromotionState promotionState = FullSceneShaderPromotionState::Instrumented;
    std::string owner = "unknown";
    std::string fallbackOwner = "v1_fallback";
    std::string failureReason = "V2 domain is instrumented but not promoted to beauty output";
};

struct FullSceneShaderFrameContext {
    std::string schema = "cortex.full_scene_shader_pipeline_v2.runtime_report.v1";
    std::string status = "runtime_placeholder_v1_fallback";
    std::string beautyOutput = "v1_fallback";

    bool familyCountsAvailable = false;
    bool reflectionPoliciesAvailable = false;
    bool temporalPoliciesAvailable = false;
    bool postPoliciesAvailable = false;
    bool velocityReady = false;
    bool extendedMaterialChannelsReady = false;
    bool materialPolicyChannelReady = false;
    bool jitterReprojectionReady = false;
    bool reflectionOwnerReportAvailable = false;
    bool unknownReflectionOwner = true;
    bool rtMissEnvironmentPolicyReady = false;
    bool sceneLocalEnvironmentShaderReady = false;
    bool postNamedStagesReady = false;
    bool explicitPassGraphReady = false;

    FullSceneMaterialModelEvidence materialModelEvidence;

    FullSceneShaderDomainEvidence material;
    FullSceneShaderDomainEvidence gbuffer;
    FullSceneShaderDomainEvidence lighting;
    FullSceneShaderDomainEvidence reflections;
    FullSceneShaderDomainEvidence shadows;
    FullSceneShaderDomainEvidence temporal;
    FullSceneShaderDomainEvidence post;
    FullSceneShaderDomainEvidence renderGraph;
    FullSceneShaderDomainEvidence assetEvidence;
    FullSceneShaderDomainEvidence packetGate;
};

inline uint32_t FullSceneShaderSceneMaterialFamilyCount(const FrameContract::MaterialStats& materials) {
    return materials.sceneMaterialDefault +
           materials.sceneMaterialPaintedWall +
           materials.sceneMaterialCeramicTile +
           materials.sceneMaterialPolishedWood +
           materials.sceneMaterialBrushedMetal +
           materials.sceneMaterialPolishedMetal +
           materials.sceneMaterialGlassPane +
           materials.sceneMaterialFabric +
           materials.sceneMaterialPlastic +
           materials.sceneMaterialWetSurface +
           materials.sceneMaterialEmissiveNeon +
           materials.sceneMaterialScreenPanel +
           materials.sceneMaterialConcrete +
           materials.sceneMaterialRubber +
           materials.sceneMaterialWater +
           materials.sceneMaterialMirror;
}

inline uint32_t FullSceneShaderMaterialReflectionPreferenceCount(const FrameContract::MaterialStats& materials) {
    return materials.materialReflectionNeutralFallback +
           materials.materialReflectionLocalProbe +
           materials.materialReflectionProbeGrid +
           materials.materialReflectionPlanarProbe +
           materials.materialReflectionSSR +
           materials.materialReflectionRT;
}

inline uint32_t FullSceneShaderMaterialTemporalPolicyCount(const FrameContract::MaterialStats& materials) {
    return materials.materialTemporalStableDiffuse +
           materials.materialTemporalStableGlossy +
           materials.materialTemporalMirrorLocked +
           materials.materialTemporalEmissiveLocked +
           materials.materialTemporalWaterViewDependent;
}

inline uint32_t FullSceneShaderMaterialPostSensitivityCount(const FrameContract::MaterialStats& materials) {
    return materials.materialPostNormal +
           materials.materialPostBloomEmitter +
           materials.materialPostExposureProtected +
           materials.materialPostWetHighlight;
}

inline bool FullSceneShaderHasResource(const FrameContract& contract, const char* name) {
    return std::any_of(
        contract.resources.begin(),
        contract.resources.end(),
        [name](const FrameContract::ResourceInfo& resource) {
            return resource.name == name && resource.valid && resource.sizeMatchesContract;
        });
}

inline FullSceneShaderDomainEvidence MakeFullSceneShaderDomainEvidence(
    std::string id,
    bool enabled,
    bool ready,
    std::string owner,
    std::string failureReason,
    FullSceneShaderPromotionState promotionState = FullSceneShaderPromotionState::Instrumented) {
    FullSceneShaderDomainEvidence evidence;
    evidence.id = std::move(id);
    evidence.enabled = enabled;
    evidence.ready = ready;
    evidence.owner = std::move(owner);
    evidence.failureReason = std::move(failureReason);
    evidence.promotionState = promotionState;
    return evidence;
}

inline FullSceneShaderFrameContext BuildFullSceneShaderFrameContext(const FrameContract& contract) {
    FullSceneShaderFrameContext context;
    context.materialModelEvidence = BuildFullSceneMaterialModelEvidence(contract.materials);
    context.familyCountsAvailable = context.materialModelEvidence.familyCountsAvailable;
    context.reflectionPoliciesAvailable = context.materialModelEvidence.reflectionPoliciesAvailable;
    context.temporalPoliciesAvailable = context.materialModelEvidence.temporalPoliciesAvailable;
    context.postPoliciesAvailable = context.materialModelEvidence.postPoliciesAvailable;
    context.extendedMaterialChannelsReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext0") &&
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext1") &&
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext2");
    context.materialPolicyChannelReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext2") &&
        context.familyCountsAvailable &&
        context.reflectionPoliciesAvailable &&
        context.temporalPoliciesAvailable &&
        context.postPoliciesAvailable;
    context.velocityReady =
        FullSceneShaderHasResource(contract, "velocity") &&
        contract.motionVectors.planned &&
        contract.motionVectors.executed;
    context.jitterReprojectionReady =
        contract.features.taaEnabled &&
        context.velocityReady &&
        contract.temporalMask.built &&
        FullSceneShaderHasResource(contract, "temporal_rejection_mask");
    context.reflectionOwnerReportAvailable =
        contract.sceneVisual.pixelReflectionOwnerHistogramAvailable ||
        contract.sceneVisual.reflectionOwnerDebugViewMode != 0;
    context.unknownReflectionOwner =
        contract.sceneVisual.reflectionOwner.empty() ||
        contract.sceneVisual.reflectionOwner == "unknown";
    context.rtMissEnvironmentPolicyReady =
        !contract.sceneVisual.invalidExternalHDRI &&
        (!contract.sceneVisual.enclosedScene ||
         contract.environment.localReflectionProbeCount > 0 ||
         contract.environment.backgroundExposure <= 0.001f ||
         !contract.features.iblEnabled);
    context.sceneLocalEnvironmentShaderReady =
        contract.sceneVisual.active &&
        !contract.sceneVisual.invalidExternalHDRI &&
        !contract.sceneVisual.environmentOwner.empty() &&
        contract.sceneVisual.environmentOwner != "unknown";
    context.postNamedStagesReady =
        contract.cinematicPost.enabled &&
        contract.cinematicPost.postProcessPlanned &&
        contract.cinematicPost.postProcessExecuted;
    context.explicitPassGraphReady =
        contract.renderGraph.active &&
        contract.renderGraph.passRecords > 0 &&
        contract.renderGraph.transientValidationRan;

    context.material = MakeFullSceneShaderDomainEvidence(
        "material",
        context.materialModelEvidence.enabled,
        context.materialModelEvidence.fullSceneMaterialModelReady,
        context.materialModelEvidence.owner,
        context.materialModelEvidence.failureReason);
    context.gbuffer = MakeFullSceneShaderDomainEvidence(
        "gbuffer",
        contract.features.visibilityBufferEnabled,
        context.extendedMaterialChannelsReady &&
            context.materialPolicyChannelReady &&
            context.velocityReady,
        "VisibilityBufferRenderer",
        "V2 GBuffer policy channels are reported but object-id/debug ownership is not promoted");
    context.lighting = MakeFullSceneShaderDomainEvidence(
        "lighting",
        contract.lighting.lightCount > 0 || contract.features.iblEnabled,
        context.sceneLocalEnvironmentShaderReady &&
            !contract.lighting.rigId.empty() &&
            contract.lighting.rigId != "custom",
        "SceneVisualContract/LightingState",
        "Semantic light-rig output remains V1-owned until packet gates pass");
    context.reflections = MakeFullSceneShaderDomainEvidence(
        "reflections",
        contract.features.ssrEnabled ||
            contract.features.rtReflectionsEnabled ||
            contract.environment.localReflectionProbeCount > 0,
        context.rtMissEnvironmentPolicyReady &&
            context.reflectionOwnerReportAvailable &&
            !context.unknownReflectionOwner,
        "SceneVisualContract/EnvironmentState",
        "Reflection-source resolver is instrumented but not promoted");
    context.shadows = MakeFullSceneShaderDomainEvidence(
        "shadows",
        contract.features.shadowsEnabled,
        contract.features.shadowsEnabled &&
            !contract.lighting.shadowPolicyId.empty() &&
            contract.lighting.shadowPolicyId != "default" &&
            FullSceneShaderHasResource(contract, "shadow_map"),
        "ShadowResources/SceneVisualContract",
        "Contact-shadow and stability gates are not promoted");
    context.temporal = MakeFullSceneShaderDomainEvidence(
        "temporal",
        contract.features.taaEnabled,
        context.jitterReprojectionReady &&
            context.temporalPoliciesAvailable &&
            FullSceneShaderHasResource(contract, "taa_history"),
        "TemporalMask/FrameDiagnostics",
        "Material-aware temporal clamp is not promoted across packet gates");
    context.post = MakeFullSceneShaderDomainEvidence(
        "post",
        contract.cinematicPost.enabled,
        context.postNamedStagesReady &&
            contract.cinematicPost.exposurePolicyActive &&
            !contract.cinematicPost.toneMapperPreset.empty(),
        "CinematicPostInfo",
        "HDR Post V2 named stages are reported but not promoted");
    context.renderGraph = MakeFullSceneShaderDomainEvidence(
        "render_graph",
        contract.renderGraph.active,
        context.explicitPassGraphReady,
        "FrameDiagnostics/RenderGraphInfo",
        "Render graph ownership is not yet the V2 promotion authority");
    context.assetEvidence = MakeFullSceneShaderDomainEvidence(
        "asset_evidence",
        false,
        false,
        "external_material_evidence_report",
        "Asset Registry V2 evidence is external and pending runtime admission",
        FullSceneShaderPromotionState::Planned);
    context.packetGate = MakeFullSceneShaderDomainEvidence(
        "packet_gate",
        false,
        false,
        "external_cross_family_packet_gate",
        "Cross-family V2 packet gate has not been run",
        FullSceneShaderPromotionState::Planned);

    return context;
}

} // namespace Cortex::Graphics
