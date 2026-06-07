#pragma once

#include "RenderGraph.h"
#include "VisibilityBuffer.h"

#include <cstdint>

namespace Cortex::Graphics::VisibilityBufferGraphDetail {

inline constexpr uint32_t kVBDebugNone = 0;
inline constexpr uint32_t kVBDebugVisibility = 1;
inline constexpr uint32_t kVBDebugDepth = 2;
inline constexpr uint32_t kVBDebugGBufferAlbedo = 3;
inline constexpr uint32_t kVBDebugGBufferNormal = 4;
inline constexpr uint32_t kVBDebugGBufferEmissive = 5;
inline constexpr uint32_t kVBDebugGBufferExt0 = 6;
inline constexpr uint32_t kVBDebugGBufferExt1 = 7;
inline constexpr uint32_t kVBDebugGBufferExt2 = 8;
inline constexpr uint32_t kVBDebugMaterialId = 9;
inline constexpr uint32_t kVBDebugStableObjectId = 10;
inline constexpr uint32_t kVBDebugMaterialFamily = 11;
inline constexpr uint32_t kVBDebugReflectionPolicy = 12;
inline constexpr uint32_t kVBDebugTemporalPolicy = 13;
inline constexpr uint32_t kVBDebugPostSensitivity = 14;
inline constexpr uint32_t kVBDebugMaterialMissingChannelMask = 20;
inline constexpr uint32_t kVBDebugLightingV3Direct = 15;
inline constexpr uint32_t kVBDebugLightingV3DirectUnshadowed = 16;
inline constexpr uint32_t kVBDebugLightingV3ShadowVisibility = 17;
inline constexpr uint32_t kVBDebugLightingV3ShadowLoss = 18;
inline constexpr uint32_t kVBDebugLightingV3Indirect = 19;
inline constexpr uint32_t kVBDebugLightingV3EnergyBudget = 21;
inline constexpr uint32_t kVBDebugLightingV3ShadowAttribution = 22;
inline constexpr D3D12_RESOURCE_STATES kVBShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

inline bool IsVBGBufferDebugView(uint32_t debugView) {
    return debugView >= kVBDebugGBufferAlbedo && debugView <= kVBDebugGBufferExt2;
}

inline bool IsVBFullSceneLightingV3DebugView(uint32_t debugView) {
    return (debugView >= kVBDebugLightingV3Direct && debugView <= kVBDebugLightingV3Indirect) ||
           debugView == kVBDebugLightingV3EnergyBudget ||
           debugView == kVBDebugLightingV3ShadowAttribution;
}

inline bool IsVBVisibilityIdentityDebugView(uint32_t debugView) {
    return debugView == kVBDebugVisibility ||
           debugView == kVBDebugMaterialId ||
           debugView == kVBDebugStableObjectId ||
           debugView == kVBDebugMaterialFamily ||
           debugView == kVBDebugReflectionPolicy ||
           debugView == kVBDebugTemporalPolicy ||
           debugView == kVBDebugPostSensitivity ||
           debugView == kVBDebugMaterialMissingChannelMask;
}

inline VisibilityBufferRenderer::DebugBlitVisibilityMode SelectVBVisibilityDebugMode(uint32_t debugView) {
    if (debugView == kVBDebugMaterialId) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialId;
    }
    if (debugView == kVBDebugStableObjectId) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::StableObjectId;
    }
    if (debugView == kVBDebugMaterialFamily) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialFamily;
    }
    if (debugView == kVBDebugReflectionPolicy) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::ReflectionPolicy;
    }
    if (debugView == kVBDebugTemporalPolicy) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::TemporalPolicy;
    }
    if (debugView == kVBDebugPostSensitivity) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::PostSensitivity;
    }
    if (debugView == kVBDebugMaterialMissingChannelMask) {
        return VisibilityBufferRenderer::DebugBlitVisibilityMode::MaterialMissingChannelMask;
    }
    return VisibilityBufferRenderer::DebugBlitVisibilityMode::PayloadInstance;
}

