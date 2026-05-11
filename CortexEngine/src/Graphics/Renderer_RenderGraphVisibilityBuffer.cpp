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
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_services.visibilityBuffer || !m_depthResources.resources.buffer || !m_mainTargets.hdr.resources.color) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_visibility_buffer_prerequisites_missing";
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
            m_depthResources.resources.buffer.Get(),
            m_depthResources.resources.resourceState,
            m_mainTargets.hdr.resources.color.Get(),
            m_mainTargets.hdr.resources.state,
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
        std::string vbStageFailureStage;
        std::string vbStageFailureError;
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
        VisibilityBufferGraphPass::StageFailureContext vbFailure{
            &vbStageFailed,
            &vbStageFailureStage,
            &vbStageFailureError
        };
        vbGraphContext.clear.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.clear.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.clear.failure = vbFailure;
        vbGraphContext.visibility.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.visibility.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.visibility.depthBuffer = m_depthResources.resources.buffer.Get();
        vbGraphContext.visibility.depthDSV = m_depthResources.descriptors.dsv.cpu;
        vbGraphContext.visibility.viewProjection = &m_constantBuffers.frameCPU.viewProjectionMatrix;
        vbGraphContext.visibility.meshDraws = &m_visibilityBufferState.meshDraws;
        vbGraphContext.visibility.cullMaskAddress = vbCullMaskAddress;
        vbGraphContext.visibility.depthState = &m_depthResources.resources.resourceState;
        vbGraphContext.visibility.instanceCount = static_cast<uint32_t>(m_visibilityBufferState.instances.size());
        vbGraphContext.visibility.contractInstances = &m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances;
        vbGraphContext.visibility.contractMeshes = &m_frameDiagnostics.contract.drawCounts.visibilityBufferMeshes;
        vbGraphContext.visibility.contractDrawBatches = &m_frameDiagnostics.contract.drawCounts.visibilityBufferDrawBatches;
        vbGraphContext.visibility.failure = vbFailure;
        vbGraphContext.materialResolve.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.materialResolve.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.materialResolve.depthBuffer = m_depthResources.resources.buffer.Get();
        vbGraphContext.materialResolve.depthSRV = m_depthResources.descriptors.srv.cpu;
        vbGraphContext.materialResolve.viewProjection = &m_constantBuffers.frameCPU.viewProjectionMatrix;
        vbGraphContext.materialResolve.meshDraws = &m_visibilityBufferState.meshDraws;
        vbGraphContext.materialResolve.biomeMaterialsAddress =
            m_constantBuffers.biomeMaterialsValid ? m_constantBuffers.biomeMaterials.gpuAddress : 0;
        vbGraphContext.materialResolve.depthState = &m_depthResources.resources.resourceState;
        vbGraphContext.materialResolve.failure = vbFailure;
        vbGraphContext.debugBlit.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.debugBlit.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.debugBlit.hdrTarget = m_mainTargets.hdr.resources.color.Get();
        vbGraphContext.debugBlit.hdrRTV = m_mainTargets.hdr.descriptors.rtv.cpu;
        vbGraphContext.debugBlit.depthBuffer = m_depthResources.resources.buffer.Get();
        vbGraphContext.debugBlit.debugVisibility = debugVisibility;
        vbGraphContext.debugBlit.debugDepth = debugDepth;
        vbGraphContext.debugBlit.debugGBuffer = debugGBuffer;
        vbGraphContext.debugBlit.gbufferSource = SelectVBGBufferDebugBuffer(vbDebugView);
        vbGraphContext.debugBlit.hdrState = &m_mainTargets.hdr.resources.state;
        vbGraphContext.debugBlit.depthState = &m_depthResources.resources.resourceState;
        vbGraphContext.debugBlit.renderedThisFrame = &m_visibilityBufferState.renderedThisFrame;
        vbGraphContext.debugBlit.debugOverrideThisFrame = &m_visibilityBufferState.debugOverrideThisFrame;
        vbGraphContext.debugBlit.failure = vbFailure;
        vbGraphContext.brdfLut.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.brdfLut.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.brdfLut.failure = vbFailure;
        vbGraphContext.clusteredLights.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.clusteredLights.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.clusteredLights.params = deferredInputs.params;
        vbGraphContext.clusteredLights.failure = vbFailure;
        vbGraphContext.deferredLighting = [&]() {
            if (vbStageFailed) return;
            m_depthResources.resources.resourceState = kDepthSampleState;
            m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
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
        if (!vbStageFailureStage.empty()) {
            spdlog::warn("VisibilityBuffer RG {} failed: {}", vbStageFailureStage, vbStageFailureError);
        }
        AccumulateRenderGraphExecutionStats(&result);
        if (execResult.IsErr()) {
            result.fallbackUsed = true;
            result.fallbackReason = execResult.Error();
        } else if (vbStageFailed) {
            result.fallbackUsed = true;
            result.fallbackReason = "visibility_buffer_graph_stage_failed";
        } else {
            m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(vbResources.depth);
            m_mainTargets.hdr.resources.state = m_services.renderGraph->GetResourceState(vbResources.hdr);
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
            spdlog::warn("VisibilityBuffer RG: {} (staged path did not complete)", result.fallbackReason);
        }

        return result;
    }

    result.fallbackUsed = true;
    result.fallbackReason = "visibility_buffer_graph_resources_missing";
    return result;
}


} // namespace Cortex::Graphics
