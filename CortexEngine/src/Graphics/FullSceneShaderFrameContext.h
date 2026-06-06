#pragma once

#include "Graphics/FrameContract.h"
#include "Graphics/MaterialModel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

    struct FullSceneLightingRigEvidence {
        bool enabled = false;
        bool ready = false;
        bool semanticLightRigReady = false;
        bool sceneLocalEnvironmentShaderReady = false;
        bool lightOwnerReportAvailable = false;
        bool semanticLightRolesAvailable = false;
        bool rigPolicyIdsConsistent = false;
        bool lightingBalancePolicyReady = false;
        bool localFixtureContractReady = false;
        bool shadowedLightContractReady = false;
        bool shaderLightArrayReady = false;
        bool semanticLightPayloadReady = false;
        bool semanticLightShaderPayloadReady = false;
        bool areaLightPayloadReady = false;
        bool clusteredLightListReady = false;
        bool directLightPassReady = false;
        bool directLightShadowOutputReady = false;
        bool directLightDebugViewReady = false;
        bool directLightUnshadowedDebugViewReady = false;
        bool directLightShadowLossDebugViewReady = false;
        bool lightingV2ShadowOutputReady = false;
        bool exposurePolicyReady = false;
        bool exposureClippingGatePassed = false;
        uint32_t lightCount = 0;
        uint32_t pointLightCount = 0;
        uint32_t spotLightCount = 0;
        uint32_t rectAreaLightCount = 0;
        uint32_t twoSidedAreaLightCount = 0;
        uint32_t semanticFixtureLightCount = 0;
        uint32_t semanticLightPayloadCount = 0;
        uint32_t softFixtureLightCount = 0;
        uint32_t emissiveFixtureLightCount = 0;
        uint32_t stageFixtureLightCount = 0;
        uint32_t practicalFixtureLightCount = 0;
        uint32_t shadowCastingLightCount = 0;
        float totalLightIntensity = 0.0f;
        float maxLightIntensity = 0.0f;
        uint32_t missingLightingContractCount = 0;
        std::string semanticLightPayloadOwner = "none";
        std::string semanticLightPayloadChannels = "none";
        std::string lightingV2PassOwner = "none";
        std::string lightingV2OutputResource = "none";
        std::string owner = "SceneVisualContract/FullSceneLightingRigEvidence";
        std::string failureReason = "Semantic light-rig evidence is not populated";
    };

    FullSceneLightingRigEvidence lightingEvidence;

    struct FullSceneReflectionOwnershipEvidence {
        bool enabled = false;
        bool ready = false;
        bool reflectionOwnerReportAvailable = false;
        bool reflectionOwnerKnown = false;
        bool reflectionPoliciesAvailable = false;
        bool externalIblVisibilityAuthorized = false;
        bool localProbeRigDeclared = false;
        bool localProbeTableReady = false;
        bool localProbeRadianceReady = false;
        bool localProbeIntensityReady = false;
        bool localProbeContractReady = false;
        bool rtMissEnvironmentPolicyReady = false;
        bool enclosedMissFallbackSafe = false;
        bool reflectionSourceContractReady = false;
        uint32_t roomProbeCount = 0;
        uint32_t heroProbeCount = 0;
        uint32_t planarProbeCount = 0;
        uint32_t skippedProbeCount = 0;
        float localProbeDiffuseIntensity = 0.0f;
        float localProbeSpecularIntensity = 0.0f;
        float unauthorizedExternalHDRIRatio = 1.0f;
        float unknownReflectionOwnerRatio = 1.0f;
        uint32_t missingReflectionContractCount = 0;
        std::string owner = "SceneVisualContract/FullSceneReflectionOwnershipEvidence";
        std::string failureReason = "Reflection ownership evidence is not populated";
    };

    FullSceneReflectionOwnershipEvidence reflectionEvidence;

    struct FullSceneShadowContactEvidence {
        bool enabled = false;
        bool ready = false;
        bool shadowPolicyReportAvailable = false;
        bool shadowMapReady = false;
        bool shadowMapProducerReady = false;
        bool cascadeDebugAvailable = false;
        bool cascadePolicyReady = false;
        bool shadowBiasPolicyReady = false;
        bool shadowFilterPolicyReady = false;
        bool shadowCasterOwnershipReady = false;
        bool localShadowAtlasReady = false;
        bool rtShadowMaskReady = false;
        bool rtShadowHistoryReady = false;
        bool contactShadowReady = false;
        bool shadowStabilityGatePassed = false;
        uint32_t shadowCastingLightCount = 0;
        float shadowBias = 0.0f;
        float shadowPCFRadius = 0.0f;
        float cascadeSplitLambda = 0.0f;
        uint32_t missingShadowContractCount = 0;
        std::string owner = "ShadowResources/FullSceneShadowContactEvidence";
        std::string failureReason = "Shadow/contact stability evidence is not populated";
    };

    FullSceneShadowContactEvidence shadowEvidence;

    struct FullSceneTemporalEvidence {
        bool enabled = false;
        bool ready = false;
        bool motionVectorsReady = false;
        bool visibilityBufferMotionReady = false;
        bool previousTransformHistoryReady = false;
        bool temporalMaskReady = false;
        bool temporalMaskStatsReady = false;
        bool temporalMaskLatencyReady = false;
        bool jitterReprojectionReady = false;
        bool materialAwareRejectionReady = false;
        bool historyClampReady = false;
        bool taaHistoryReady = false;
        bool taaHistoryVelocityReprojectionReady = false;
        bool taaHistoryDisocclusionRejectionReady = false;
        bool smoothSurfaceMotionGatePassed = false;
        bool cameraSweepGatePassed = false;
        float temporalMaskAcceptedRatio = 0.0f;
        float temporalMaskDisocclusionRatio = 0.0f;
        float temporalMaskHighMotionRatio = 0.0f;
        float temporalMaskOutOfBoundsRatio = 0.0f;
        uint32_t temporalMaskReadbackLatencyFrames = 0;
        float taaHistoryAccumulationAlpha = 0.0f;
        uint64_t taaHistoryAgeFrames = 0;
        uint32_t missingTemporalContractCount = 0;
        std::string owner = "TemporalMask/FullSceneTemporalEvidence";
        std::string failureReason = "Material-aware temporal evidence is not populated";
    };

    FullSceneTemporalEvidence temporalEvidence;

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

inline bool FullSceneShaderPassReadsResource(
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
                       pass.reads.begin(),
                       pass.reads.end(),
                       [resourceName](const std::string& read) {
                           return read == resourceName;
                       });
        });
}

