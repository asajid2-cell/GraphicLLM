#include "Renderer.h"

#include "Passes/VisibilityBufferGraphPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"
#include "Renderer_RenderGraphVisibilityBufferHelpers.h"
#include "Scene/ECS_Registry.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>

namespace Cortex::Graphics {

using namespace VisibilityBufferGraphDetail;

namespace {

DescriptorHandle SelectVBFullSceneLightingV3DebugSRV(const FullSceneLightingV3TargetDescriptors& descriptors,
                                                     uint32_t debugView) {
    switch (debugView) {
        case kVBDebugLightingV3DirectUnshadowed: return descriptors.directLightingUnshadowedSRV;
        case kVBDebugLightingV3ShadowVisibility: return descriptors.shadowVisibilitySRV;
        case kVBDebugLightingV3ShadowLoss: return descriptors.shadowLossSRV;
        case kVBDebugLightingV3Indirect: return descriptors.indirectLightingSRV;
        case kVBDebugLightingV3EnergyBudget: return descriptors.lightingEnergyBudgetSRV;
        case kVBDebugLightingV3ShadowAttribution: return descriptors.shadowSourceAttributionSRV;
        case kVBDebugLightingV3Direct:
        default: return descriptors.directLightingSRV;
    }
}

const char* SelectVBFullSceneLightingV3DebugResourceName(uint32_t debugView) {
    switch (debugView) {
        case kVBDebugLightingV3DirectUnshadowed: return "direct_lighting_unshadowed";
        case kVBDebugLightingV3ShadowVisibility: return "shadow_visibility";
        case kVBDebugLightingV3ShadowLoss: return "shadow_loss";
        case kVBDebugLightingV3Indirect: return "indirect_lighting";
        case kVBDebugLightingV3EnergyBudget: return "lighting_energy_budget";
        case kVBDebugLightingV3ShadowAttribution: return "shadow_source_attribution";
        case kVBDebugLightingV3Direct:
        default: return "direct_lighting";
    }
}

} // namespace

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
        const bool debugVisibility = IsVBVisibilityIdentityDebugView(vbDebugView);
        const bool debugDepth = (vbDebugView == kVBDebugDepth);
        const bool debugGBuffer = IsVBGBufferDebugView(vbDebugView);
        const bool debugLightingV3 = IsVBFullSceneLightingV3DebugView(vbDebugView);
        const bool debugPath = (vbDebugView != kVBDebugNone) && !debugLightingV3;
        const bool needsMaterialResolve = !debugVisibility && !debugDepth;
        const auto deferredInputs = debugPath
            ? VisibilityBufferDeferredLightingInputs{}
            : PrepareVisibilityBufferDeferredLighting(registry);
        const bool candidateBeautyV3Requested =
            m_postProcessState.fullSceneCandidateBeautyV3Enabled ||
            std::getenv("CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3") != nullptr ||
            std::getenv("CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3") != nullptr;
        const bool fullSceneLightingV3Requested =
            debugLightingV3 ||
            candidateBeautyV3Requested ||
            std::getenv("CORTEX_ENABLE_FULL_SCENE_LIGHTING_V3_SPLIT") != nullptr;
        const bool fullSceneLightingV3Enabled =
            !debugPath &&
            fullSceneLightingV3Requested &&
            m_mainTargets.lightingV3.resources.directLighting &&
            m_mainTargets.lightingV3.resources.directLightingUnshadowed &&
            m_mainTargets.lightingV3.resources.shadowVisibility &&
            m_mainTargets.lightingV3.resources.shadowLoss &&
            m_mainTargets.lightingV3.resources.indirectLighting &&
            m_mainTargets.lightingV3.resources.lightingEnergyBudget &&
            m_mainTargets.lightingV3.resources.shadowSourceAttribution;

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
            m_rtGITargets.colorState,
            m_mainTargets.lightingV3.resources.directLighting.Get(),
            m_mainTargets.lightingV3.resources.directLightingUnshadowed.Get(),
            m_mainTargets.lightingV3.resources.shadowVisibility.Get(),
            m_mainTargets.lightingV3.resources.shadowLoss.Get(),
            m_mainTargets.lightingV3.resources.indirectLighting.Get(),
            m_mainTargets.lightingV3.resources.lightingEnergyBudget.Get(),
            m_mainTargets.lightingV3.resources.shadowSourceAttribution.Get(),
            m_mainTargets.lightingV3.resources.state);
        const bool clusterGraphOwned =
            !debugPath &&
            !deferredInputs.localLights.empty() &&
            vbResources.clusterRanges.IsValid() &&
            vbResources.clusterLightIndices.IsValid();

        bool vbStageFailed = false;
        std::string vbStageFailureStage;
        std::string vbStageFailureError;

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
        vbPassResources.directLighting = vbResources.directLighting;
        vbPassResources.directLightingUnshadowed = vbResources.directLightingUnshadowed;
        vbPassResources.shadowVisibility = vbResources.shadowVisibility;
        vbPassResources.shadowLoss = vbResources.shadowLoss;
        vbPassResources.indirectLighting = vbResources.indirectLighting;
        vbPassResources.lightingEnergyBudget = vbResources.lightingEnergyBudget;
        vbPassResources.shadowSourceAttribution = vbResources.shadowSourceAttribution;
        vbPassResources.debugSource = SelectVBGBufferDebugHandle(vbResources, vbDebugView);
        vbPassResources.lightingV3DebugSource = SelectVBFullSceneLightingV3DebugHandle(vbResources, vbDebugView);

        VisibilityBufferGraphPass::GraphContext vbGraphContext{};
        vbGraphContext.resources = vbPassResources;
        vbGraphContext.needsMaterialResolve = needsMaterialResolve;
        vbGraphContext.debugPath = debugPath;
        vbGraphContext.debugVisibility = debugVisibility;
        vbGraphContext.debugDepth = debugDepth;
        vbGraphContext.debugGBuffer = debugGBuffer;
        vbGraphContext.brdfGraphOwned = brdfGraphOwned;
        vbGraphContext.clusterGraphOwned = clusterGraphOwned;
        vbGraphContext.fullSceneLightingV3Enabled = fullSceneLightingV3Enabled;
        vbGraphContext.lightingV3DebugBlit = debugLightingV3;
        VisibilityBufferGraphPass::StageFailureContext vbFailure{
            &vbStageFailed,
            &vbStageFailureStage,
            &vbStageFailureError
        };
        vbGraphContext.graphFailure = vbFailure;
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
            m_constantBuffers.biomeMaterials.gpuAddress;
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
        vbGraphContext.debugBlit.debugExternalSRV = debugLightingV3;
        vbGraphContext.debugBlit.externalSRV =
            SelectVBFullSceneLightingV3DebugSRV(m_mainTargets.lightingV3.descriptors, vbDebugView);
        vbGraphContext.debugBlit.visibilityMode = SelectVBVisibilityDebugMode(vbDebugView);
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
        vbGraphContext.deferredLighting.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.deferredLighting.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.deferredLighting.hdrTarget = m_mainTargets.hdr.resources.color.Get();
        vbGraphContext.deferredLighting.hdrRTV = m_mainTargets.hdr.descriptors.rtv.cpu;
        vbGraphContext.deferredLighting.depthBuffer = m_depthResources.resources.buffer.Get();
        vbGraphContext.deferredLighting.depthSRV = m_depthResources.descriptors.srv;
        vbGraphContext.deferredLighting.envDiffuseResource = deferredInputs.envDiffuseResource;
        vbGraphContext.deferredLighting.envSpecularResource = deferredInputs.envSpecularResource;
        vbGraphContext.deferredLighting.envFormat = deferredInputs.envFormat;
        vbGraphContext.deferredLighting.shadowMapSRV = m_shadowResources.resources.srv;
        vbGraphContext.deferredLighting.params = deferredInputs.params;
        vbGraphContext.deferredLighting.depthState = &m_depthResources.resources.resourceState;
        vbGraphContext.deferredLighting.hdrState = &m_mainTargets.hdr.resources.state;
        vbGraphContext.deferredLighting.shadowState = &m_shadowResources.resources.resourceState;
        vbGraphContext.deferredLighting.rtShadowState = &m_rtShadowTargets.maskState;
        vbGraphContext.deferredLighting.rtGIState = &m_rtGITargets.colorState;
        vbGraphContext.deferredLighting.shadowValid = vbResources.shadow.IsValid();
        vbGraphContext.deferredLighting.rtShadowValid = vbResources.rtShadow.IsValid();
        vbGraphContext.deferredLighting.rtGIValid = vbResources.rtGI.IsValid();
        vbGraphContext.deferredLighting.brdfLutValid = vbResources.brdfLut.IsValid();
        vbGraphContext.deferredLighting.clusterGraphOwned = clusterGraphOwned;
        vbGraphContext.deferredLighting.renderedThisFrame = &m_visibilityBufferState.renderedThisFrame;
        vbGraphContext.deferredLighting.failure = vbFailure;
        vbGraphContext.fullSceneLightingV3.renderer = m_services.visibilityBuffer.get();
        vbGraphContext.fullSceneLightingV3.commandList = m_commandResources.graphicsList.Get();
        vbGraphContext.fullSceneLightingV3.targets.directLighting = m_mainTargets.lightingV3.resources.directLighting.Get();
        vbGraphContext.fullSceneLightingV3.targets.directLightingRTV = m_mainTargets.lightingV3.descriptors.directLightingRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.directLightingUnshadowed =
            m_mainTargets.lightingV3.resources.directLightingUnshadowed.Get();
        vbGraphContext.fullSceneLightingV3.targets.directLightingUnshadowedRTV =
            m_mainTargets.lightingV3.descriptors.directLightingUnshadowedRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.shadowVisibility =
            m_mainTargets.lightingV3.resources.shadowVisibility.Get();
        vbGraphContext.fullSceneLightingV3.targets.shadowVisibilityRTV =
            m_mainTargets.lightingV3.descriptors.shadowVisibilityRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.shadowLoss = m_mainTargets.lightingV3.resources.shadowLoss.Get();
        vbGraphContext.fullSceneLightingV3.targets.shadowLossRTV = m_mainTargets.lightingV3.descriptors.shadowLossRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.indirectLighting =
            m_mainTargets.lightingV3.resources.indirectLighting.Get();
        vbGraphContext.fullSceneLightingV3.targets.indirectLightingRTV =
            m_mainTargets.lightingV3.descriptors.indirectLightingRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.lightingEnergyBudget =
            m_mainTargets.lightingV3.resources.lightingEnergyBudget.Get();
        vbGraphContext.fullSceneLightingV3.targets.lightingEnergyBudgetRTV =
            m_mainTargets.lightingV3.descriptors.lightingEnergyBudgetRTV.cpu;
        vbGraphContext.fullSceneLightingV3.targets.shadowSourceAttribution =
            m_mainTargets.lightingV3.resources.shadowSourceAttribution.Get();
        vbGraphContext.fullSceneLightingV3.targets.shadowSourceAttributionRTV =
            m_mainTargets.lightingV3.descriptors.shadowSourceAttributionRTV.cpu;
        vbGraphContext.fullSceneLightingV3.depthBuffer = m_depthResources.resources.buffer.Get();
        vbGraphContext.fullSceneLightingV3.depthSRV = m_depthResources.descriptors.srv;
        vbGraphContext.fullSceneLightingV3.envDiffuseResource = deferredInputs.envDiffuseResource;
        vbGraphContext.fullSceneLightingV3.envSpecularResource = deferredInputs.envSpecularResource;
        vbGraphContext.fullSceneLightingV3.envFormat = deferredInputs.envFormat;
        vbGraphContext.fullSceneLightingV3.shadowMapSRV = m_shadowResources.resources.srv;
        vbGraphContext.fullSceneLightingV3.params = deferredInputs.params;
        vbGraphContext.fullSceneLightingV3.depthState = &m_depthResources.resources.resourceState;
        vbGraphContext.fullSceneLightingV3.lightingSplitState = &m_mainTargets.lightingV3.resources.state;
        vbGraphContext.fullSceneLightingV3.shadowState = &m_shadowResources.resources.resourceState;
        vbGraphContext.fullSceneLightingV3.rtShadowState = &m_rtShadowTargets.maskState;
        vbGraphContext.fullSceneLightingV3.rtGIState = &m_rtGITargets.colorState;
        vbGraphContext.fullSceneLightingV3.enabled = fullSceneLightingV3Enabled;
        vbGraphContext.fullSceneLightingV3.shadowValid = vbResources.shadow.IsValid();
        vbGraphContext.fullSceneLightingV3.rtShadowValid = vbResources.rtShadow.IsValid();
        vbGraphContext.fullSceneLightingV3.rtGIValid = vbResources.rtGI.IsValid();
        vbGraphContext.fullSceneLightingV3.brdfLutValid = vbResources.brdfLut.IsValid();
        vbGraphContext.fullSceneLightingV3.clusterGraphOwned = clusterGraphOwned;
        vbGraphContext.fullSceneLightingV3.failure = vbFailure;

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
            if (vbResources.directLighting.IsValid()) {
                m_mainTargets.lightingV3.resources.state = m_services.renderGraph->GetResourceState(vbResources.directLighting);
            }

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
            if (fullSceneLightingV3Enabled) {
                RecordFramePass("FullSceneLightingV3", true, true, 1,
                                {"depth", "gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_emissive_metallic",
                                 "gbuffer_material_ext0", "gbuffer_material_ext1", "gbuffer_material_ext2",
                                 "brdf_lut", "cluster_ranges", "cluster_light_indices", "shadow_map"},
                                {"direct_lighting", "direct_lighting_unshadowed", "shadow_visibility",
                                 "shadow_loss", "indirect_lighting", "lighting_energy_budget",
                                 "shadow_source_attribution"},
                                false, nullptr, true);
            }
            if (debugLightingV3) {
                RecordFramePass("FullSceneLightingV3DebugBlit", true, m_visibilityBufferState.renderedThisFrame, 1,
                                {SelectVBFullSceneLightingV3DebugResourceName(vbDebugView)},
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
