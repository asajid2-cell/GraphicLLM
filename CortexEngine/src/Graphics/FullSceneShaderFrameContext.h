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
        bool exposurePolicyReady = false;
        bool exposureClippingGatePassed = false;
        uint32_t lightCount = 0;
        uint32_t rectAreaLightCount = 0;
        uint32_t semanticFixtureLightCount = 0;
        uint32_t softFixtureLightCount = 0;
        uint32_t emissiveFixtureLightCount = 0;
        uint32_t stageFixtureLightCount = 0;
        uint32_t practicalFixtureLightCount = 0;
        uint32_t shadowCastingLightCount = 0;
        float totalLightIntensity = 0.0f;
        float maxLightIntensity = 0.0f;
        uint32_t missingLightingContractCount = 0;
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
    evidence.rectAreaLightCount = contract.lighting.areaRectLightCount;
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
