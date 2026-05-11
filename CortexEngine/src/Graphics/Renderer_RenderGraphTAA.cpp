#include "Renderer.h"

#include "Passes/TAAPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kScreenSpaceShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::RenderGraphPassResult
Renderer::ExecuteTAAInRenderGraph() {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_mainTargets.hdr.resources.color) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_prerequisites_missing";
        return result;
    }

    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_feature_disabled_or_resources_missing";
        return result;
    }

    if (!m_temporalScreenState.historyColor || !m_temporalScreenState.historySRV.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_history_missing";
        return result;
    }
    if (!m_temporalScreenState.taaResolveSrvTableValid) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_resolve_descriptor_table_missing";
        return result;
    }

    bool stageFailed = false;
    const char* stageError = nullptr;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.resources.state, "HDR_TAA");
    const RGResourceHandle historyHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.historyColor.Get(), m_temporalScreenState.historyState, "TAA_History");
    const RGResourceHandle taaIntermediateHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.taaIntermediate.Get(), m_temporalScreenState.taaIntermediateState, "TAA_Intermediate");

    RGResourceHandle velocityHandle{};
    if (m_temporalScreenState.velocityBuffer) {
        velocityHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity_TAA");
    }

    RGResourceHandle depthHandle{};
    if (m_depthResources.resources.buffer) {
        depthHandle = m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_TAA");
    }

    bool usesVBNormal = false;
    RGResourceHandle normalHandle{};
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    ID3D12Resource* normalResource = nullptr;
    D3D12_RESOURCE_STATES normalState = D3D12_RESOURCE_STATE_COMMON;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        usesVBNormal = true;
        vbStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
        normalState = vbStates.normalRoughness;
        normalHandle = m_services.renderGraph->ImportResource(
            normalResource,
            normalState,
            "VB_NormalRoughness_TAA");
    } else if (m_mainTargets.normalRoughness.resources.texture) {
        normalResource = m_mainTargets.normalRoughness.resources.texture.Get();
        normalState = m_mainTargets.normalRoughness.resources.state;
        normalHandle = m_services.renderGraph->ImportResource(
            normalResource,
            normalState,
            "NormalRoughness_TAA");
    }

    const bool seedOnly = !m_temporalHistory.manager.CanReproject(TemporalHistoryId::TAAColor);
    TAAPass::GraphContext taaContext{};
    taaContext.hdr = hdrHandle;
    taaContext.history = historyHandle;
    taaContext.intermediate = taaIntermediateHandle;
    taaContext.velocity = velocityHandle;
    taaContext.depth = depthHandle;
    taaContext.normalRoughness = normalHandle;
    taaContext.seedOnly = seedOnly;
    taaContext.status.failed = &stageFailed;
    taaContext.status.stage = &stageError;
    taaContext.seedHistory.commandList = m_commandResources.graphicsList.Get();
    taaContext.seedHistory.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    taaContext.seedHistory.historyColor = {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState};
    taaContext.seedHistory.skipTransitions = true;
    taaContext.seedHistory.returnHdrAndHistoryToShaderResource = false;
    taaContext.resolveInputs.commandList = m_commandResources.graphicsList.Get();
    taaContext.resolveInputs.taaIntermediate = {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState};
    taaContext.resolveInputs.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    taaContext.resolveInputs.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState};
    taaContext.resolveInputs.normalRoughness = {normalResource, &normalState};
    taaContext.resolveInputs.velocity = {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState};
    taaContext.resolveInputs.historyColor = {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState};
    taaContext.resolveInputs.temporalMask = {m_temporalMaskState.texture.Get(), &m_temporalMaskState.resourceState};
    taaContext.resolveInputs.depthSampleState = kDepthSampleState;
    taaContext.resolveInputs.skipTransitions = true;
    auto& resolveTable = m_temporalScreenState.taaResolveSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    taaContext.resolveDescriptors.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    taaContext.resolveDescriptors.srvTable = std::span<DescriptorHandle>(resolveTable.data(), resolveTable.size());
    taaContext.resolveDescriptors.hdr = m_mainTargets.hdr.resources.color.Get();
    taaContext.resolveDescriptors.bloomIntensity = m_bloomResources.controls.intensity;
    taaContext.resolveDescriptors.bloomOverride = m_bloomResources.resources.postProcessOverride;
    taaContext.resolveDescriptors.bloomFallback = (m_bloomResources.resources.activeLevels > 1)
        ? m_bloomResources.resources.texA[1].Get()
        : m_bloomResources.resources.texA[0].Get();
    taaContext.resolveDescriptors.ssao = m_ssaoResources.resources.texture.Get();
    taaContext.resolveDescriptors.history = m_temporalScreenState.historyColor.Get();
    taaContext.resolveDescriptors.depth = m_depthResources.resources.buffer.Get();
    taaContext.resolveDescriptors.normalRoughness = normalResource;
    taaContext.resolveDescriptors.ssr = m_ssrResources.resources.color.Get();
    taaContext.resolveDescriptors.velocity = m_temporalScreenState.velocityBuffer.Get();
    taaContext.resolveDescriptors.temporalMask = m_temporalMaskState.texture.Get();
    taaContext.resolve.commandList = m_commandResources.graphicsList.Get();
    taaContext.resolve.descriptorManager = m_services.descriptorManager.get();
    taaContext.resolve.rootSignature = m_pipelineState.rootSignature.get();
    taaContext.resolve.frameConstants = m_constantBuffers.currentFrameGPU;
    taaContext.resolve.pipeline = m_pipelineState.taa.get();
    taaContext.resolve.viewportSource = m_mainTargets.hdr.resources.color.Get();
    taaContext.resolve.targetRtv = m_temporalScreenState.taaIntermediateRTV;
    taaContext.resolve.srvTable = std::span<DescriptorHandle>(resolveTable.data(), resolveTable.size());
    taaContext.resolve.shadowAndEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    taaContext.copyToHDR.commandList = m_commandResources.graphicsList.Get();
    taaContext.copyToHDR.taaIntermediate = {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState};
    taaContext.copyToHDR.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    taaContext.copyToHDR.skipTransitions = true;
    taaContext.copyToHistory.commandList = m_commandResources.graphicsList.Get();
    taaContext.copyToHistory.hdrColor = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state};
    taaContext.copyToHistory.historyColor = {m_temporalScreenState.historyColor.Get(), &m_temporalScreenState.historyState};
    taaContext.copyToHistory.taaIntermediate = {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState};
    taaContext.copyToHistory.transitionIntermediateToRenderTarget = true;
    taaContext.copyToHistory.skipTransitions = true;
    taaContext.copyToHistory.returnHdrAndHistoryToShaderResource = false;
    (void)TAAPass::AddToGraph(*m_services.renderGraph, taaContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "taa_graph_stage_failed";
        if (stageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += stageError;
        }
    } else {
        m_mainTargets.hdr.resources.state = m_services.renderGraph->GetResourceState(hdrHandle);
        m_temporalScreenState.historyState = m_services.renderGraph->GetResourceState(historyHandle);
        m_temporalScreenState.taaIntermediateState = m_services.renderGraph->GetResourceState(taaIntermediateHandle);
        if (velocityHandle.IsValid()) {
            m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        }
        if (depthHandle.IsValid()) {
            m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        }
        if (normalHandle.IsValid()) {
            if (usesVBNormal) {
                auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
                finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
                m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
            } else {
                m_mainTargets.normalRoughness.resources.state = m_services.renderGraph->GetResourceState(normalHandle);
            }
        }
        MarkTAAHistoryValid();
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("TAA RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}


} // namespace Cortex::Graphics
