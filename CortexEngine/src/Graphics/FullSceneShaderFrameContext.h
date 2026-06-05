#pragma once

#include "Graphics/FrameContract.h"
#include "Graphics/MaterialModel.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
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

    struct FullSceneGBufferEvidence {
        bool enabled = false;
        bool ready = false;
        bool channelInventoryAvailable = false;
        bool albedoChannelReady = false;
        bool normalRoughnessChannelReady = false;
        bool emissiveMetallicChannelReady = false;
        bool extendedMaterialChannelsReady = false;
        bool semanticMaterialPolicyChannelReady = false;
        bool visibilityPayloadChannelReady = false;
        bool visibilityPayloadProducerReady = false;
        bool instanceIdentityTableReady = false;
        bool instanceMaterialLookupReady = false;
        bool stableInstanceIdAvailable = false;
        bool materialIdChannelReady = false;
        bool objectIdChannelReady = false;
        bool velocityChannelReady = false;
        bool producerOwnershipAvailable = false;
        bool debugViewSourceReportAvailable = false;
        uint32_t visibilityBufferInstanceCount = 0;
        uint32_t visibilityBufferMaterialCount = 0;
        uint32_t invalidStableInstanceIdCount = 0;
        uint32_t missingRequiredChannelCount = 0;
        uint32_t missingOwnershipChannelCount = 0;
        std::string owner = "VisibilityBufferRenderer/FullSceneGBufferEvidence";
        std::string failureReason = "Visibility buffer is not enabled";
    };

    FullSceneGBufferEvidence gbufferEvidence;

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

inline bool FullSceneShaderPassWritesResource(
    const FrameContract& contract,
    const char* passName,
    const char* resourceName) {
    return std::any_of(
        contract.passes.begin(),
        contract.passes.end(),
        [passName, resourceName](const FrameContract::PassRecord& pass) {
            return pass.name == passName &&
                   pass.planned &&
                   pass.executed &&
                   std::any_of(
                       pass.writes.begin(),
                       pass.writes.end(),
                       [resourceName](const std::string& write) {
                           return write == resourceName;
                       });
        });
}

inline bool FullSceneShaderExecutedProducerWrites(
    const FrameContract& contract,
    const char* passName,
    std::initializer_list<const char*> resourceNames) {
    return std::all_of(
        resourceNames.begin(),
        resourceNames.end(),
        [&contract, passName](const char* resourceName) {
            return FullSceneShaderPassWritesResource(contract, passName, resourceName);
        });
}

