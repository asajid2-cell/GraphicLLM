#include "Renderer.h"

#include "Passes/RenderPassScope.h"
#include "Passes/SSRPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <span>
#include <string>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kScreenSpaceShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::RenderGraphPassResult
Renderer::ExecuteSSRInRenderGraph() {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_pipelineState.ssr || !m_ssrResources.resources.color ||
        !m_mainTargets.hdr.resources.color || !m_depthResources.resources.buffer) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssr_prerequisites_missing";
        return result;
    }

    ID3D12Resource* normalResource = m_mainTargets.normalRoughness.resources.texture.Get();
    D3D12_RESOURCE_STATES normalState = m_mainTargets.normalRoughness.resources.state;
    const bool usesVBNormal =
        m_visibilityBufferState.renderedThisFrame &&
        m_services.visibilityBuffer &&
        m_services.visibilityBuffer->GetNormalRoughnessBuffer();

    VisibilityBufferRenderer::ResourceStateSnapshot vbInitialStates{};
    if (usesVBNormal) {
        vbInitialStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
        normalState = vbInitialStates.normalRoughness;
    }

    if (!normalResource) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssr_normal_resource_missing";
        return result;
    }
    if (!m_ssrResources.descriptors.srvTableValid) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssr_descriptor_table_missing";
        return result;
    }

    bool stageFailed = false;
    std::string stageError;
    auto failStage = [&](const char* stage) {
        if (!stageFailed) {
            stageError = stage ? stage : "ssr_graph_stage_failed";
        }
        stageFailed = true;
    };

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.resources.state, "HDR_SSR");
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_SSR");
    const RGResourceHandle normalHandle =
        m_services.renderGraph->ImportResource(normalResource, normalState, usesVBNormal ? "VB_NormalRoughness_SSR" : "NormalRoughness_SSR");
    const RGResourceHandle ssrHandle =
        m_services.renderGraph->ImportResource(m_ssrResources.resources.color.Get(), m_ssrResources.resources.resourceState, "SSRColor");

    SSRPass::GraphContext ssrContext{};
    ssrContext.hdr = hdrHandle;
    ssrContext.depth = depthHandle;
    ssrContext.normalRoughness = normalHandle;
    ssrContext.ssr = ssrHandle;
    ssrContext.failStage = failStage;
    ssrContext.prepare.commandList = m_commandResources.graphicsList.Get();
    ssrContext.prepare.skipTransitions = true;
    ssrContext.prepare.ssrTarget = {
        m_ssrResources.resources.color.Get(),
        &m_ssrResources.resources.resourceState,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
    };
    ssrContext.prepare.hdr = {
        m_mainTargets.hdr.resources.color.Get(),
        &m_mainTargets.hdr.resources.state,
        kScreenSpaceShaderResourceState,
    };
    ssrContext.prepare.normalRoughness = {
        normalResource,
        &normalState,
        kScreenSpaceShaderResourceState,
    };
    ssrContext.prepare.depth = {
        m_depthResources.resources.buffer.Get(),
        &m_depthResources.resources.resourceState,
        kDepthSampleState,
    };
    auto& ssrTable = m_ssrResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];
    ssrContext.draw.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ssrContext.draw.commandList = m_commandResources.graphicsList.Get();
    ssrContext.draw.descriptorManager = m_services.descriptorManager.get();
    ssrContext.draw.rootSignature = m_pipelineState.rootSignature.get();
    ssrContext.draw.frameConstants = m_constantBuffers.currentFrameGPU;
    ssrContext.draw.pipeline = m_pipelineState.ssr.get();
    ssrContext.draw.target = m_ssrResources.resources.color.Get();
    ssrContext.draw.targetRtv = m_ssrResources.resources.rtv;
    ssrContext.draw.hdr = m_mainTargets.hdr.resources.color.Get();
    ssrContext.draw.depth = m_depthResources.resources.buffer.Get();
    ssrContext.draw.normalRoughness = normalResource;
    ssrContext.draw.srvTable = std::span<DescriptorHandle>(ssrTable.data(), ssrTable.size());
    ssrContext.draw.shadowAndEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    (void)SSRPass::AddToGraph(*m_services.renderGraph, ssrContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "ssr_graph_stage_failed: " + stageError;
    } else {
        m_mainTargets.hdr.resources.state = m_services.renderGraph->GetResourceState(hdrHandle);
        m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_ssrResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssrHandle);
        if (usesVBNormal) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        } else {
            m_mainTargets.normalRoughness.resources.state = m_services.renderGraph->GetResourceState(normalHandle);
        }
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("SSR RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}


} // namespace Cortex::Graphics
