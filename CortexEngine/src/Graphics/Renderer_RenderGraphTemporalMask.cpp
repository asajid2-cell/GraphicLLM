#include "Renderer.h"

#include "TemporalRejectionMask.h"
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
Renderer::ExecuteTemporalRejectionMaskInRenderGraph(const char* frameNormalRoughnessResource) {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_temporalMaskState.texture ||
        !m_depthResources.buffer || !m_temporalScreenState.velocityBuffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_temporal_mask_prerequisites_missing";
        BuildTemporalRejectionMask(frameNormalRoughnessResource);
        result.executed = m_temporalMaskState.builtThisFrame;
        return result;
    }

    bool usesVBNormal = false;
    ID3D12Resource* normalResource = m_mainTargets.gbufferNormalRoughness.Get();
    D3D12_RESOURCE_STATES normalState = m_mainTargets.gbufferNormalRoughnessState;
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        usesVBNormal = true;
        vbStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
        normalState = vbStates.normalRoughness;
    }
    if (!normalResource) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_temporal_mask_normal_missing";
        BuildTemporalRejectionMask(frameNormalRoughnessResource);
        result.executed = m_temporalMaskState.builtThisFrame;
        return result;
    }

    bool stageFailed = false;
    std::string stageError;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_TemporalMask");
    const RGResourceHandle normalHandle =
        m_services.renderGraph->ImportResource(normalResource, normalState, "NormalRoughness_TemporalMask");
    const RGResourceHandle velocityHandle =
        m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity_TemporalMask");
    const RGResourceHandle maskHandle =
        m_services.renderGraph->ImportResource(m_temporalMaskState.texture.Get(),
                                      m_temporalMaskState.resourceState,
                                      "TemporalRejectionMask");

    TemporalRejectionMask::GraphContext graphContext{};
    graphContext.depth = depthHandle;
    graphContext.normalRoughness = normalHandle;
    graphContext.velocity = velocityHandle;
    graphContext.mask = maskHandle;
    graphContext.dispatch = [&]() {
        m_depthResources.resourceState = kDepthSampleState;
        m_temporalScreenState.velocityState = kScreenSpaceShaderResourceState;
        m_temporalMaskState.resourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (usesVBNormal) {
            auto states = m_services.visibilityBuffer->GetResourceStateSnapshot();
            states.normalRoughness = kScreenSpaceShaderResourceState;
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(states);
        } else {
            m_mainTargets.gbufferNormalRoughnessState = kScreenSpaceShaderResourceState;
        }

        BuildTemporalRejectionMask(frameNormalRoughnessResource, true, true);
        return m_temporalMaskState.builtThisFrame;
    };
    graphContext.failStage = [&](const char* stage) {
        stageFailed = true;
        stageError = stage ? stage : "unknown";
    };

    const RGResourceHandle temporalMaskResult = TemporalRejectionMask::AddToGraph(*m_services.renderGraph, graphContext);
    if (!temporalMaskResult.IsValid()) {
        stageFailed = true;
        if (stageError.empty()) {
            stageError = "temporal_mask_graph_contract";
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "temporal_mask_graph_stage_failed";
        if (!stageError.empty()) {
            result.fallbackReason += ": " + stageError;
        }
    } else {
        m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        m_temporalMaskState.resourceState = m_services.renderGraph->GetResourceState(maskHandle);
        if (usesVBNormal) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        } else {
            m_mainTargets.gbufferNormalRoughnessState = m_services.renderGraph->GetResourceState(normalHandle);
        }
        result.executed = true;
        CaptureTemporalRejectionMaskStats();
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("TemporalRejectionMask RG: {} (falling back to legacy barriers)",
                     result.fallbackReason);
        BuildTemporalRejectionMask(frameNormalRoughnessResource);
        result.executed = m_temporalMaskState.builtThisFrame;
    }

    return result;
}


} // namespace Cortex::Graphics