inline FullSceneShaderFrameContext::FullSceneGBufferEvidence BuildFullSceneGBufferEvidence(
    const FrameContract& contract,
    bool velocityReady,
    bool materialPolicyChannelReady,
    bool extendedMaterialChannelsReady) {
    FullSceneShaderFrameContext::FullSceneGBufferEvidence evidence;
    evidence.enabled = contract.features.visibilityBufferEnabled;
    evidence.albedoChannelReady = FullSceneShaderHasResource(contract, "vb_gbuffer_albedo");
    evidence.normalRoughnessChannelReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_normal_roughness");
    evidence.emissiveMetallicChannelReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_emissive_metallic");
    evidence.extendedMaterialChannelsReady = extendedMaterialChannelsReady;
    evidence.semanticMaterialPolicyChannelReady = materialPolicyChannelReady;
    evidence.visibilityPayloadChannelReady = FullSceneShaderHasResource(contract, "visibility_buffer");
    evidence.visibilityPayloadProducerReady =
        FullSceneShaderPassWritesResource(contract, "VBVisibility", "visibility_buffer") ||
        FullSceneShaderPassWritesResource(contract, "VisibilityBuffer", "visibility_buffer");
    evidence.visibilityBufferInstanceCount = contract.draws.visibilityBufferInstances;
    evidence.visibilityBufferMaterialCount = contract.draws.visibilityBufferMaterials;
    evidence.invalidStableInstanceIdCount = contract.draws.visibilityBufferInvalidStableIds;
    evidence.instanceIdentityTableReady = evidence.visibilityBufferInstanceCount > 0;
    evidence.instanceMaterialLookupReady =
        evidence.visibilityBufferMaterialCount > 0 &&
        evidence.visibilityBufferMaterialCount <= evidence.visibilityBufferInstanceCount;
    evidence.stableInstanceIdAvailable =
        evidence.instanceIdentityTableReady &&
        evidence.invalidStableInstanceIdCount == 0;
    evidence.velocityChannelReady = velocityReady;
    evidence.producerOwnershipAvailable =
        evidence.visibilityPayloadProducerReady &&
        FullSceneShaderExecutedProducerWrites(
            contract,
            "VisibilityBuffer",
            {"vb_gbuffer_albedo",
             "vb_gbuffer_normal_roughness",
             "vb_gbuffer_emissive_metallic",
             "vb_gbuffer_material_ext0",
             "vb_gbuffer_material_ext1",
             "vb_gbuffer_material_ext2"}) &&
        FullSceneShaderPassWritesResource(contract, "MotionVectors", "velocity");
    evidence.channelInventoryAvailable =
        evidence.albedoChannelReady &&
        evidence.normalRoughnessChannelReady &&
        evidence.emissiveMetallicChannelReady &&
        evidence.extendedMaterialChannelsReady;

    const bool requiredChannels[] = {
        evidence.albedoChannelReady,
        evidence.normalRoughnessChannelReady,
        evidence.emissiveMetallicChannelReady,
        evidence.extendedMaterialChannelsReady,
        evidence.semanticMaterialPolicyChannelReady,
        evidence.visibilityPayloadChannelReady,
        evidence.visibilityPayloadProducerReady,
        evidence.instanceIdentityTableReady,
        evidence.instanceMaterialLookupReady,
        evidence.stableInstanceIdAvailable,
        evidence.velocityChannelReady,
        evidence.producerOwnershipAvailable,
    };
    for (bool ready : requiredChannels) {
        if (!ready) {
            ++evidence.missingRequiredChannelCount;
        }
    }

    evidence.materialIdChannelReady =
        evidence.visibilityPayloadChannelReady &&
        evidence.instanceMaterialLookupReady;
    evidence.objectIdChannelReady =
        evidence.visibilityPayloadChannelReady &&
        evidence.stableInstanceIdAvailable;
    evidence.debugViewSourceReportAvailable =
        evidence.visibilityPayloadProducerReady &&
        evidence.materialIdChannelReady &&
        evidence.objectIdChannelReady;

    const bool ownershipChannels[] = {
        evidence.materialIdChannelReady,
        evidence.objectIdChannelReady,
        evidence.debugViewSourceReportAvailable,
    };
    for (bool ready : ownershipChannels) {
        if (!ready) {
            ++evidence.missingOwnershipChannelCount;
        }
    }

    evidence.ready =
        evidence.enabled &&
        evidence.missingRequiredChannelCount == 0 &&
        evidence.missingOwnershipChannelCount == 0;

    if (!evidence.enabled) {
        evidence.failureReason = "Visibility buffer is not enabled";
    } else if (evidence.missingRequiredChannelCount > 0) {
        evidence.failureReason = "Required GBuffer identity resources or producers are missing";
    } else if (!evidence.materialIdChannelReady) {
        evidence.failureReason = "Stable per-pixel material-id channel is not promoted";
    } else if (!evidence.objectIdChannelReady) {
        evidence.failureReason = "Stable per-pixel object-id channel is not promoted";
    } else if (!evidence.debugViewSourceReportAvailable) {
        evidence.failureReason = "Debug-view producer ownership is not reported";
    } else {
        evidence.failureReason = "FullSceneFrameData GBuffer ownership is ready";
    }

    return evidence;
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
    context.gbufferEvidence = BuildFullSceneGBufferEvidence(
        contract,
        context.velocityReady,
        context.materialPolicyChannelReady,
        context.extendedMaterialChannelsReady);
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
        context.gbufferEvidence.enabled,
        context.gbufferEvidence.ready,
        context.gbufferEvidence.owner,
        context.gbufferEvidence.failureReason);
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
