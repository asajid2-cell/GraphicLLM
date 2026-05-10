#include "Renderer.h"

#include "Passes/TAAPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <string>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kScreenSpaceShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::RenderGraphPassResult
Renderer::ExecuteTAAInRenderGraph() {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_mainTargets.hdrColor) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_prerequisites_missing";
        RenderTAA();
        result.executed = true;
        return result;
    }

    if (!m_temporalAAState.enabled || !m_pipelineState.taa || !m_temporalScreenState.taaIntermediate || !m_services.window) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_feature_disabled_or_resources_missing";
        RenderTAA();
        result.executed = true;
        return result;
    }

    if (!m_temporalScreenState.historyColor || !m_temporalScreenState.historySRV.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_taa_history_missing";
        RenderTAA();
        result.executed = true;
        return result;
    }

    bool stageFailed = false;
    std::string stageError;
    auto failStage = [&](const char* stage) {
        if (!stageFailed) {
            stageError = stage;
        }
        stageFailed = true;
    };

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdrColor.Get(), m_mainTargets.hdrState, "HDR_TAA");
    const RGResourceHandle historyHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.historyColor.Get(), m_temporalScreenState.historyState, "TAA_History");
    const RGResourceHandle taaIntermediateHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.taaIntermediate.Get(), m_temporalScreenState.taaIntermediateState, "TAA_Intermediate");

    RGResourceHandle velocityHandle{};
    if (m_temporalScreenState.velocityBuffer) {
        velocityHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity_TAA");
    }

    RGResourceHandle depthHandle{};
    if (m_depthResources.buffer) {
        depthHandle = m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_TAA");
    }

    bool usesVBNormal = false;
    RGResourceHandle normalHandle{};
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        usesVBNormal = true;
        vbStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        normalHandle = m_services.renderGraph->ImportResource(
            m_services.visibilityBuffer->GetNormalRoughnessBuffer(),
            vbStates.normalRoughness,
            "VB_NormalRoughness_TAA");
    } else if (m_mainTargets.gbufferNormalRoughness) {
        normalHandle = m_services.renderGraph->ImportResource(
            m_mainTargets.gbufferNormalRoughness.Get(),
            m_mainTargets.gbufferNormalRoughnessState,
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
    taaContext.failStage = failStage;
    taaContext.seedHistory = [&]() {
        m_mainTargets.hdrState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
        return SeedTAAHistory(true);
    };
    taaContext.resolve = [&]() {
        m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_mainTargets.hdrState = kScreenSpaceShaderResourceState;
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (velocityHandle.IsValid()) {
            m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        if (depthHandle.IsValid()) {
            m_depthResources.resourceState = kDepthSampleState;
        }
        if (normalHandle.IsValid()) {
            if (usesVBNormal) {
                auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
                states.normalRoughness = kScreenSpaceShaderResourceState;
                m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);
            } else {
                m_mainTargets.gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            }
        }
        return ResolveTAAIntermediate(true);
    };
    taaContext.copyToHDR = [&]() {
        m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_mainTargets.hdrState = D3D12_RESOURCE_STATE_COPY_DEST;
        return CopyTAAIntermediateToHDR(true);
    };
    taaContext.copyToHistory = [&]() {
        m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_mainTargets.hdrState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
        return CopyHDRToTAAHistory(true);
    };
    (void)TAAPass::AddToGraph(*m_services.renderGraph, taaContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "taa_graph_stage_failed: " + stageError;
    } else {
        m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(hdrHandle);
        m_temporalScreenState.historyState = m_services.renderGraph->GetResourceState(historyHandle);
        m_temporalScreenState.taaIntermediateState = m_services.renderGraph->GetResourceState(taaIntermediateHandle);
        if (velocityHandle.IsValid()) {
            m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        }
        if (depthHandle.IsValid()) {
            m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        }
        if (normalHandle.IsValid()) {
            if (usesVBNormal) {
                auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
                finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
                m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
            } else {
                m_mainTargets.gbufferNormalRoughnessState = m_services.renderGraph->GetResourceState(normalHandle);
            }
        }
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("TAA RG: {} (falling back to legacy path)", result.fallbackReason);
        RenderTAA();
        result.executed = true;
    }

    return result;
}


} // namespace Cortex::Graphics
