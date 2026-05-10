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
inline constexpr D3D12_RESOURCE_STATES kVBShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

inline bool IsVBGBufferDebugView(uint32_t debugView) {
    return debugView >= kVBDebugGBufferAlbedo && debugView <= kVBDebugGBufferExt2;
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
    D3D12_RESOURCE_STATES rtGIState) {
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

} // namespace Cortex::Graphics::VisibilityBufferGraphDetail
