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
        !m_depthResources.resources.buffer || !m_temporalScreenState.velocityBuffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_temporal_mask_prerequisites_missing";
        return result;
    }

    bool usesVBNormal = false;
    DescriptorHandle normalSRV = m_mainTargets.normalRoughness.descriptors.srv;
    ID3D12Resource* normalResource = m_mainTargets.normalRoughness.resources.texture.Get();
    D3D12_RESOURCE_STATES normalState = m_mainTargets.normalRoughness.resources.state;
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        const DescriptorHandle& vbNormalSRV = m_services.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormalSRV.IsValid()) {
            usesVBNormal = true;
            normalSRV = vbNormalSRV;
            vbStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
            normalState = vbStates.normalRoughness;
        }
    }
    if (!normalSRV.IsValid() || !normalResource) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_temporal_mask_normal_missing";
        return result;
    }
    D3D12_RESOURCE_STATES vbNormalDispatchState = normalState;
    D3D12_RESOURCE_STATES* normalDispatchState = usesVBNormal
        ? &vbNormalDispatchState
        : &m_mainTargets.normalRoughness.resources.state;

    uint32_t width = 0;
    uint32_t height = 0;
    const D3D12_RESOURCE_DESC maskDesc = m_temporalMaskState.texture->GetDesc();
    if (maskDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        width = static_cast<uint32_t>(maskDesc.Width);
        height = maskDesc.Height;
    }

    bool stageFailed = false;
    std::string stageError;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_TemporalMask");
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
    graphContext.dispatch.pass = m_services.temporalRejectionMask.get();
    graphContext.dispatch.device = m_services.device->GetDevice();
    graphContext.dispatch.descriptorManager = m_services.descriptorManager.get();
    graphContext.dispatch.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState};
    graphContext.dispatch.normalRoughness = {normalResource, normalDispatchState};
    graphContext.dispatch.velocity = {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState};
    graphContext.dispatch.output = {m_temporalMaskState.texture.Get(), &m_temporalMaskState.resourceState};
    graphContext.dispatch.depthSampleState = kDepthSampleState;
    graphContext.dispatch.shaderResourceState = kScreenSpaceShaderResourceState;
    graphContext.dispatch.skipTransitions = true;
    graphContext.dispatch.dispatch.width = width;
    graphContext.dispatch.dispatch.height = height;
    graphContext.dispatch.dispatch.frameConstants = m_constantBuffers.currentFrameGPU;
    graphContext.dispatch.dispatch.depthSRV = m_depthResources.descriptors.srv;
    graphContext.dispatch.dispatch.normalRoughnessSRV = normalSRV;
    graphContext.dispatch.dispatch.velocitySRV = m_temporalScreenState.velocitySRV;
    graphContext.dispatch.dispatch.outputUAV = m_temporalMaskState.uav;
    graphContext.dispatch.dispatch.depthResource = m_depthResources.resources.buffer.Get();
    graphContext.dispatch.dispatch.normalRoughnessResource = normalResource;
    graphContext.dispatch.dispatch.velocityResource = m_temporalScreenState.velocityBuffer.Get();
    graphContext.dispatch.dispatch.outputResource = m_temporalMaskState.texture.Get();
    graphContext.dispatch.dispatch.srvTable = m_temporalMaskState.srvTables[m_frameRuntime.frameIndex % kFrameCount][0];
    graphContext.dispatch.dispatch.uavTable = m_temporalMaskState.uavTables[m_frameRuntime.frameIndex % kFrameCount][0];
    graphContext.dispatch.builtThisFrame = &m_temporalMaskState.builtThisFrame;
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
        m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        m_temporalMaskState.resourceState = m_services.renderGraph->GetResourceState(maskHandle);
        if (usesVBNormal) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        } else {
            m_mainTargets.normalRoughness.resources.state = m_services.renderGraph->GetResourceState(normalHandle);
        }
        result.executed = true;
        RecordFramePass("TemporalRejectionMask",
                        true,
                        true,
                        1,
                        {"depth", frameNormalRoughnessResource ? frameNormalRoughnessResource : "normal_roughness", "velocity"},
                        {"temporal_rejection_mask"},
                        false,
                        nullptr,
                        true);
        CaptureTemporalRejectionMaskStats();
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("TemporalRejectionMask RG: {} (graph path did not execute)",
                     result.fallbackReason);
    }

    return result;
}


} // namespace Cortex::Graphics
