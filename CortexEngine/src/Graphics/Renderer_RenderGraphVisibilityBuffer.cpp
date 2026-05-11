#include "Renderer.h"

#include "Passes/VisibilityBufferGraphPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"
#include "Renderer_RenderGraphVisibilityBufferHelpers.h"
#include "Scene/ECS_Registry.h"

#include <spdlog/spdlog.h>

#include <string>

namespace Cortex::Graphics {

using namespace VisibilityBufferGraphDetail;

Renderer::RenderGraphPassResult
Renderer::ExecuteVisibilityBufferInRenderGraph(Scene::ECS_Registry* registry) {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_services.visibilityBuffer || !m_depthResources.buffer || !m_mainTargets.hdrColor) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_visibility_buffer_prerequisites_missing";
        RenderVisibilityBufferPath(registry);
        result.executed = m_visibilityBufferState.renderedThisFrame;
        return result;
    }

    const uint32_t vbDebugView = GetVisibilityBufferDebugView();
    if (m_services.visibilityBuffer->GetVisibilityBuffer() &&
        m_services.visibilityBuffer->GetAlbedoBuffer() &&
        m_services.visibilityBuffer->GetNormalRoughnessBuffer() &&
        m_services.visibilityBuffer->GetEmissiveMetallicBuffer() &&
        m_services.visibilityBuffer->GetMaterialExt0Buffer() &&
        m_services.visibilityBuffer->GetMaterialExt1Buffer() &&
        m_services.visibilityBuffer->GetMaterialExt2Buffer()) {
        CollectInstancesForVisibilityBuffer(registry);
        if (m_visibilityBufferState.instances.empty() || m_visibilityBufferState.meshDraws.empty()) {
            spdlog::warn("VB: No instances collected (instances={}, meshDraws={})",
                         m_visibilityBufferState.instances.size(), m_visibilityBufferState.meshDraws.size());
            return result;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS vbCullMaskAddress = ResolveVisibilityBufferCullMask(vbDebugView);
        LogVisibilityBufferFirstFrame();
        const bool debugVisibility = (vbDebugView == kVBDebugVisibility);
        const bool debugDepth = (vbDebugView == kVBDebugDepth);
        const bool debugGBuffer = IsVBGBufferDebugView(vbDebugView);
        const bool debugPath = (vbDebugView != kVBDebugNone);
        const bool needsMaterialResolve = !debugVisibility && !debugDepth;
        const auto deferredInputs = debugPath
            ? VisibilityBufferDeferredLightingInputs{}
            : PrepareVisibilityBufferDeferredLighting(registry);

        m_services.renderGraph->BeginFrame();
        const VisibilityBufferGraphResources vbResources = ImportVisibilityBufferGraphResources(
            *m_services.renderGraph,
            *m_services.visibilityBuffer,
            m_depthResources.buffer.Get(),
            m_depthResources.resourceState,
            m_mainTargets.hdrColor.Get(),
            m_mainTargets.hdrState,
            m_shadowResources.resources.map.Get(),
            m_shadowResources.resources.resourceState,
            m_rtShadowTargets.mask.Get(),
            m_rtShadowTargets.maskState,
            m_rtGITargets.color.Get(),
            m_rtGITargets.colorState);
        const bool clusterGraphOwned =
            !debugPath &&
            !deferredInputs.localLights.empty() &&
            vbResources.clusterRanges.IsValid() &&
            vbResources.clusterLightIndices.IsValid();

        bool vbStageFailed = false;
        auto markStageFailure = [&](const char* stage, const std::string& error) {
            if (!vbStageFailed) {
                spdlog::warn("VisibilityBuffer RG {} failed: {}", stage, error);
            }
            vbStageFailed = true;
        };

        const bool brdfGraphOwned =
            !debugPath && vbResources.brdfLut.IsValid() && !m_services.visibilityBuffer->IsBRDFLUTReady();
        VisibilityBufferGraphPass::ResourceHandles vbPassResources{};
        vbPassResources.depth = vbResources.depth;
        vbPassResources.hdr = vbResources.hdr;
        vbPassResources.visibility = vbResources.visibility;
        vbPassResources.albedo = vbResources.albedo;
        vbPassResources.normalRoughness = vbResources.normalRoughness;
        vbPassResources.emissiveMetallic = vbResources.emissiveMetallic;
        vbPassResources.materialExt0 = vbResources.materialExt0;
        vbPassResources.materialExt1 = vbResources.materialExt1;
        vbPassResources.materialExt2 = vbResources.materialExt2;
        vbPassResources.brdfLut = vbResources.brdfLut;
        vbPassResources.clusterRanges = vbResources.clusterRanges;
        vbPassResources.clusterLightIndices = vbResources.clusterLightIndices;
        vbPassResources.shadow = vbResources.shadow;
        vbPassResources.rtShadow = vbResources.rtShadow;
        vbPassResources.rtGI = vbResources.rtGI;
        vbPassResources.debugSource = SelectVBGBufferDebugHandle(vbResources, vbDebugView);

        VisibilityBufferGraphPass::GraphContext vbGraphContext{};
        vbGraphContext.resources = vbPassResources;
        vbGraphContext.needsMaterialResolve = needsMaterialResolve;
        vbGraphContext.debugPath = debugPath;
        vbGraphContext.debugVisibility = debugVisibility;
        vbGraphContext.debugDepth = debugDepth;
        vbGraphContext.debugGBuffer = debugGBuffer;
        vbGraphContext.brdfGraphOwned = brdfGraphOwned;
        vbGraphContext.clusterGraphOwned = clusterGraphOwned;
        vbGraphContext.failStage = [&](const char* stage) {
            markStageFailure(stage ? stage : "graph_contract", "graph contract failed");
        };
        vbGraphContext.clear = [&]() {
            if (vbStageFailed) return;
            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.visibility = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.visibilityPass = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            auto clearResult = m_services.visibilityBuffer->ClearVisibilityBuffer(m_commandResources.graphicsList.Get());
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (clearResult.IsErr()) {
                markStageFailure("clear", clearResult.Error());
            }
        };
        vbGraphContext.visibility = [&]() {
            if (vbStageFailed) return;
            m_depthResources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.visibility = D3D12_RESOURCE_STATE_RENDER_TARGET;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.visibilityPass = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            auto visResult = m_services.visibilityBuffer->RasterizeVisibilityBuffer(
                m_commandResources.graphicsList.Get(),
                m_depthResources.buffer.Get(),
                m_depthResources.dsv.cpu,
                m_constantBuffers.frameCPU.viewProjectionMatrix,
                m_visibilityBufferState.meshDraws,
                vbCullMaskAddress);
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (visResult.IsErr()) {
                markStageFailure("visibility", visResult.Error());
                return;
            }

            uint32_t vbDrawBatches = 0;
            for (const auto& draw : m_visibilityBufferState.meshDraws) {
                vbDrawBatches += (draw.instanceCount > 0) ? 1u : 0u;
                vbDrawBatches += (draw.instanceCountDoubleSided > 0) ? 1u : 0u;
                vbDrawBatches += (draw.instanceCountAlpha > 0) ? 1u : 0u;
                vbDrawBatches += (draw.instanceCountAlphaDoubleSided > 0) ? 1u : 0u;
            }
            m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances = static_cast<uint32_t>(m_visibilityBufferState.instances.size());
            m_frameDiagnostics.contract.drawCounts.visibilityBufferMeshes = static_cast<uint32_t>(m_visibilityBufferState.meshDraws.size());
            m_frameDiagnostics.contract.drawCounts.visibilityBufferDrawBatches = vbDrawBatches;
        };
        vbGraphContext.materialResolve = [&]() {
            if (vbStageFailed) return;
            m_depthResources.resourceState = kDepthSampleState;

            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.visibility = kVBShaderResourceState;
            states.albedo = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.normalRoughness = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.emissiveMetallic = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.materialExt0 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.materialExt1 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.materialExt2 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.materialResolve = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            bool completedPath = false;
            const bool resolved = RenderVisibilityBufferMaterialResolveStage(kVBDebugNone, completedPath);
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (!resolved || completedPath) {
                markStageFailure("material_resolve", resolved ? "unexpected debug completion" : "material resolve failed");
            }
        };
        vbGraphContext.debugBlit = [&]() {
            if (vbStageFailed) return;

            m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            if (debugVisibility) {
                states.visibility = kVBShaderResourceState;
            } else if (debugGBuffer) {
                switch (vbDebugView) {
                    case kVBDebugGBufferNormal:
                        states.normalRoughness = kVBShaderResourceState;
                        break;
                    case kVBDebugGBufferEmissive:
                        states.emissiveMetallic = kVBShaderResourceState;
                        break;
                    case kVBDebugGBufferExt0:
                        states.materialExt0 = kVBShaderResourceState;
                        break;
                    case kVBDebugGBufferExt1:
                        states.materialExt1 = kVBShaderResourceState;
                        break;
                    case kVBDebugGBufferExt2:
                        states.materialExt2 = kVBShaderResourceState;
                        break;
                    default:
                        states.albedo = kVBShaderResourceState;
                        break;
                }
            }
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);
            if (debugDepth) {
                m_depthResources.resourceState = kDepthSampleState;
            }

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.debugBlit = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);

            Result<void> debugResult = Result<void>::Ok();
            if (debugVisibility) {
                debugResult = m_services.visibilityBuffer->DebugBlitVisibilityToHDR(
                    m_commandResources.graphicsList.Get(), m_mainTargets.hdrColor.Get(), m_mainTargets.hdrRTV.cpu);
            } else if (debugDepth) {
                debugResult = m_services.visibilityBuffer->DebugBlitDepthToHDR(
                    m_commandResources.graphicsList.Get(), m_mainTargets.hdrColor.Get(), m_mainTargets.hdrRTV.cpu, m_depthResources.buffer.Get());
            } else if (debugGBuffer) {
                debugResult = m_services.visibilityBuffer->DebugBlitGBufferToHDR(
                    m_commandResources.graphicsList.Get(), m_mainTargets.hdrColor.Get(), m_mainTargets.hdrRTV.cpu, SelectVBGBufferDebugBuffer(vbDebugView));
            }

            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (debugResult.IsErr()) {
                markStageFailure("debug_blit", debugResult.Error());
                return;
            }

            m_visibilityBufferState.renderedThisFrame = true;
            m_visibilityBufferState.debugOverrideThisFrame = true;
        };
        vbGraphContext.brdfLut = [&]() {
            if (vbStageFailed) return;

            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.brdfLut = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.brdfLut = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            auto brdfResult = m_services.visibilityBuffer->EnsureBRDFLUT(m_commandResources.graphicsList.Get());
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (brdfResult.IsErr()) {
                markStageFailure("brdf_lut", brdfResult.Error());
            }
        };
        vbGraphContext.clusteredLights = [&]() {
            if (vbStageFailed) return;

            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.clusterRanges = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            states.clusterLightIndices = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.clusteredLights = true;
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            auto clusterResult =
                m_services.visibilityBuffer->BuildClusteredLightLists(m_commandResources.graphicsList.Get(), deferredInputs.params);
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
            if (clusterResult.IsErr()) {
                markStageFailure("clustered_lights", clusterResult.Error());
            }
        };
        vbGraphContext.deferredLighting = [&]() {
            if (vbStageFailed) return;
            m_depthResources.resourceState = kDepthSampleState;
            m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (vbResources.shadow.IsValid()) {
                m_shadowResources.resources.resourceState = kVBShaderResourceState;
            }
            if (vbResources.rtShadow.IsValid()) {
                m_rtShadowTargets.maskState = kVBShaderResourceState;
            }
            if (vbResources.rtGI.IsValid()) {
                m_rtGITargets.colorState = kVBShaderResourceState;
            }

            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.albedo = kVBShaderResourceState;
            states.normalRoughness = kVBShaderResourceState;
            states.emissiveMetallic = kVBShaderResourceState;
            states.materialExt0 = kVBShaderResourceState;
            states.materialExt1 = kVBShaderResourceState;
            states.materialExt2 = kVBShaderResourceState;
            if (vbResources.brdfLut.IsValid()) {
                states.brdfLut = kVBShaderResourceState;
            }
            if (clusterGraphOwned) {
                states.clusterRanges = kVBShaderResourceState;
                states.clusterLightIndices = kVBShaderResourceState;
            }
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);

            auto controls = m_services.visibilityBuffer->GetTransitionSkipControls();
            const auto previousControls = controls;
            controls.deferredLighting = true;
            if (clusterGraphOwned) {
                controls.clusteredLights = true;
            }
            m_services.visibilityBuffer->SetTransitionSkipControls(controls);
            ApplyVisibilityBufferDeferredLighting(deferredInputs);
            m_services.visibilityBuffer->SetTransitionSkipControls(previousControls);
        };

        if (!VisibilityBufferGraphPass::AddStagedPath(*m_services.renderGraph, vbGraphContext)) {
            vbStageFailed = true;
        }

        const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
        AccumulateRenderGraphExecutionStats(&result);
        if (execResult.IsErr()) {
            result.fallbackUsed = true;
            result.fallbackReason = execResult.Error();
        } else if (vbStageFailed) {
            result.fallbackUsed = true;
            result.fallbackReason = "visibility_buffer_graph_stage_failed";
        } else {
            m_depthResources.resourceState = m_services.renderGraph->GetResourceState(vbResources.depth);
            m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(vbResources.hdr);
            if (vbResources.shadow.IsValid()) m_shadowResources.resources.resourceState = m_services.renderGraph->GetResourceState(vbResources.shadow);
            if (vbResources.rtShadow.IsValid()) m_rtShadowTargets.maskState = m_services.renderGraph->GetResourceState(vbResources.rtShadow);
            if (vbResources.rtGI.IsValid()) m_rtGITargets.colorState = m_services.renderGraph->GetResourceState(vbResources.rtGI);

            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.visibility = m_services.renderGraph->GetResourceState(vbResources.visibility);
            finalStates.albedo = m_services.renderGraph->GetResourceState(vbResources.albedo);
            finalStates.normalRoughness = m_services.renderGraph->GetResourceState(vbResources.normalRoughness);
            finalStates.emissiveMetallic = m_services.renderGraph->GetResourceState(vbResources.emissiveMetallic);
            finalStates.materialExt0 = m_services.renderGraph->GetResourceState(vbResources.materialExt0);
            finalStates.materialExt1 = m_services.renderGraph->GetResourceState(vbResources.materialExt1);
            finalStates.materialExt2 = m_services.renderGraph->GetResourceState(vbResources.materialExt2);
            if (vbResources.brdfLut.IsValid()) {
                finalStates.brdfLut = m_services.renderGraph->GetResourceState(vbResources.brdfLut);
            }
            if (vbResources.clusterRanges.IsValid()) {
                finalStates.clusterRanges = m_services.renderGraph->GetResourceState(vbResources.clusterRanges);
            }
            if (vbResources.clusterLightIndices.IsValid()) {
                finalStates.clusterLightIndices = m_services.renderGraph->GetResourceState(vbResources.clusterLightIndices);
            }
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
            RecordFramePass("VBClear", true, true, 0,
                            {},
                            {"vb_visibility"},
                            false, nullptr, true);
            RecordFramePass("VBVisibility", true, true, m_frameDiagnostics.contract.drawCounts.visibilityBufferDrawBatches,
                            {"depth"},
                            {"vb_visibility", "depth"},
                            false, nullptr, true);
            if (needsMaterialResolve) {
                RecordFramePass("VBMaterialResolve", true, true, 0,
                                {"vb_visibility", "depth"},
                                {"gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_emissive_metallic",
                                 "gbuffer_material_ext0", "gbuffer_material_ext1", "gbuffer_material_ext2"},
                                false, nullptr, true);
            }
            if (debugPath) {
                RecordFramePass("VBDebugBlit", true, m_visibilityBufferState.renderedThisFrame, 1,
                                debugVisibility ? std::initializer_list<const char*>{"vb_visibility"} :
                                (debugDepth ? std::initializer_list<const char*>{"depth"} :
                                              std::initializer_list<const char*>{"vb_debug_source"}),
                                {"hdr_color"},
                                false, nullptr, true);
            }
            if (brdfGraphOwned) {
                RecordFramePass("VBBRDFLUT", true, true, 0,
                                {},
                                {"brdf_lut"},
                                false, nullptr, true);
            }
            if (clusterGraphOwned) {
                RecordFramePass("VBClusteredLights", true, true, 0,
                                {"local_lights"},
                                {"cluster_ranges", "cluster_light_indices"},
                                false, nullptr, true);
            }
            if (!debugPath) {
                RecordFramePass("VBDeferredLighting", true, m_visibilityBufferState.renderedThisFrame, 1,
                                {"depth", "gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_emissive_metallic",
                                 "gbuffer_material_ext0", "gbuffer_material_ext1", "gbuffer_material_ext2",
                                 "brdf_lut", "cluster_ranges", "cluster_light_indices", "shadow_map"},
                                {"hdr_color"},
                                false, nullptr, true);
            }
            result.executed = m_visibilityBufferState.renderedThisFrame;
        }
        m_services.renderGraph->EndFrame();

        if (result.fallbackUsed) {
            ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
            spdlog::warn("VisibilityBuffer RG: {} (staged path did not complete)", result.fallbackReason);
        }

        return result;
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_VB");
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdrColor.Get(), m_mainTargets.hdrState, "HDR_VB");

    RGResourceHandle shadowHandle{};
    if (m_shadowResources.resources.map) {
        shadowHandle = m_services.renderGraph->ImportResource(m_shadowResources.resources.map.Get(), m_shadowResources.resources.resourceState, "ShadowMap_VB");
    }

    RGResourceHandle rtShadowHandle{};
    if (m_rtShadowTargets.mask) {
        rtShadowHandle = m_services.renderGraph->ImportResource(m_rtShadowTargets.mask.Get(), m_rtShadowTargets.maskState, "RTShadowMask_VB");
    }

    RGResourceHandle rtGIHandle{};
    if (m_rtGITargets.color) {
        rtGIHandle = m_services.renderGraph->ImportResource(m_rtGITargets.color.Get(), m_rtGITargets.colorState, "RTGI_VB");
    }

    VisibilityBufferGraphPass::LegacyPathContext legacyGraphContext{};
    legacyGraphContext.depth = depthHandle;
    legacyGraphContext.hdr = hdrHandle;
    legacyGraphContext.shadow = shadowHandle;
    legacyGraphContext.rtShadow = rtShadowHandle;
    legacyGraphContext.rtGI = rtGIHandle;
    legacyGraphContext.execute = [&, shadowHandle, rtShadowHandle, rtGIHandle]() {
            // The graph moves external resources into the starting states for
            // the legacy VB path. VB-internal resources still perform their own
            // transitions until this path is split into visibility/resolve/light
            // graph nodes.
            m_depthResources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (shadowHandle.IsValid()) {
                m_shadowResources.resources.resourceState =
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            if (rtShadowHandle.IsValid()) {
                m_rtShadowTargets.maskState =
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            if (rtGIHandle.IsValid()) {
                m_rtGITargets.colorState =
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            RenderVisibilityBufferPath(registry);
    };
    legacyGraphContext.failStage = [&](const char* reason) {
        result.fallbackUsed = true;
        result.fallbackReason = reason ? reason : "visibility_buffer_legacy_graph_contract";
    };

    if (!VisibilityBufferGraphPass::AddLegacyPath(*m_services.renderGraph, legacyGraphContext)) {
        m_services.renderGraph->EndFrame();
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("VisibilityBuffer RG: {} (falling back to legacy barriers)", result.fallbackReason);
        RenderVisibilityBufferPath(registry);
        result.executed = m_visibilityBufferState.renderedThisFrame;
        return result;
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);
    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else {
        m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(hdrHandle);
        if (shadowHandle.IsValid()) m_shadowResources.resources.resourceState = m_services.renderGraph->GetResourceState(shadowHandle);
        if (rtShadowHandle.IsValid()) m_rtShadowTargets.maskState = m_services.renderGraph->GetResourceState(rtShadowHandle);
        if (rtGIHandle.IsValid()) m_rtGITargets.colorState = m_services.renderGraph->GetResourceState(rtGIHandle);
        result.executed = m_visibilityBufferState.renderedThisFrame;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("VisibilityBuffer RG: {} (falling back to legacy barriers)", result.fallbackReason);
        RenderVisibilityBufferPath(registry);
        result.executed = m_visibilityBufferState.renderedThisFrame;
    }

    return result;
}


} // namespace Cortex::Graphics