struct VisibilityBufferGraphResources {
    VisibilityBufferRenderer::ResourceStateSnapshot initialStates{};
    RGResourceHandle depth;
    RGResourceHandle hdr;
    RGResourceHandle visibility;
    RGResourceHandle albedo;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt0;
    RGResourceHandle materialExt1;
    RGResourceHandle materialExt2;
    RGResourceHandle brdfLut;
    RGResourceHandle clusterRanges;
    RGResourceHandle clusterLightIndices;
    RGResourceHandle shadow;
    RGResourceHandle rtShadow;
    RGResourceHandle rtGI;
    RGResourceHandle directLighting;
    RGResourceHandle directLightingUnshadowed;
    RGResourceHandle shadowVisibility;
    RGResourceHandle shadowLoss;
    RGResourceHandle indirectLighting;
    RGResourceHandle lightingEnergyBudget;
    RGResourceHandle shadowSourceAttribution;
};

inline RGResourceHandle ImportOptionalResource(RenderGraph& graph,
                                               ID3D12Resource* resource,
                                               D3D12_RESOURCE_STATES state,
                                               const char* name) {
    if (!resource) {
        return {};
    }
    return graph.ImportResource(resource, state, name);
}

inline VisibilityBufferGraphResources ImportVisibilityBufferGraphResources(
    RenderGraph& graph,
    VisibilityBufferRenderer& visibilityBuffer,
    ID3D12Resource* depthBuffer,
    D3D12_RESOURCE_STATES depthState,
    ID3D12Resource* hdrColor,
    D3D12_RESOURCE_STATES hdrState,
    ID3D12Resource* shadowMap,
    D3D12_RESOURCE_STATES shadowMapState,
    ID3D12Resource* rtShadowMask,
    D3D12_RESOURCE_STATES rtShadowMaskState,
    ID3D12Resource* rtGIColor,
    D3D12_RESOURCE_STATES rtGIState,
    ID3D12Resource* directLighting = nullptr,
    ID3D12Resource* directLightingUnshadowed = nullptr,
    ID3D12Resource* shadowVisibility = nullptr,
    ID3D12Resource* shadowLoss = nullptr,
    ID3D12Resource* indirectLighting = nullptr,
    ID3D12Resource* lightingEnergyBudget = nullptr,
    ID3D12Resource* shadowSourceAttribution = nullptr,
    D3D12_RESOURCE_STATES lightingSplitState = D3D12_RESOURCE_STATE_COMMON) {
    VisibilityBufferGraphResources resources{};
    resources.initialStates = visibilityBuffer.GetResourceStateSnapshot();

    resources.depth = graph.ImportResource(depthBuffer, depthState, "Depth_VB");
    resources.hdr = graph.ImportResource(hdrColor, hdrState, "HDR_VB");
    resources.visibility = graph.ImportResource(
        visibilityBuffer.GetVisibilityBuffer(), resources.initialStates.visibility, "VB_Visibility");
    resources.albedo = graph.ImportResource(
        visibilityBuffer.GetAlbedoBuffer(), resources.initialStates.albedo, "VB_Albedo");
    resources.normalRoughness = graph.ImportResource(
        visibilityBuffer.GetNormalRoughnessBuffer(), resources.initialStates.normalRoughness, "VB_NormalRoughness");
    resources.emissiveMetallic = graph.ImportResource(
        visibilityBuffer.GetEmissiveMetallicBuffer(), resources.initialStates.emissiveMetallic, "VB_EmissiveMetallic");
    resources.materialExt0 = graph.ImportResource(
        visibilityBuffer.GetMaterialExt0Buffer(), resources.initialStates.materialExt0, "VB_MaterialExt0");
    resources.materialExt1 = graph.ImportResource(
        visibilityBuffer.GetMaterialExt1Buffer(), resources.initialStates.materialExt1, "VB_MaterialExt1");
    resources.materialExt2 = graph.ImportResource(
        visibilityBuffer.GetMaterialExt2Buffer(), resources.initialStates.materialExt2, "VB_MaterialExt2");

    resources.brdfLut = ImportOptionalResource(
        graph, visibilityBuffer.GetBRDFLUT(), resources.initialStates.brdfLut, "VB_BRDF_LUT");
    resources.clusterRanges = ImportOptionalResource(
        graph, visibilityBuffer.GetClusterRangesBuffer(), resources.initialStates.clusterRanges, "VB_ClusterRanges");
    resources.clusterLightIndices = ImportOptionalResource(
        graph,
        visibilityBuffer.GetClusterLightIndicesBuffer(),
        resources.initialStates.clusterLightIndices,
        "VB_ClusterLightIndices");
    resources.shadow = ImportOptionalResource(graph, shadowMap, shadowMapState, "ShadowMap_VB");
    resources.rtShadow = ImportOptionalResource(graph, rtShadowMask, rtShadowMaskState, "RTShadowMask_VB");
    resources.rtGI = ImportOptionalResource(graph, rtGIColor, rtGIState, "RTGI_VB");
    resources.directLighting = ImportOptionalResource(
        graph, directLighting, lightingSplitState, "FullSceneV3_DirectLighting");
    resources.directLightingUnshadowed = ImportOptionalResource(
        graph, directLightingUnshadowed, lightingSplitState, "FullSceneV3_DirectLightingUnshadowed");
    resources.shadowVisibility = ImportOptionalResource(
        graph, shadowVisibility, lightingSplitState, "FullSceneV3_ShadowVisibility");
    resources.shadowLoss = ImportOptionalResource(
        graph, shadowLoss, lightingSplitState, "FullSceneV3_ShadowLoss");
    resources.indirectLighting = ImportOptionalResource(
        graph, indirectLighting, lightingSplitState, "FullSceneV3_IndirectLighting");
    resources.lightingEnergyBudget = ImportOptionalResource(
        graph, lightingEnergyBudget, lightingSplitState, "FullSceneV3_LightingEnergyBudget");
    resources.shadowSourceAttribution = ImportOptionalResource(
        graph, shadowSourceAttribution, lightingSplitState, "FullSceneV3_ShadowSourceAttribution");
    return resources;
}