inline const FrameContract::HistoryInfo* FullSceneShaderFindHistory(
    const FrameContract& contract,
    const char* name) {
    auto it = std::find_if(
        contract.histories.begin(),
        contract.histories.end(),
        [name](const FrameContract::HistoryInfo& history) {
            return history.name == name;
        });
    return it != contract.histories.end() ? &(*it) : nullptr;
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

inline FullSceneShaderFrameContext::FullSceneLightingRigEvidence BuildFullSceneLightingRigEvidence(
    const FrameContract& contract,
    bool sceneLocalEnvironmentShaderReady) {
    FullSceneShaderFrameContext::FullSceneLightingRigEvidence evidence;
    evidence.enabled = contract.lighting.lightCount > 0 || contract.features.iblEnabled;
    evidence.sceneLocalEnvironmentShaderReady = sceneLocalEnvironmentShaderReady;
    evidence.semanticLightRigReady =
        contract.sceneVisual.active &&
        !contract.lighting.rigId.empty() &&
        contract.lighting.rigId != "custom" &&
        !contract.lighting.rigSource.empty() &&
        contract.lighting.rigSource != "manual";
    evidence.lightCount = contract.lighting.lightCount;
    evidence.pointLightCount = contract.lighting.pointLightCount;
    evidence.spotLightCount = contract.lighting.spotLightCount;
    evidence.rectAreaLightCount = contract.lighting.areaRectLightCount;
    evidence.twoSidedAreaLightCount = contract.lighting.twoSidedAreaLightCount;
    evidence.semanticFixtureLightCount = contract.lighting.semanticFixtureLightCount;
    evidence.softFixtureLightCount = contract.lighting.softFixtureLightCount;
    evidence.emissiveFixtureLightCount = contract.lighting.emissiveFixtureLightCount;
    evidence.stageFixtureLightCount = contract.lighting.stageFixtureLightCount;
    evidence.practicalFixtureLightCount = contract.lighting.practicalFixtureLightCount;
    evidence.shadowCastingLightCount = contract.lighting.shadowCastingLightCount;
    evidence.totalLightIntensity = contract.lighting.totalLightIntensity;
    evidence.maxLightIntensity = contract.lighting.maxLightIntensity;

    evidence.lightOwnerReportAvailable =
        evidence.semanticFixtureLightCount > 0 ||
        contract.sceneVisual.profileLightFixtureCount > 0;
    evidence.semanticLightRolesAvailable =
        evidence.semanticFixtureLightCount > 0 ||
        evidence.rectAreaLightCount > 0 ||
        evidence.stageFixtureLightCount > 0 ||
        evidence.practicalFixtureLightCount > 0 ||
        contract.lighting.sunIntensity > 0.0f;
    evidence.rigPolicyIdsConsistent =
        contract.sceneVisual.lightRigId == contract.lighting.rigId &&
        contract.sceneVisual.shadowPolicyId == contract.lighting.shadowPolicyId &&
        contract.sceneVisual.exposurePolicyId == contract.lighting.exposurePolicyId;
    evidence.lightingBalancePolicyReady =
        contract.lighting.lightingBalancePolicyActive &&
        !contract.lighting.lightingBalancePolicyId.empty() &&
        contract.lighting.lightingBalancePolicyId != "none" &&
        contract.lighting.lightingBalanceSunScale > 0.0f &&
        contract.lighting.lightingBalanceAmbientScale > 0.0f &&
        contract.lighting.lightingBalanceLocalFixtureScale > 0.0f &&
        contract.lighting.lightingBalanceLocalProbeDiffuseScale >= 0.0f &&
        contract.lighting.lightingBalanceLocalProbeSpecularScale >= 0.0f &&
        contract.lighting.lightingBalanceExposureScale > 0.0f &&
        contract.lighting.lightingBalanceSSAOScale > 0.0f;
    evidence.localFixtureContractReady =
        evidence.lightCount > 0 &&
        (evidence.semanticFixtureLightCount > 0 ||
         evidence.rectAreaLightCount > 0 ||
         contract.lighting.pointLightCount > 0 ||
         contract.lighting.spotLightCount > 0);
    evidence.shadowedLightContractReady =
        !contract.features.shadowsEnabled ||
        (evidence.shadowCastingLightCount > 0 &&
         !contract.lighting.shadowPolicyId.empty() &&
         contract.lighting.shadowPolicyId != "default");
    evidence.shaderLightArrayReady =
        evidence.lightCount > 0 &&
        evidence.lightCount <= 16u;
    evidence.semanticLightPayloadReady =
        evidence.semanticLightRolesAvailable &&
        evidence.semanticFixtureLightCount > 0;
    evidence.semanticLightPayloadCount = evidence.semanticFixtureLightCount;
    evidence.semanticLightPayloadOwner =
        evidence.semanticLightPayloadReady ? "FrameConstants.lights" : "none";
    evidence.semanticLightPayloadChannels =
        evidence.semanticLightPayloadReady ? "direction_cosInner.w_or_params.z" : "none";
    evidence.semanticLightShaderPayloadReady =
        evidence.semanticLightPayloadReady &&
        evidence.shaderLightArrayReady &&
        evidence.semanticLightPayloadCount <= evidence.lightCount &&
        evidence.semanticLightPayloadOwner == "FrameConstants.lights";
    evidence.areaLightPayloadReady =
        evidence.rectAreaLightCount == 0 ||
        (evidence.rectAreaLightCount <= evidence.lightCount &&
         contract.lighting.areaLightSizeScale > 0.0f);
    evidence.clusteredLightListReady =
        FullSceneShaderPassReadsResource(contract, "VBClusteredLights", "local_lights") &&
        FullSceneShaderPassWritesResource(contract, "VBClusteredLights", "cluster_ranges") &&
        FullSceneShaderPassWritesResource(contract, "VBClusteredLights", "cluster_light_indices");
    evidence.directLightPassReady =
        FullSceneShaderPassReadsResource(contract, "VBDeferredLighting", "gbuffer_albedo") &&
        FullSceneShaderPassReadsResource(contract, "VBDeferredLighting", "gbuffer_normal_roughness") &&
        FullSceneShaderPassReadsResource(contract, "VBDeferredLighting", "gbuffer_material_ext2") &&
        FullSceneShaderPassWritesResource(contract, "VBDeferredLighting", "hdr_color");
    evidence.directLightShadowOutputReady =
        evidence.directLightPassReady &&
        evidence.shadowedLightContractReady &&
        (!contract.features.shadowsEnabled ||
         FullSceneShaderPassReadsResource(contract, "VBDeferredLighting", "shadow_map"));
    evidence.directLightDebugViewReady = evidence.directLightPassReady;
    evidence.directLightUnshadowedDebugViewReady = evidence.directLightPassReady;
    evidence.directLightShadowLossDebugViewReady = evidence.directLightShadowOutputReady;
    evidence.lightingV2PassOwner = evidence.directLightPassReady ? "VBDeferredLighting" : "none";
    evidence.lightingV2OutputResource = evidence.directLightPassReady ? "hdr_color" : "none";
    evidence.lightingV2ShadowOutputReady =
        evidence.directLightShadowOutputReady &&
        evidence.directLightDebugViewReady &&
        evidence.directLightUnshadowedDebugViewReady &&
        evidence.directLightShadowLossDebugViewReady &&
        evidence.lightingV2PassOwner == "VBDeferredLighting" &&
        evidence.lightingV2OutputResource == "hdr_color";
    evidence.exposurePolicyReady =
        !contract.lighting.exposurePolicyId.empty() &&
        contract.lighting.exposurePolicyId != "default" &&
        contract.lighting.exposure > 0.0f;
    evidence.exposureClippingGatePassed =
        evidence.exposurePolicyReady &&
        contract.lighting.exposure >= 0.05f &&
        contract.lighting.exposure <= 10.0f &&
        evidence.maxLightIntensity >= 0.0f &&
        evidence.maxLightIntensity <= 100.0f &&
        evidence.totalLightIntensity >= 0.0f &&
        evidence.totalLightIntensity <= 1000.0f;

    const bool requiredContracts[] = {
        evidence.semanticLightRigReady,
        evidence.sceneLocalEnvironmentShaderReady,
        evidence.lightOwnerReportAvailable,
        evidence.semanticLightRolesAvailable,
        evidence.rigPolicyIdsConsistent,
        evidence.lightingBalancePolicyReady,
        evidence.localFixtureContractReady,
        evidence.shadowedLightContractReady,
        evidence.shaderLightArrayReady,
        evidence.semanticLightPayloadReady,
        evidence.semanticLightShaderPayloadReady,
        evidence.areaLightPayloadReady,
        evidence.clusteredLightListReady,
        evidence.directLightPassReady,
        evidence.directLightShadowOutputReady,
        evidence.directLightDebugViewReady,
        evidence.directLightUnshadowedDebugViewReady,
        evidence.directLightShadowLossDebugViewReady,
        evidence.lightingV2ShadowOutputReady,
        evidence.exposurePolicyReady,
        evidence.exposureClippingGatePassed,
    };
    for (bool ready : requiredContracts) {
        if (!ready) {
            ++evidence.missingLightingContractCount;
        }
    }

    evidence.ready = evidence.enabled && evidence.missingLightingContractCount == 0;

    if (!evidence.enabled) {
        evidence.failureReason = "No scene-local light or IBL contribution is enabled";
    } else if (!evidence.semanticLightRigReady) {
        evidence.failureReason = "Semantic light rig id/source is missing or still manual";
    } else if (!evidence.lightOwnerReportAvailable) {
        evidence.failureReason = "Light owner report has no semantic fixture evidence";
    } else if (!evidence.rigPolicyIdsConsistent) {
        evidence.failureReason = "Lighting rig, shadow policy, or exposure policy do not match the scene visual contract";
    } else if (!evidence.lightingBalancePolicyReady) {
        evidence.failureReason = "Scene-local lighting balance policy is missing or invalid";
    } else if (!evidence.localFixtureContractReady) {
        evidence.failureReason = "Local fixture contract has no usable scene-owned lights";
    } else if (!evidence.shadowedLightContractReady) {
        evidence.failureReason = "Shadowed light contract is missing shadow-casting light ownership";
    } else if (!evidence.shaderLightArrayReady) {
        evidence.failureReason = "Shader-facing light array is empty or over budget";
    } else if (!evidence.semanticLightPayloadReady) {
        evidence.failureReason = "Semantic light payload is not encoded in shader-facing lights";
    } else if (!evidence.semanticLightShaderPayloadReady) {
        evidence.failureReason = "Semantic light payload is not owned by FrameConstants.lights shader lanes";
    } else if (!evidence.areaLightPayloadReady) {
        evidence.failureReason = "Area-light payload is missing or outside valid bounds";
    } else if (!evidence.clusteredLightListReady) {
        evidence.failureReason = "Clustered light-list pass/resource ownership is missing";
    } else if (!evidence.directLightPassReady) {
        evidence.failureReason = "V2 direct-light pass inputs or HDR output are missing";
    } else if (!evidence.directLightShadowOutputReady) {
        evidence.failureReason = "V2 direct-light shadow output is not connected to shadow_map";
    } else if (!evidence.directLightDebugViewReady) {
        evidence.failureReason = "V2 direct-light debug view is not owned by the deferred lighting pass";
    } else if (!evidence.directLightUnshadowedDebugViewReady) {
        evidence.failureReason = "V2 unshadowed direct-light debug view is not owned by the deferred lighting pass";
    } else if (!evidence.directLightShadowLossDebugViewReady) {
        evidence.failureReason = "V2 direct-light shadow-loss debug view is not owned by the deferred lighting pass";
    } else if (!evidence.lightingV2ShadowOutputReady) {
        evidence.failureReason = "FullSceneLightingV2 shadow output is not owned by VBDeferredLighting -> hdr_color";
    } else if (!evidence.exposureClippingGatePassed) {
        evidence.failureReason = "Exposure or light intensity contract is outside V2 bounds";
    } else {
        evidence.failureReason = "Scene-local semantic light-rig ownership is ready";
    }

    return evidence;
}

inline FullSceneShaderFrameContext::FullSceneReflectionOwnershipEvidence
BuildFullSceneReflectionOwnershipEvidence(
    const FrameContract& contract,
    bool reflectionOwnerReportAvailable,
    bool reflectionOwnerKnown,
    bool reflectionPoliciesAvailable,
    bool rtMissEnvironmentPolicyReady) {
    FullSceneShaderFrameContext::FullSceneReflectionOwnershipEvidence evidence;
    evidence.enabled =
        contract.features.ssrEnabled ||
        contract.features.rtReflectionsEnabled ||
        contract.features.iblEnabled ||
        contract.environment.localReflectionProbeCount > 0;
    evidence.reflectionOwnerReportAvailable = reflectionOwnerReportAvailable;
    evidence.reflectionOwnerKnown = reflectionOwnerKnown;
    evidence.reflectionPoliciesAvailable = reflectionPoliciesAvailable;
    evidence.roomProbeCount = contract.environment.localReflectionProbeCount;
    evidence.skippedProbeCount = contract.environment.localReflectionProbeSkipped;
    evidence.localProbeDiffuseIntensity = contract.environment.localReflectionProbeDiffuseIntensity;
    evidence.localProbeSpecularIntensity = contract.environment.localReflectionProbeSpecularIntensity;
    evidence.externalIblVisibilityAuthorized =
        !contract.sceneVisual.invalidExternalHDRI &&
        (!contract.sceneVisual.externalHDRIVisible ||
         contract.sceneVisual.visibleExternalHDRIAllowed);
    evidence.localProbeRigDeclared =
        !contract.sceneVisual.localReflectionProbeRigId.empty() &&
        contract.sceneVisual.localReflectionProbeRigId != "none";
    evidence.localProbeTableReady =
        evidence.roomProbeCount > 0 &&
        contract.environment.localReflectionProbeTableValid &&
        evidence.skippedProbeCount == 0;
    evidence.localProbeRadianceReady =
        contract.environment.localReflectionProbeRadianceEnabled;
    evidence.localProbeIntensityReady =
        evidence.localProbeDiffuseIntensity > 0.0f ||
        evidence.localProbeSpecularIntensity > 0.0f;
    evidence.localProbeContractReady =
        !evidence.localProbeRigDeclared ||
        (evidence.localProbeTableReady &&
         evidence.localProbeRadianceReady &&
         evidence.localProbeIntensityReady);
    evidence.rtMissEnvironmentPolicyReady = rtMissEnvironmentPolicyReady;
    evidence.enclosedMissFallbackSafe =
        !contract.sceneVisual.enclosedScene ||
        contract.environment.localReflectionProbeCount > 0 ||
        contract.environment.backgroundExposure <= 0.001f ||
        !contract.features.iblEnabled;
    evidence.reflectionSourceContractReady =
        evidence.localProbeContractReady ||
        contract.features.ssrEnabled ||
        contract.features.rtReflectionsEnabled ||
        (contract.features.iblEnabled && evidence.externalIblVisibilityAuthorized);
    evidence.unauthorizedExternalHDRIRatio = evidence.externalIblVisibilityAuthorized ? 0.0f : 1.0f;
    evidence.unknownReflectionOwnerRatio = evidence.reflectionOwnerKnown ? 0.0f : 1.0f;

    const bool requiredContracts[] = {
        evidence.reflectionOwnerReportAvailable,
        evidence.reflectionOwnerKnown,
        evidence.reflectionPoliciesAvailable,
        evidence.externalIblVisibilityAuthorized,
        evidence.localProbeContractReady,
        evidence.rtMissEnvironmentPolicyReady,
        evidence.enclosedMissFallbackSafe,
        evidence.reflectionSourceContractReady,
    };
    for (bool ready : requiredContracts) {
        if (!ready) {
            ++evidence.missingReflectionContractCount;
        }
    }

    evidence.ready = evidence.enabled && evidence.missingReflectionContractCount == 0;

    if (!evidence.enabled) {
        evidence.failureReason = "No reflection or environment source is enabled";
    } else if (!evidence.reflectionOwnerReportAvailable) {
        evidence.failureReason = "Reflection-owner debug report is missing";
    } else if (!evidence.reflectionOwnerKnown) {
        evidence.failureReason = "Scene visual contract has unknown reflection owner";
    } else if (!evidence.reflectionPoliciesAvailable) {
        evidence.failureReason = "Material reflection policies are not complete";
    } else if (!evidence.externalIblVisibilityAuthorized) {
        evidence.failureReason = "External HDRI visibility is unauthorized for the scene";
    } else if (!evidence.localProbeContractReady) {
        evidence.failureReason = "Declared local reflection probe rig is missing table/radiance/intensity evidence";
    } else if (!evidence.rtMissEnvironmentPolicyReady) {
        evidence.failureReason = "RT reflection miss policy can still sample an unsafe environment fallback";
    } else if (!evidence.enclosedMissFallbackSafe) {
        evidence.failureReason = "Enclosed scene has no safe local or neutral reflection miss fallback";
    } else if (!evidence.reflectionSourceContractReady) {
        evidence.failureReason = "No authorized reflection source contract is available";
    } else {
        evidence.failureReason = "Scene-local reflection/probe ownership is ready";
    }

    return evidence;
}

inline FullSceneShaderFrameContext::FullSceneShadowContactEvidence BuildFullSceneShadowContactEvidence(
    const FrameContract& contract) {
    FullSceneShaderFrameContext::FullSceneShadowContactEvidence evidence;
    evidence.enabled = contract.features.shadowsEnabled;
    evidence.shadowCastingLightCount = contract.lighting.shadowCastingLightCount;
    evidence.shadowBias = contract.lighting.shadowBias;
    evidence.shadowPCFRadius = contract.lighting.shadowPCFRadius;
    evidence.cascadeSplitLambda = contract.lighting.cascadeSplitLambda;
    evidence.shadowPolicyReportAvailable =
        !contract.lighting.shadowPolicyId.empty() &&
        contract.lighting.shadowPolicyId != "default";
    evidence.shadowMapReady = FullSceneShaderHasResource(contract, "shadow_map");
    evidence.shadowMapProducerReady =
        FullSceneShaderPassWritesResource(contract, "ShadowPass", "shadow_map");
    evidence.cascadeDebugAvailable = evidence.shadowMapReady;
    evidence.cascadePolicyReady =
        evidence.cascadeSplitLambda >= 0.0f &&
        evidence.cascadeSplitLambda <= 1.0f;
    evidence.shadowBiasPolicyReady =
        evidence.shadowBias >= 0.0f &&
        evidence.shadowBias <= 0.02f;
    evidence.shadowFilterPolicyReady =
        evidence.shadowPCFRadius >= 0.0f &&
        evidence.shadowPCFRadius <= 8.0f;
    evidence.shadowCasterOwnershipReady = evidence.shadowCastingLightCount > 0;
    evidence.localShadowAtlasReady =
        evidence.shadowMapReady &&
        evidence.shadowMapProducerReady &&
        evidence.shadowCasterOwnershipReady;
    evidence.rtShadowMaskReady =
        FullSceneShaderHasResource(contract, "rt_shadow_mask") &&
        FullSceneShaderPassWritesResource(contract, "RTShadowsGI", "rt_shadow_mask");
    evidence.rtShadowHistoryReady = FullSceneShaderHasResource(contract, "rt_shadow_history");
    evidence.contactShadowReady =
        evidence.rtShadowMaskReady ||
        (evidence.localShadowAtlasReady && evidence.shadowPCFRadius > 0.0f);

    const bool requiredContracts[] = {
        evidence.shadowPolicyReportAvailable,
        evidence.shadowMapReady,
        evidence.shadowMapProducerReady,
        evidence.cascadeDebugAvailable,
        evidence.cascadePolicyReady,
        evidence.shadowBiasPolicyReady,
        evidence.shadowFilterPolicyReady,
        evidence.shadowCasterOwnershipReady,
        evidence.localShadowAtlasReady,
        evidence.contactShadowReady,
    };
    for (bool ready : requiredContracts) {
        if (!ready) {
            ++evidence.missingShadowContractCount;
        }
    }

    evidence.shadowStabilityGatePassed =
        evidence.enabled &&
        evidence.missingShadowContractCount == 0 &&
        (!contract.features.rtGIEnabled || evidence.rtShadowMaskReady);
    evidence.ready = evidence.enabled && evidence.shadowStabilityGatePassed;

    if (!evidence.enabled) {
        evidence.failureReason = "Shadows are disabled";
    } else if (!evidence.shadowPolicyReportAvailable) {
        evidence.failureReason = "Scene-local shadow policy report is missing";
    } else if (!evidence.shadowMapReady || !evidence.shadowMapProducerReady) {
        evidence.failureReason = "Shadow map resource or producer is missing";
    } else if (!evidence.shadowCasterOwnershipReady) {
        evidence.failureReason = "No shadow-casting light ownership is reported";
    } else if (!evidence.cascadePolicyReady) {
        evidence.failureReason = "Cascade split policy is outside valid bounds";
    } else if (!evidence.shadowBiasPolicyReady) {
        evidence.failureReason = "Shadow bias policy is outside valid bounds";
    } else if (!evidence.shadowFilterPolicyReady) {
        evidence.failureReason = "Shadow filter policy is outside valid bounds";
    } else if (!evidence.contactShadowReady) {
        evidence.failureReason = "Contact/near-field shadow signal is missing";
    } else if (!evidence.shadowStabilityGatePassed) {
        evidence.failureReason = "Shadow stability gate has unresolved required contracts";
    } else {
        evidence.failureReason = "Scene-local shadow/contact stability is ready";
    }

    return evidence;
}

inline FullSceneShaderFrameContext::FullSceneTemporalEvidence BuildFullSceneTemporalEvidence(
    const FrameContract& contract,
    bool velocityReady,
    bool jitterReprojectionReady,
    bool temporalPoliciesAvailable) {
    FullSceneShaderFrameContext::FullSceneTemporalEvidence evidence;
    evidence.enabled = contract.features.taaEnabled;
    evidence.motionVectorsReady = velocityReady;
    evidence.visibilityBufferMotionReady =
        contract.motionVectors.visibilityBufferMotion &&
        !contract.motionVectors.cameraOnlyFallback;
    evidence.previousTransformHistoryReady =
        !contract.motionVectors.previousTransformHistoryReset &&
        contract.motionVectors.instanceCount > 0 &&
        contract.motionVectors.previousWorldMatrices >= contract.motionVectors.instanceCount;
    evidence.temporalMaskReady =
        FullSceneShaderHasResource(contract, "temporal_rejection_mask") &&
        contract.temporalMask.built &&
        contract.temporalMask.valid;
    evidence.temporalMaskAcceptedRatio = contract.temporalMask.acceptedRatio;
    evidence.temporalMaskDisocclusionRatio = contract.temporalMask.disocclusionRatio;
    evidence.temporalMaskHighMotionRatio = contract.temporalMask.highMotionRatio;
    evidence.temporalMaskOutOfBoundsRatio = contract.temporalMask.outOfBoundsRatio;
    evidence.temporalMaskReadbackLatencyFrames = contract.temporalMask.readbackLatencyFrames;
    evidence.temporalMaskStatsReady =
        evidence.temporalMaskReady &&
        contract.temporalMask.pixelCount > 0 &&
        contract.temporalMask.acceptedRatio >= 0.0f &&
        contract.temporalMask.acceptedRatio <= 1.0f &&
        contract.temporalMask.disocclusionRatio >= 0.0f &&
        contract.temporalMask.disocclusionRatio <= 1.0f &&
        contract.temporalMask.highMotionRatio >= 0.0f &&
        contract.temporalMask.highMotionRatio <= 1.0f &&
        contract.temporalMask.outOfBoundsRatio >= 0.0f &&
        contract.temporalMask.outOfBoundsRatio <= 1.0f;
    evidence.temporalMaskLatencyReady =
        evidence.temporalMaskReady &&
        contract.temporalMask.readbackLatencyFrames <= 8u;
    evidence.jitterReprojectionReady = jitterReprojectionReady;
    evidence.materialAwareRejectionReady =
        temporalPoliciesAvailable &&
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext2");
    const FrameContract::HistoryInfo* taaHistory = FullSceneShaderFindHistory(contract, "taa_color");
    evidence.historyClampReady = FullSceneShaderHasResource(contract, "taa_history");
    evidence.taaHistoryReady =
        taaHistory != nullptr &&
        taaHistory->valid &&
        taaHistory->resourceValid &&
        taaHistory->width == contract.renderWidth &&
        taaHistory->height == contract.renderHeight;
    if (taaHistory != nullptr) {
        evidence.taaHistoryAccumulationAlpha = taaHistory->accumulationAlpha;
        evidence.taaHistoryAgeFrames = taaHistory->ageFrames;
        evidence.taaHistoryVelocityReprojectionReady = taaHistory->usesVelocityReprojection;
        evidence.taaHistoryDisocclusionRejectionReady = taaHistory->usesDisocclusionRejection;
    }
    evidence.smoothSurfaceMotionGatePassed =
        evidence.materialAwareRejectionReady &&
        evidence.taaHistoryReady &&
        evidence.taaHistoryVelocityReprojectionReady &&
        evidence.taaHistoryDisocclusionRejectionReady &&
        evidence.taaHistoryAccumulationAlpha > 0.0f &&
        evidence.taaHistoryAccumulationAlpha <= 0.5f;
    evidence.cameraSweepGatePassed =
        evidence.temporalMaskStatsReady &&
        evidence.temporalMaskLatencyReady &&
        evidence.temporalMaskAcceptedRatio >= 0.50f &&
        evidence.temporalMaskHighMotionRatio <= 0.35f &&
        evidence.temporalMaskOutOfBoundsRatio <= 0.10f;

    const bool requiredContracts[] = {
        evidence.motionVectorsReady,
        evidence.visibilityBufferMotionReady,
        evidence.previousTransformHistoryReady,
        evidence.temporalMaskReady,
        evidence.temporalMaskStatsReady,
        evidence.temporalMaskLatencyReady,
        evidence.jitterReprojectionReady,
        evidence.materialAwareRejectionReady,
        evidence.historyClampReady,
        evidence.taaHistoryReady,
        evidence.taaHistoryVelocityReprojectionReady,
        evidence.taaHistoryDisocclusionRejectionReady,
        evidence.smoothSurfaceMotionGatePassed,
        evidence.cameraSweepGatePassed,
    };
    for (bool ready : requiredContracts) {
        if (!ready) {
            ++evidence.missingTemporalContractCount;
        }
    }

    evidence.ready = evidence.enabled && evidence.missingTemporalContractCount == 0;

    if (!evidence.enabled) {
        evidence.failureReason = "TAA is disabled";
    } else if (!evidence.motionVectorsReady) {
        evidence.failureReason = "Velocity resource or motion-vector pass is missing";
    } else if (!evidence.visibilityBufferMotionReady) {
        evidence.failureReason = "Visibility-buffer motion is missing or camera-only fallback is active";
    } else if (!evidence.previousTransformHistoryReady) {
        evidence.failureReason = "Previous transform history is missing or was reset";
    } else if (!evidence.temporalMaskReady || !evidence.temporalMaskStatsReady) {
        evidence.failureReason = "Temporal rejection mask or statistics are missing";
    } else if (!evidence.temporalMaskLatencyReady) {
        evidence.failureReason = "Temporal rejection mask statistics are too stale";
    } else if (!evidence.jitterReprojectionReady) {
        evidence.failureReason = "Jitter-aware reprojection contract is missing";
    } else if (!evidence.materialAwareRejectionReady) {
        evidence.failureReason = "Material temporal policies are not complete";
    } else if (!evidence.taaHistoryReady) {
        evidence.failureReason = "TAA history resource is missing or invalid";
    } else if (!evidence.taaHistoryVelocityReprojectionReady ||
               !evidence.taaHistoryDisocclusionRejectionReady) {
        evidence.failureReason = "TAA history does not report velocity/disocclusion rejection";
    } else if (!evidence.smoothSurfaceMotionGatePassed) {
        evidence.failureReason = "Smooth-surface temporal history gate has not passed";
    } else if (!evidence.cameraSweepGatePassed) {
        evidence.failureReason = "Camera-sweep temporal rejection gate has not passed";
    } else {
        evidence.failureReason = "Material-aware temporal stability is ready";
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
    context.extendedMaterialChannelsReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext0") &&
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext1") &&
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext2");
    context.familyCountsAvailable =
        FullSceneShaderSceneMaterialFamilyCount(contract.materials) == contract.materials.sampled &&
        contract.materials.sampled > 0;
    context.reflectionPoliciesAvailable =
        FullSceneShaderMaterialReflectionPreferenceCount(contract.materials) == contract.materials.sampled &&
        contract.materials.sampled > 0;
    context.temporalPoliciesAvailable =
        FullSceneShaderMaterialTemporalPolicyCount(contract.materials) == contract.materials.sampled &&
        contract.materials.sampled > 0;
    context.postPoliciesAvailable =
        FullSceneShaderMaterialPostSensitivityCount(contract.materials) == contract.materials.sampled &&
        contract.materials.sampled > 0;
    context.materialPolicyChannelReady =
        FullSceneShaderHasResource(contract, "vb_gbuffer_material_ext2") &&
        context.familyCountsAvailable &&
        context.reflectionPoliciesAvailable &&
        context.temporalPoliciesAvailable &&
        context.postPoliciesAvailable;
    context.materialModelEvidence = BuildFullSceneMaterialModelEvidence(
        contract.materials,
        contract.draws.visibilityBufferMaterials,
        context.materialPolicyChannelReady);
    context.familyCountsAvailable = context.materialModelEvidence.familyCountsAvailable;
    context.reflectionPoliciesAvailable = context.materialModelEvidence.reflectionPoliciesAvailable;
    context.temporalPoliciesAvailable = context.materialModelEvidence.temporalPoliciesAvailable;
    context.postPoliciesAvailable = context.materialModelEvidence.postPoliciesAvailable;
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
    context.lightingEvidence = BuildFullSceneLightingRigEvidence(
        contract,
        context.sceneLocalEnvironmentShaderReady);
    context.reflectionEvidence = BuildFullSceneReflectionOwnershipEvidence(
        contract,
        context.reflectionOwnerReportAvailable,
        !context.unknownReflectionOwner,
        context.reflectionPoliciesAvailable,
        context.rtMissEnvironmentPolicyReady);
    context.shadowEvidence = BuildFullSceneShadowContactEvidence(contract);
    context.temporalEvidence = BuildFullSceneTemporalEvidence(
        contract,
        context.velocityReady,
        context.jitterReprojectionReady,
        context.temporalPoliciesAvailable);
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
        context.lightingEvidence.enabled,
        context.lightingEvidence.ready,
        context.lightingEvidence.owner,
        context.lightingEvidence.failureReason);
    context.reflections = MakeFullSceneShaderDomainEvidence(
        "reflections",
        context.reflectionEvidence.enabled,
        context.reflectionEvidence.ready,
        context.reflectionEvidence.owner,
        context.reflectionEvidence.failureReason);
    context.shadows = MakeFullSceneShaderDomainEvidence(
        "shadows",
        context.shadowEvidence.enabled,
        context.shadowEvidence.ready,
        context.shadowEvidence.owner,
        context.shadowEvidence.failureReason);
    context.temporal = MakeFullSceneShaderDomainEvidence(
        "temporal",
        context.temporalEvidence.enabled,
        context.temporalEvidence.ready,
        context.temporalEvidence.owner,
        context.temporalEvidence.failureReason);
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

struct FullSceneShaderPipelineV3DomainEvidence {
    std::string id;
    bool enabled = false;
    bool ready = false;
    bool defaultBeautyAffects = false;
    std::string producer = "planned";
    std::string outputResource = "none";
    std::string debugView = "none";
    std::string packetGate = "pending";
    std::string promotionState = "planned";
    std::string failureReason = "V3 domain is planned but not implemented";
    std::vector<std::string> backingResources;
    std::vector<std::string> debugViews;
    std::vector<std::string> channels;
    uint32_t backingResourceCount = 0;
    uint32_t requiredChannelCount = 0;
    uint32_t readyChannelCount = 0;
    uint32_t missingRequiredChannelCount = 0;
};

struct FullSceneShaderPipelineV3FrameContext {
    std::string schema = "cortex.full_scene_shader_pipeline_v3.runtime_report.v1";
    std::string status = "planned_not_promoted";
    std::string beautyOutput = "v2_or_legacy_beauty";
    bool defaultBeautyAffects = false;
    bool candidateBeautyRequested = false;
    bool candidateBeautyReady = false;
    bool candidateBeautyDisplayed = false;
    std::string candidateBeautyProducer = "none";
    std::string candidateBeautyOutput = "none";
    bool runtimePlaceholdersReady = true;
    bool contractGrounded = true;
    bool packetGateReady = false;
    bool materialAttributesReady = false;
    bool lightingAdapterReady = false;
    bool lightingSplitResourcesAllocated = false;
    bool lightingSplitResourcesReady = false;
    bool sceneLocalEnvironmentReady = false;
    bool reflectionV3Ready = false;
    bool reflectionRadianceReady = false;
    bool reflectionConfidenceReady = false;
    bool reflectionSourceIdReady = false;
    bool reflectionTemporalDeltaReady = false;
    bool reflectionSSRSourceSignalReady = false;
    bool reflectionRTSourceSignalReady = false;
    bool compositeV3Ready = false;
    bool hdrSceneColorReady = false;
    bool compositeInputsReady = false;
    bool compositeEnergyPolicyReady = false;
    bool compositeOverbrightDiagnosticsReady = false;
    bool cinematicPostV3Ready = false;
    bool ldrCinematicOutputReady = false;
    bool exposureMeterReady = false;
    bool bloomExtractReady = false;
    bool colorGradeReady = false;
    bool toneMapReady = false;
    uint32_t materialAttributesResourceCount = 0;
    uint32_t materialAttributesChannelCount = 0;
    uint32_t lightingAdapterSignalCount = 0;
    uint32_t lightingSplitResourceCount = 0;
    uint32_t sceneLocalEnvironmentChannelCount = 0;
    uint32_t reflectionV3ChannelCount = 0;
    uint32_t reflectionV3SourceCount = 0;
    uint32_t compositeV3ChannelCount = 0;
    uint32_t cinematicPostV3ChannelCount = 0;
    std::string sceneLocalEnvironmentMode = "unknown";
    std::string reflectionV3SourceContract = "unknown";
    std::string compositeV3Producer = "unknown";
    std::string cinematicPostV3Producer = "unknown";
    std::string contractPath = "assets/final_art/full_scene_shader_pipeline_v3_contract.json";
    std::string planPath = "docs/FULL_SCENE_SHADER_PIPELINE_V3.md";
    std::vector<std::string> requiredSceneFamilies = {
        "gallery",
        "kitchen",
        "office",
        "gym",
        "concert",
        "red_room",
        "stadium",
    };
    std::vector<std::string> requiredOutputs = {
        "material_attributes",
        "direct_lighting",
        "indirect_lighting",
        "shadow_visibility",
        "reflection_radiance",
        "reflection_confidence",
        "reflection_source_id",
        "reflection_rejected_source_mask",
        "reflection_temporal_delta",
        "reflection_ssr_source_signal",
        "reflection_rt_source_signal",
        "scene_local_environment",
        "hdr_scene_color",
        "candidate_hdr_scene_color",
        "ldr_cinematic_output",
        "candidate_ldr_cinematic_output",
    };
    std::vector<FullSceneShaderPipelineV3DomainEvidence> domains;
};

inline FullSceneShaderPipelineV3DomainEvidence MakeFullSceneShaderPipelineV3DomainEvidence(
    std::string id,
    std::string producer,
    std::string outputResource,
    std::string debugView,
    std::string failureReason) {
    FullSceneShaderPipelineV3DomainEvidence evidence;
    evidence.id = std::move(id);
    evidence.producer = std::move(producer);
    evidence.outputResource = std::move(outputResource);
    evidence.debugView = std::move(debugView);
    evidence.failureReason = std::move(failureReason);
    return evidence;
}

inline bool FullSceneShaderKnownContractString(const std::string& value) {
    return !value.empty() && value != "unknown" && value != "none" && value != "default";
}

inline std::string FullSceneShaderReflectionV3ForcedSourceContract() {
    const char* value = std::getenv("CORTEX_V3_REFLECTION_SOURCE_OVERRIDE");
    if (!value || value[0] == '\0') {
        return {};
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "auto" || normalized == "0") {
        return {};
    }
    if (normalized == "local" || normalized == "scene_local" || normalized == "1") {
        return "forced_scene_local_radiance";
    }
    if (normalized == "ssr" || normalized == "screen_space" || normalized == "2") {
        return "forced_screen_space_reflection";
    }
    if (normalized == "rt" || normalized == "ray_query" || normalized == "raytraced" ||
        normalized == "ray_traced" || normalized == "3") {
        return "forced_ray_query_reflection";
    }
    if (normalized == "environment" || normalized == "env" || normalized == "4") {
        return "forced_scene_local_environment";
    }
    if (normalized == "none" || normalized == "off" || normalized == "255") {
        return "forced_none";
    }
    return "forced_unknown";
}

inline std::string FullSceneShaderPipelineV3EnvironmentMode(const FrameContract& contract) {
    if (!contract.sceneVisual.active) {
        return "unknown";
    }
    if (contract.sceneVisual.family.find("gallery") != std::string::npos ||
        contract.sceneVisual.profileId.find("gallery") != std::string::npos) {
        return "neutral_lab";
    }
    if (contract.sceneVisual.enclosedScene) {
        const std::string& family = contract.sceneVisual.family;
        const std::string& rig = contract.sceneVisual.lightRigId;
        if (family.find("concert") != std::string::npos ||
            family.find("stage") != std::string::npos ||
            rig.find("stage") != std::string::npos) {
            return "stage";
        }
        return "enclosed_room";
    }
    return "open_exterior";
}

inline FullSceneShaderPipelineV3FrameContext BuildFullSceneShaderPipelineV3FrameContext(
    const FrameContract& contract) {
    FullSceneShaderPipelineV3FrameContext context;
    context.defaultBeautyAffects = false;
    context.beautyOutput = contract.sceneVisual.active ? "full_scene_shader_pipeline_v2" : "legacy_beauty";
    context.candidateBeautyRequested =
        std::getenv("CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3") != nullptr ||
        FullSceneShaderPassWritesResource(
            contract,
            "CinematicPostV3",
            "candidate_ldr_cinematic_output") ||
        FullSceneShaderPassWritesResource(
            contract,
            "FullSceneCandidateBeautyV3",
            "candidate_ldr_cinematic_output");
    context.candidateBeautyDisplayed =
        FullSceneShaderPassWritesResource(
            contract,
            "FullSceneCandidateBeautyV3Display",
            "back_buffer");

    const std::vector<std::string> materialBackingResources = {
        "vb_gbuffer_albedo",
        "vb_gbuffer_normal_roughness",
        "vb_gbuffer_emissive_metallic",
        "vb_gbuffer_material_ext0",
        "vb_gbuffer_material_ext1",
        "vb_gbuffer_material_ext2",
    };
    const std::vector<std::string> materialChannels = {
        "base_color",
        "opacity",
        "normal",
        "roughness",
        "emissive",
        "metallic",
        "clearcoat",
        "ior",
        "specular",
        "transmission",
        "surface_class",
        "anisotropy",
        "sheen",
        "material_class",
        "reflection_policy",
        "temporal_policy",
        "post_sensitivity",
    };
    uint32_t readyMaterialResources = 0;
    for (const std::string& resource : materialBackingResources) {
        if (FullSceneShaderHasResource(contract, resource.c_str())) {
            ++readyMaterialResources;
        }
    }
    const bool materialResolveReady =
        readyMaterialResources == materialBackingResources.size() &&
        contract.features.visibilityBufferEnabled &&
        contract.draws.visibilityBufferMaterials > 0 &&
        contract.materials.sampled > 0;
    context.materialAttributesReady = materialResolveReady;
    context.materialAttributesResourceCount = readyMaterialResources;
    context.materialAttributesChannelCount =
        materialResolveReady ? static_cast<uint32_t>(materialChannels.size()) : 0u;

    FullSceneShaderPipelineV3DomainEvidence materialDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "material",
            "FullSceneMaterialResolveV3",
            "material_attributes",
            "material_missing_channel_mask",
            materialResolveReady
                ? "FullSceneMaterialResolveV3 material_attributes aggregate is backed by visibility-buffer material resolve outputs"
                : "FullSceneMaterialResolveV3 material_attributes aggregate is missing backing material resolve outputs");
    materialDomain.enabled = contract.features.visibilityBufferEnabled;
    materialDomain.ready = materialResolveReady;
    materialDomain.promotionState = materialResolveReady ? "instrumented" : "planned";
    materialDomain.backingResources = materialBackingResources;
    materialDomain.debugViews = {
        "VB_GBuffer_Albedo",
        "VB_GBuffer_NormalRoughness",
        "VB_GBuffer_EmissiveMetallic",
        "VB_GBuffer_MaterialExt0",
        "VB_GBuffer_MaterialExt1",
        "VB_GBuffer_SurfaceClass",
        "VB_MaterialFamilyPolicy",
        "VB_ReflectionPolicy",
        "VB_TemporalPolicy",
        "VB_PostSensitivity",
    };
    materialDomain.channels = materialChannels;
    materialDomain.backingResourceCount = readyMaterialResources;
    materialDomain.requiredChannelCount = static_cast<uint32_t>(materialChannels.size());
    materialDomain.readyChannelCount =
        materialResolveReady ? static_cast<uint32_t>(materialChannels.size()) : 0u;
    materialDomain.missingRequiredChannelCount =
        materialDomain.requiredChannelCount - materialDomain.readyChannelCount;

    const bool lightingAdapterReady =
        FullSceneShaderPassWritesResource(contract, "VBDeferredLighting", "hdr_color") &&
        contract.features.visibilityBufferEnabled &&
        contract.features.shadowsEnabled &&
        contract.lighting.lightCount > 0 &&
        contract.draws.visibilityBufferInstances > 0;
    context.lightingAdapterReady = lightingAdapterReady;
    context.lightingSplitResourcesAllocated =
        FullSceneShaderHasResource(contract, "direct_lighting") &&
        FullSceneShaderHasResource(contract, "direct_lighting_unshadowed") &&
        FullSceneShaderHasResource(contract, "shadow_visibility") &&
        FullSceneShaderHasResource(contract, "shadow_loss") &&
        FullSceneShaderHasResource(contract, "indirect_lighting");
    context.lightingSplitResourcesReady =
        context.lightingSplitResourcesAllocated &&
        FullSceneShaderPassWritesResource(contract, "FullSceneLightingV3", "direct_lighting") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneLightingV3", "direct_lighting_unshadowed") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneLightingV3", "shadow_visibility") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneLightingV3", "shadow_loss") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneLightingV3", "indirect_lighting");
    context.lightingAdapterSignalCount = lightingAdapterReady ? 4u : 0u;
    context.lightingSplitResourceCount = context.lightingSplitResourcesAllocated ? 5u : 0u;

    FullSceneShaderPipelineV3DomainEvidence lightingDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "lighting",
            context.lightingSplitResourcesReady ? "FullSceneLightingV3" : "FullSceneLightingV3Adapter",
            context.lightingSplitResourcesReady ? "lighting_split" : "hdr_color",
            "VB_DeferredDirectLight",
            context.lightingSplitResourcesReady
                ? "FullSceneLightingV3 writes direct/unshadowed/shadow/indirect split lighting resources"
                : lightingAdapterReady
                ? "FullSceneLightingV3 adapter is backed by VBDeferredLighting -> hdr_color; split V3 lighting resources are pending"
                : "FullSceneLightingV3 adapter is missing current deferred lighting ownership");
    lightingDomain.enabled = lightingAdapterReady;
    lightingDomain.ready = context.lightingSplitResourcesReady;
    lightingDomain.promotionState =
        context.lightingSplitResourcesReady ? "producer" : (lightingAdapterReady ? "adapter" : "planned");
    lightingDomain.backingResources = {
        "hdr_color",
        "direct_lighting",
        "direct_lighting_unshadowed",
        "shadow_visibility",
        "shadow_loss",
        "indirect_lighting",
    };
    lightingDomain.debugViews = {
        "VB_DeferredDirectLight",
        "VB_DeferredDirectLightUnshadowed",
        "VB_DeferredDirectLightShadowLoss",
        "VB_DeferredShadowFactor",
        "VB_DeferredAmbientIBL",
    };
    lightingDomain.channels = {
        "direct_lighting_debug",
        "direct_lighting_unshadowed_debug",
        "shadow_loss_debug",
        "shadow_factor_debug",
        "ambient_ibl_debug",
    };
    lightingDomain.backingResourceCount = 1u + context.lightingSplitResourceCount;
    lightingDomain.requiredChannelCount = 5u;
    lightingDomain.readyChannelCount = lightingAdapterReady ? 5u : 0u;
    lightingDomain.missingRequiredChannelCount =
        lightingDomain.requiredChannelCount - lightingDomain.readyChannelCount;

    const std::string environmentMode = FullSceneShaderPipelineV3EnvironmentMode(contract);
    const bool environmentOwnerKnown =
        FullSceneShaderKnownContractString(contract.sceneVisual.environmentOwner);
    const bool reflectionOwnerKnown =
        FullSceneShaderKnownContractString(contract.sceneVisual.reflectionOwner);
    const bool environmentModeReady = environmentMode != "unknown";
    const bool visibleBackgroundReady =
        contract.sceneVisual.active &&
        !contract.sceneVisual.invalidExternalHDRI &&
        (!contract.sceneVisual.externalHDRIVisible ||
         contract.sceneVisual.visibleExternalHDRIAllowed ||
         !contract.sceneVisual.enclosedScene);
    const bool reflectionBackgroundReady =
        contract.sceneVisual.active &&
        reflectionOwnerKnown &&
        !contract.sceneVisual.invalidExternalHDRI;
    const bool ambientLightingReady =
        contract.sceneVisual.active &&
        (contract.sceneVisual.lightingBalancePolicyActive ||
         contract.features.iblEnabled ||
         contract.environment.localReflectionProbeRadianceEnabled ||
         contract.lighting.lightCount > 0);
    const bool atmosphereReady =
        contract.sceneVisual.active &&
        environmentOwnerKnown &&
        FullSceneShaderKnownContractString(contract.sceneVisual.exposurePolicyId);
    uint32_t readyEnvironmentChannels = 0;
    readyEnvironmentChannels += environmentModeReady ? 1u : 0u;
    readyEnvironmentChannels += ambientLightingReady ? 1u : 0u;
    readyEnvironmentChannels += visibleBackgroundReady ? 1u : 0u;
    readyEnvironmentChannels += reflectionBackgroundReady ? 1u : 0u;
    readyEnvironmentChannels += atmosphereReady ? 1u : 0u;
    context.sceneLocalEnvironmentReady =
        environmentOwnerKnown &&
        readyEnvironmentChannels == 5u;
    context.sceneLocalEnvironmentChannelCount = readyEnvironmentChannels;
    context.sceneLocalEnvironmentMode = environmentMode;

    FullSceneShaderPipelineV3DomainEvidence environmentDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "environment",
            "SceneLocalEnvironmentV3",
            "scene_local_environment",
            "environment_mode",
            context.sceneLocalEnvironmentReady
                ? "SceneLocalEnvironmentV3 is backed by scene visual, ambient, visible-background, reflection-background, and atmosphere ownership contracts"
                : "SceneLocalEnvironmentV3 is missing one or more scene-local environment ownership channels");
    environmentDomain.enabled = contract.sceneVisual.active;
    environmentDomain.ready = context.sceneLocalEnvironmentReady;
    environmentDomain.promotionState =
        context.sceneLocalEnvironmentReady ? "instrumented" : "planned";
    environmentDomain.backingResources = {
        "scene_visual_contract",
        "environment_state",
        "scene_lighting_balance_policy",
        "local_reflection_probe_rig",
        "scene_post_exposure_policy",
    };
    environmentDomain.debugViews = {
        "environment_mode",
        "ambient_lighting",
        "visible_background",
        "reflection_background",
        "atmosphere",
    };
    environmentDomain.channels = {
        environmentMode,
        ambientLightingReady ? "ambient_lighting_owned" : "ambient_lighting_missing",
        visibleBackgroundReady ? "visible_background_owned" : "visible_background_missing",
        reflectionBackgroundReady ? "reflection_background_owned" : "reflection_background_missing",
        atmosphereReady ? "atmosphere_owned" : "atmosphere_missing",
    };
    environmentDomain.backingResourceCount = readyEnvironmentChannels;
    environmentDomain.requiredChannelCount = 5u;
    environmentDomain.readyChannelCount = readyEnvironmentChannels;
    environmentDomain.missingRequiredChannelCount =
        environmentDomain.requiredChannelCount - environmentDomain.readyChannelCount;

    const bool reflectionOwnerReportAvailable =
        contract.sceneVisual.pixelReflectionOwnerHistogramAvailable ||
        contract.sceneVisual.reflectionOwnerDebugViewMode != 0;
    const bool reflectionPoliciesAvailable =
        materialResolveReady &&
        contract.materials.reflectionEligible > 0 &&
        (contract.materials.materialReflectionNeutralFallback +
         contract.materials.materialReflectionLocalProbe +
         contract.materials.materialReflectionProbeGrid +
         contract.materials.materialReflectionPlanarProbe +
         contract.materials.materialReflectionSSR +
         contract.materials.materialReflectionRT) > 0;
    const bool externalIblVisibilityAuthorized =
        !contract.sceneVisual.invalidExternalHDRI &&
        (!contract.sceneVisual.externalHDRIVisible ||
         contract.sceneVisual.visibleExternalHDRIAllowed);
    const bool localProbeSourceReady =
        contract.environment.localReflectionProbeCount > 0 &&
        contract.environment.localReflectionProbeSkipped == 0 &&
        contract.environment.localReflectionProbeTableValid &&
        contract.environment.localReflectionProbeRadianceEnabled &&
        (contract.environment.localReflectionProbeDiffuseIntensity > 0.0f ||
         contract.environment.localReflectionProbeSpecularIntensity > 0.0f);
    const bool ssrSourceReady = contract.features.ssrEnabled;
    const bool rtSourceReady =
        contract.features.rtReflectionsEnabled &&
        contract.rayTracing.reflectionDispatchReady &&
        contract.rayTracing.reflectionHasOutput &&
        contract.rayTracing.reflectionHasDepth &&
        contract.rayTracing.reflectionHasNormalRoughness &&
        contract.rayTracing.reflectionHasMaterialExt2 &&
        contract.rayTracing.reflectionHasEnvironmentTable;
    const bool iblSourceReady =
        contract.features.iblEnabled &&
        externalIblVisibilityAuthorized;
    uint32_t readyReflectionSources = 0;
    readyReflectionSources += localProbeSourceReady ? 1u : 0u;
    readyReflectionSources += ssrSourceReady ? 1u : 0u;
    readyReflectionSources += rtSourceReady ? 1u : 0u;
    readyReflectionSources += iblSourceReady ? 1u : 0u;
    const bool enclosedMissFallbackSafe =
        !contract.sceneVisual.enclosedScene ||
        localProbeSourceReady ||
        contract.environment.backgroundExposure <= 0.001f ||
        !contract.features.iblEnabled ||
        contract.sceneVisual.visibleExternalHDRIAllowed;
    const bool sourceContractReady =
        reflectionOwnerKnown &&
        reflectionOwnerReportAvailable &&
        externalIblVisibilityAuthorized &&
        enclosedMissFallbackSafe &&
        readyReflectionSources > 0;
    const bool reflectionResolverV3WritesOutputs =
        FullSceneShaderHasResource(contract, "reflection_radiance") &&
        FullSceneShaderHasResource(contract, "reflection_confidence") &&
        FullSceneShaderHasResource(contract, "reflection_source_id") &&
        FullSceneShaderHasResource(contract, "reflection_rejected_source_mask") &&
        FullSceneShaderHasResource(contract, "reflection_temporal_delta") &&
        FullSceneShaderHasResource(contract, "reflection_ssr_source_signal") &&
        FullSceneShaderHasResource(contract, "reflection_rt_source_signal") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneReflectionV3", "local_reflection_radiance") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneReflectionV3", "ssr_color") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneReflectionV3", "rt_reflection") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_radiance") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_confidence") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_source_id") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_rejected_source_mask") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_temporal_delta") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_ssr_source_signal") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneReflectionV3", "reflection_rt_source_signal");
    context.reflectionRadianceReady =
        context.sceneLocalEnvironmentReady &&
        sourceContractReady &&
        reflectionResolverV3WritesOutputs;
    context.reflectionSourceIdReady =
        reflectionOwnerKnown &&
        reflectionOwnerReportAvailable &&
        reflectionResolverV3WritesOutputs;
    context.reflectionConfidenceReady =
        context.reflectionRadianceReady &&
        reflectionPoliciesAvailable &&
        reflectionResolverV3WritesOutputs;
    const bool primaryReflectionSourceSceneLocal =
        localProbeSourceReady ||
        (!localProbeSourceReady && !rtSourceReady && !ssrSourceReady && iblSourceReady);
    const bool sceneLocalReflectionTemporalBoundReady =
        context.reflectionRadianceReady &&
        primaryReflectionSourceSceneLocal &&
        context.sceneLocalEnvironmentReady &&
        reflectionOwnerKnown;
    const bool historyReflectionTemporalBoundReady =
        (contract.rayTracing.reflectionHistorySignalValid &&
         contract.rayTracing.reflectionHistorySignalReadbackLatencyFrames <= 8u) ||
        (contract.features.taaEnabled &&
         contract.materials.materialTemporalStableGlossy > 0 &&
         FullSceneShaderHasResource(contract, "taa_history"));
    context.reflectionTemporalDeltaReady =
        reflectionResolverV3WritesOutputs &&
        (sceneLocalReflectionTemporalBoundReady ||
         historyReflectionTemporalBoundReady);
    context.reflectionSSRSourceSignalReady =
        reflectionResolverV3WritesOutputs &&
        FullSceneShaderHasResource(contract, "reflection_ssr_source_signal");
    context.reflectionRTSourceSignalReady =
        reflectionResolverV3WritesOutputs &&
        FullSceneShaderHasResource(contract, "reflection_rt_source_signal");
    uint32_t readyReflectionChannels = 0;
    readyReflectionChannels += context.reflectionRadianceReady ? 1u : 0u;
    readyReflectionChannels += context.reflectionConfidenceReady ? 1u : 0u;
    readyReflectionChannels += context.reflectionSourceIdReady ? 1u : 0u;
    readyReflectionChannels += context.reflectionTemporalDeltaReady ? 1u : 0u;
    readyReflectionChannels += reflectionResolverV3WritesOutputs ? 1u : 0u;
    readyReflectionChannels += context.reflectionSSRSourceSignalReady ? 1u : 0u;
    readyReflectionChannels += context.reflectionRTSourceSignalReady ? 1u : 0u;
    context.reflectionV3ChannelCount = readyReflectionChannels;
    context.reflectionV3SourceCount = readyReflectionSources;
    const std::string forcedReflectionSourceContract = FullSceneShaderReflectionV3ForcedSourceContract();
    context.reflectionV3SourceContract =
        !forcedReflectionSourceContract.empty() ? forcedReflectionSourceContract :
        localProbeSourceReady ? "local_probe" :
        rtSourceReady ? "ray_query_reflection" :
        ssrSourceReady ? "screen_space_reflection" :
        iblSourceReady ? "scene_local_environment" :
        "unknown";
    context.reflectionV3Ready = readyReflectionChannels == 7u;

    FullSceneShaderPipelineV3DomainEvidence reflectionDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "reflection",
            "FullSceneReflectionV3",
            "reflection_radiance",
            "reflection_confidence",
            context.reflectionV3Ready
                ? "FullSceneReflectionV3 writes concrete radiance, confidence, source-id, rejected-source, temporal-delta, SSR source-signal, and RT source-signal resources"
                : "FullSceneReflectionV3 is missing one or more reflection ownership channels");
    reflectionDomain.enabled =
        contract.sceneVisual.active &&
        (contract.features.ssrEnabled ||
         contract.features.rtReflectionsEnabled ||
         contract.features.iblEnabled ||
         contract.environment.localReflectionProbeCount > 0);
    reflectionDomain.ready = context.reflectionV3Ready;
    reflectionDomain.promotionState =
        context.reflectionV3Ready ? "instrumented" : "planned";
    reflectionDomain.backingResources = {
        "scene_local_environment",
        "scene_visual_reflection_owner",
        "material_reflection_policy",
        "local_reflection_radiance",
        "rt_reflection",
            "reflection_confidence",
            "reflection_source_id",
            "reflection_rejected_source_mask",
            "reflection_temporal_delta",
            "reflection_ssr_source_signal",
            "reflection_rt_source_signal",
            "rt_reflection_signal_history",
    };
    reflectionDomain.debugViews = {
        "reflection_radiance",
        "reflection_confidence",
        "reflection_source_id",
        "reflection_temporal_delta",
        "reflection_rejected_source_mask",
        "reflection_ssr_source_signal",
        "reflection_rt_source_signal",
    };
    reflectionDomain.channels = {
        context.reflectionRadianceReady ? "reflection_radiance_owned" : "reflection_radiance_missing",
        context.reflectionConfidenceReady ? "reflection_confidence_owned" : "reflection_confidence_missing",
        context.reflectionSourceIdReady ? "reflection_source_id_owned" : "reflection_source_id_missing",
        reflectionResolverV3WritesOutputs ? "reflection_rejected_source_mask_owned" : "reflection_rejected_source_mask_missing",
        context.reflectionTemporalDeltaReady
            ? (sceneLocalReflectionTemporalBoundReady
                   ? "reflection_temporal_delta_scene_local_bound"
                   : "reflection_temporal_delta_history_bound")
            : "reflection_temporal_delta_missing",
        context.reflectionSSRSourceSignalReady
            ? "reflection_ssr_source_signal_owned"
            : "reflection_ssr_source_signal_missing",
        context.reflectionRTSourceSignalReady
            ? "reflection_rt_source_signal_owned"
            : "reflection_rt_source_signal_missing",
        std::string("primary_source=") + context.reflectionV3SourceContract,
    };
    reflectionDomain.backingResourceCount = readyReflectionChannels;
    reflectionDomain.requiredChannelCount = 7u;
    reflectionDomain.readyChannelCount = readyReflectionChannels;
    reflectionDomain.missingRequiredChannelCount =
        reflectionDomain.requiredChannelCount - reflectionDomain.readyChannelCount;

    const bool legacyHdrSceneColorReady =
        FullSceneShaderHasResource(contract, "hdr_color") &&
        FullSceneShaderPassWritesResource(contract, "VBDeferredLighting", "hdr_color");
    const bool candidateHdrSceneColorReady =
        FullSceneShaderHasResource(contract, "candidate_hdr_scene_color") &&
        FullSceneShaderPassWritesResource(contract, "FullSceneCompositeV3", "candidate_hdr_scene_color");
    const bool compositeReadsV3Inputs =
        FullSceneShaderPassReadsResource(contract, "FullSceneCompositeV3", "direct_lighting") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneCompositeV3", "indirect_lighting") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneCompositeV3", "shadow_visibility") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneCompositeV3", "hdr_color") &&
        FullSceneShaderPassReadsResource(contract, "FullSceneCompositeV3", "reflection_radiance");
    const bool realCompositeV3ProducerReady = candidateHdrSceneColorReady && compositeReadsV3Inputs;
    context.hdrSceneColorReady = candidateHdrSceneColorReady || legacyHdrSceneColorReady;
    context.compositeInputsReady =
        materialResolveReady &&
        context.lightingSplitResourcesReady &&
        context.sceneLocalEnvironmentReady &&
        context.reflectionV3Ready;
    context.compositeEnergyPolicyReady =
        contract.cinematicPost.exposurePolicyActive &&
        FullSceneShaderKnownContractString(contract.sceneVisual.exposurePolicyId) &&
        (contract.cinematicPost.hdrShoulderStrength > 0.0f ||
         contract.cinematicPost.postWhiteCompression > 0.0f ||
         contract.cinematicPost.highlightProtection > 0.0f);
    context.compositeOverbrightDiagnosticsReady =
        contract.materials.materialPostExposureProtected > 0 ||
        contract.materials.materialPostBloomEmitter > 0 ||
        contract.cinematicPost.bloomPlanned;
    uint32_t readyCompositeChannels = 0;
    readyCompositeChannels += context.hdrSceneColorReady ? 1u : 0u;
    readyCompositeChannels += context.compositeInputsReady ? 1u : 0u;
    readyCompositeChannels += context.compositeEnergyPolicyReady ? 1u : 0u;
    readyCompositeChannels += context.compositeOverbrightDiagnosticsReady ? 1u : 0u;
    context.compositeV3ChannelCount = readyCompositeChannels;
    context.compositeV3Ready = readyCompositeChannels == 4u;
    context.compositeV3Producer =
        context.compositeV3Ready
            ? (realCompositeV3ProducerReady ? "FullSceneCompositeV3" : "FullSceneCompositeV3Adapter")
            : "planned";

    FullSceneShaderPipelineV3DomainEvidence compositeDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "composite",
            context.compositeV3Producer,
            realCompositeV3ProducerReady ? "candidate_hdr_scene_color" : "hdr_scene_color",
            realCompositeV3ProducerReady ? "candidate_hdr_scene_color" : "hdr_scene_color",
            context.compositeV3Ready
                ? (realCompositeV3ProducerReady
                       ? "FullSceneCompositeV3 writes candidate HDR scene color from V3 lighting and ReflectionResolverV3 radiance resources"
                       : "FullSceneCompositeV3 adapter maps current HDR output to named HDR scene color and owns energy/overbright policy evidence")
                : "FullSceneCompositeV3 is missing HDR output, input, energy, or overbright ownership evidence");
    compositeDomain.enabled = contract.sceneVisual.active;
    compositeDomain.ready = context.compositeV3Ready;
    compositeDomain.promotionState =
        context.compositeV3Ready
            ? (realCompositeV3ProducerReady ? "producer" : "adapter")
            : "planned";
    compositeDomain.backingResources = {
        realCompositeV3ProducerReady ? "candidate_hdr_scene_color" : "hdr_color",
        "hdr_color",
        "reflection_radiance",
        "material_attributes",
        "lighting_split",
        "reflection_radiance",
        "scene_local_environment",
    };
    compositeDomain.debugViews = {
        "candidate_hdr_scene_color",
        "hdr_scene_color",
        "energy_clamp_mask",
        "overbright_mask",
    };
    compositeDomain.channels = {
        realCompositeV3ProducerReady
            ? "candidate_hdr_scene_color_owned_by_full_scene_composite_v3"
            : (context.hdrSceneColorReady ? "hdr_scene_color_owned" : "hdr_scene_color_missing"),
        compositeReadsV3Inputs ? "v3_lighting_and_reflection_inputs_read" : "v3_lighting_and_reflection_inputs_missing",
        context.compositeInputsReady ? "composite_inputs_owned" : "composite_inputs_missing",
        context.compositeEnergyPolicyReady ? "energy_clamp_policy_owned" : "energy_clamp_policy_missing",
        context.compositeOverbrightDiagnosticsReady ? "overbright_diagnostics_owned" : "overbright_diagnostics_missing",
    };
    compositeDomain.backingResourceCount = readyCompositeChannels;
    compositeDomain.requiredChannelCount = 4u;
    compositeDomain.readyChannelCount = readyCompositeChannels;
    compositeDomain.missingRequiredChannelCount =
        compositeDomain.requiredChannelCount - compositeDomain.readyChannelCount;

    const bool cinematicPostV3OutputReady =
        FullSceneShaderHasResource(contract, "candidate_ldr_cinematic_output") &&
        FullSceneShaderPassWritesResource(contract, "CinematicPostV3", "candidate_ldr_cinematic_output") &&
        FullSceneShaderPassReadsResource(contract, "CinematicPostV3", "candidate_hdr_scene_color");
    const bool legacyLdrCinematicOutputReady =
        FullSceneShaderPassWritesResource(contract, "PostProcess", "back_buffer") &&
        FullSceneShaderPassReadsResource(contract, "PostProcess", "hdr_color") &&
        contract.cinematicPost.postProcessPlanned &&
        contract.cinematicPost.postProcessExecuted;
    context.ldrCinematicOutputReady = cinematicPostV3OutputReady || legacyLdrCinematicOutputReady;
    context.exposureMeterReady =
        contract.cinematicPost.exposurePolicyActive &&
        contract.cinematicPost.profileExposureTrim > 0.0f;
    context.bloomExtractReady =
        (!contract.cinematicPost.bloomPlanned && !contract.features.bloomEnabled) ||
        (contract.cinematicPost.bloomPlanned &&
         contract.cinematicPost.bloomExecuted &&
         FullSceneShaderPassReadsResource(contract, "Bloom", "hdr_color"));
    context.colorGradeReady =
        FullSceneShaderKnownContractString(contract.cinematicPost.colorGradePreset) &&
        contract.cinematicPost.lookPolicyActive;
    context.toneMapReady =
        FullSceneShaderKnownContractString(contract.cinematicPost.toneMapperPreset) &&
        contract.cinematicPost.hdrShoulderStart > 0.0f;
    uint32_t readyPostChannels = 0;
    readyPostChannels += context.ldrCinematicOutputReady ? 1u : 0u;
    readyPostChannels += context.exposureMeterReady ? 1u : 0u;
    readyPostChannels += context.bloomExtractReady ? 1u : 0u;
    readyPostChannels += context.colorGradeReady ? 1u : 0u;
    readyPostChannels += context.toneMapReady ? 1u : 0u;
    context.cinematicPostV3ChannelCount = readyPostChannels;
    context.cinematicPostV3Ready =
        context.compositeV3Ready &&
        contract.cinematicPost.enabled &&
        readyPostChannels == 5u;
    context.cinematicPostV3Producer =
        context.cinematicPostV3Ready
            ? (cinematicPostV3OutputReady ? "CinematicPostV3" : "CinematicPostV3Adapter")
            : "planned";

    FullSceneShaderPipelineV3DomainEvidence postDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "cinematic_post",
            context.cinematicPostV3Producer,
            cinematicPostV3OutputReady ? "candidate_ldr_cinematic_output" : "ldr_cinematic_output",
            "exposure_meter",
            context.cinematicPostV3Ready
                ? (cinematicPostV3OutputReady
                       ? "CinematicPostV3 writes candidate LDR output from candidate HDR scene color while default beauty remains unchanged"
                       : "CinematicPostV3 adapter owns LDR output, exposure, bloom, grade, and tone-map evidence")
                : "CinematicPostV3 is missing LDR output, exposure, bloom, grade, or tone-map evidence");
    postDomain.enabled = contract.cinematicPost.enabled;
    postDomain.ready = context.cinematicPostV3Ready;
    postDomain.promotionState =
        context.cinematicPostV3Ready
            ? (cinematicPostV3OutputReady ? "producer" : "adapter")
            : "planned";
    postDomain.backingResources = {
        cinematicPostV3OutputReady ? "candidate_ldr_cinematic_output" : "back_buffer",
        cinematicPostV3OutputReady ? "candidate_hdr_scene_color" : "hdr_scene_color",
        "back_buffer",
        "hdr_scene_color",
        "bloom",
        "scene_post_exposure_policy",
        "scene_color_grade_policy",
    };
    postDomain.debugViews = {
        "candidate_hdr_scene_color",
        "candidate_ldr_cinematic_output",
        "ldr_cinematic_output",
        "exposure_meter",
        "bloom_extract",
        "color_grade_delta",
        "tone_map_curve",
    };
    postDomain.channels = {
        cinematicPostV3OutputReady
            ? "candidate_ldr_cinematic_output_owned_by_cinematic_post_v3"
            : (context.ldrCinematicOutputReady ? "ldr_cinematic_output_owned" : "ldr_cinematic_output_missing"),
        context.exposureMeterReady ? "exposure_meter_owned" : "exposure_meter_missing",
        context.bloomExtractReady ? "bloom_extract_owned" : "bloom_extract_missing",
        context.colorGradeReady ? "color_grade_delta_owned" : "color_grade_delta_missing",
        context.toneMapReady ? "tone_map_owned" : "tone_map_missing",
    };
    postDomain.backingResourceCount = readyPostChannels;
    postDomain.requiredChannelCount = 5u;
    postDomain.readyChannelCount = readyPostChannels;
    postDomain.missingRequiredChannelCount =
        postDomain.requiredChannelCount - postDomain.readyChannelCount;

    const bool candidateLdrOutputReady =
        FullSceneShaderHasResource(contract, "candidate_ldr_cinematic_output") &&
        (FullSceneShaderPassWritesResource(
             contract,
             "CinematicPostV3",
             "candidate_ldr_cinematic_output") ||
         FullSceneShaderPassWritesResource(
             contract,
             "FullSceneCandidateBeautyV3",
             "candidate_ldr_cinematic_output"));
    const bool candidateReadsHdr =
        FullSceneShaderPassReadsResource(contract, "CinematicPostV3", "candidate_hdr_scene_color") ||
        FullSceneShaderPassReadsResource(contract, "FullSceneCandidateBeautyV3", "hdr_color");
    context.candidateBeautyReady =
        context.candidateBeautyRequested &&
        context.compositeV3Ready &&
        context.cinematicPostV3Ready &&
        candidateLdrOutputReady &&
        candidateReadsHdr;
    context.candidateBeautyProducer =
        context.candidateBeautyReady
            ? (cinematicPostV3OutputReady ? "CinematicPostV3" : "FullSceneCandidateBeautyV3")
            : (context.candidateBeautyRequested
                   ? (cinematicPostV3OutputReady ? "CinematicPostV3" : "FullSceneCandidateBeautyV3")
                   : "none");
    context.candidateBeautyOutput =
        context.candidateBeautyRequested ? "candidate_ldr_cinematic_output" : "none";

    FullSceneShaderPipelineV3DomainEvidence candidateBeautyDomain =
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "candidate_beauty",
            context.candidateBeautyProducer,
            context.candidateBeautyOutput,
            "candidate_beauty_v3",
            context.candidateBeautyReady
                ? (cinematicPostV3OutputReady
                       ? "CinematicPostV3 is opt-in and writes a real candidate LDR output without affecting default beauty"
                       : "FullSceneCandidateBeautyV3 is opt-in and writes a real candidate LDR output without affecting default beauty")
                : context.candidateBeautyRequested
                ? "Candidate beauty was requested but its output resource, pass, or upstream composite/post evidence is incomplete"
                : "Candidate beauty is opt-in and was not requested this frame");
    candidateBeautyDomain.enabled = context.candidateBeautyRequested;
    candidateBeautyDomain.ready = context.candidateBeautyReady;
    candidateBeautyDomain.defaultBeautyAffects = false;
    candidateBeautyDomain.promotionState =
        context.candidateBeautyReady ? "candidate" : "planned";
    candidateBeautyDomain.backingResources = {
        realCompositeV3ProducerReady ? "candidate_hdr_scene_color" : "hdr_scene_color",
        "hdr_scene_color",
        "ldr_cinematic_output",
        "candidate_ldr_cinematic_output",
        "scene_local_environment",
        "reflection_radiance",
        "lighting_split",
    };
    candidateBeautyDomain.debugViews = {
        "candidate_beauty_v3",
        "candidate_hdr_scene_color",
        "hdr_scene_color",
        "ldr_cinematic_output",
    };
    candidateBeautyDomain.channels = {
        context.candidateBeautyRequested ? "candidate_requested" : "candidate_not_requested",
        context.compositeV3Ready ? "composite_ready" : "composite_missing",
        context.cinematicPostV3Ready ? "cinematic_post_ready" : "cinematic_post_missing",
        candidateLdrOutputReady ? "candidate_ldr_output_owned" : "candidate_ldr_output_missing",
        candidateReadsHdr
            ? (cinematicPostV3OutputReady ? "candidate_reads_candidate_hdr_scene_color" : "candidate_reads_hdr_color")
            : "candidate_hdr_input_missing",
        "default_beauty_unchanged",
    };
    candidateBeautyDomain.backingResourceCount =
        static_cast<uint32_t>(
            (context.compositeV3Ready ? 1u : 0u) +
            (context.cinematicPostV3Ready ? 1u : 0u) +
            (candidateLdrOutputReady ? 1u : 0u) +
            (candidateReadsHdr ? 1u : 0u));
    candidateBeautyDomain.requiredChannelCount = 5u;
    candidateBeautyDomain.readyChannelCount =
        context.candidateBeautyReady ? candidateBeautyDomain.requiredChannelCount : 0u;
    candidateBeautyDomain.missingRequiredChannelCount =
        candidateBeautyDomain.requiredChannelCount - candidateBeautyDomain.readyChannelCount;

    context.domains = {
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "render_graph",
            "FullSceneShaderPipelineV3",
            "v3_resource_registry",
            "v3_resource_ownership",
            "V3 render-graph resources are planned but not allocated"),
        materialDomain,
        lightingDomain,
        reflectionDomain,
        environmentDomain,
        compositeDomain,
        postDomain,
        candidateBeautyDomain,
        MakeFullSceneShaderPipelineV3DomainEvidence(
            "validation",
            "FullSceneShaderPipelineV3PacketGate",
            "v3_signal.json",
            "contact_sheet.png",
            "V3 packet gate is planned but not implemented"),
    };
    return context;
}

} // namespace Cortex::Graphics