inline RGResourceHandle SelectVBGBufferDebugHandle(const VisibilityBufferGraphResources& resources,
                                                   uint32_t debugView) {
    switch (debugView) {
        case kVBDebugGBufferNormal: return resources.normalRoughness;
        case kVBDebugGBufferEmissive: return resources.emissiveMetallic;
        case kVBDebugGBufferExt0: return resources.materialExt0;
        case kVBDebugGBufferExt1: return resources.materialExt1;
        case kVBDebugGBufferExt2: return resources.materialExt2;
        default: return resources.albedo;
    }
}

inline VisibilityBufferRenderer::DebugBlitBuffer SelectVBGBufferDebugBuffer(uint32_t debugView) {
    switch (debugView) {
        case kVBDebugGBufferNormal:
            return VisibilityBufferRenderer::DebugBlitBuffer::NormalRoughness;
        case kVBDebugGBufferEmissive:
            return VisibilityBufferRenderer::DebugBlitBuffer::EmissiveMetallic;
        case kVBDebugGBufferExt0:
            return VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt0;
        case kVBDebugGBufferExt1:
            return VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt1;
        case kVBDebugGBufferExt2:
            return VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt2;
        default:
            return VisibilityBufferRenderer::DebugBlitBuffer::Albedo;
    }
}

inline RGResourceHandle SelectVBFullSceneLightingV3DebugHandle(const VisibilityBufferGraphResources& resources,
                                                               uint32_t debugView) {
    switch (debugView) {
        case kVBDebugLightingV3DirectUnshadowed: return resources.directLightingUnshadowed;
        case kVBDebugLightingV3ShadowVisibility: return resources.shadowVisibility;
        case kVBDebugLightingV3ShadowLoss: return resources.shadowLoss;
        case kVBDebugLightingV3Indirect: return resources.indirectLighting;
        case kVBDebugLightingV3EnergyBudget: return resources.lightingEnergyBudget;
        case kVBDebugLightingV3ShadowAttribution: return resources.shadowSourceAttribution;
        case kVBDebugLightingV3Direct:
        default: return resources.directLighting;
    }
}

} // namespace Cortex::Graphics::VisibilityBufferGraphDetail
