#include "Renderer.h"

#include "Passes/RenderPassScope.h"
#include "Passes/SSAOPass.h"
#include "RenderGraph.h"
#include "RendererGeometryUtils.h"

#include <spdlog/spdlog.h>

#include <span>

namespace Cortex::Graphics {

Renderer::RenderGraphPassResult
Renderer::ExecuteSSAOInRenderGraph() {
    RenderGraphPassResult result{};
    const bool useComputeSSAO = m_pipelineState.ssaoCompute && m_frameRuntime.asyncComputeSupported;

    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_ssaoResources.controls.enabled || !m_ssaoResources.resources.texture ||
        !m_depthResources.resources.buffer || !m_depthResources.descriptors.srv.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssao_prerequisites_missing";
        return result;
    }

    if (useComputeSSAO && !m_ssaoResources.resources.uav.IsValid()) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssao_uav_missing";
        return result;
    }
    if (!m_ssaoResources.descriptors.descriptorTablesValid) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_ssao_descriptor_tables_missing";
        return result;
    }

    bool stageFailed = false;
    const char* stageError = nullptr;

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle depthHandle =
        m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_SSAO");
    const RGResourceHandle ssaoHandle =
        m_services.renderGraph->ImportResource(m_ssaoResources.resources.texture.Get(), m_ssaoResources.resources.resourceState, "SSAO");

    SSAOPass::GraphContext ssaoContext{};
    ssaoContext.depth = depthHandle;
    ssaoContext.ssao = ssaoHandle;
    ssaoContext.useCompute = useComputeSSAO;
    ssaoContext.status.failed = &stageFailed;
    ssaoContext.status.stage = &stageError;
    ssaoContext.prepare.commandList = m_commandResources.graphicsList.Get();
    ssaoContext.prepare.skipTransitions = true;
    ssaoContext.prepare.depth = {
        m_depthResources.resources.buffer.Get(),
        &m_depthResources.resources.resourceState,
        kDepthSampleState,
    };
    ssaoContext.prepare.target = {
        m_ssaoResources.resources.texture.Get(),
        &m_ssaoResources.resources.resourceState,
        useComputeSSAO ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET,
    };
    auto& ssaoSrvTable = m_ssaoResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];
    if (useComputeSSAO) {
        const bool compactRoot = m_pipelineState.singleSrvUavComputeRootSignature != nullptr;
        ID3D12RootSignature* ssaoRootSignature =
            compactRoot ? m_pipelineState.singleSrvUavComputeRootSignature.Get() : m_pipelineState.computeRootSignature->GetRootSignature();
        const uint32_t srvTableSize = compactRoot ? 1u : 10u;
        const uint32_t uavTableSize = compactRoot ? 1u : 4u;
        auto& ssaoUavTable = m_ssaoResources.descriptors.uavTables[m_frameRuntime.frameIndex % kFrameCount];
        ssaoContext.compute.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        ssaoContext.compute.commandList = m_commandResources.graphicsList.Get();
        ssaoContext.compute.descriptorManager = m_services.descriptorManager.get();
        ssaoContext.compute.rootSignature = ssaoRootSignature;
        ssaoContext.compute.frameConstants = m_constantBuffers.currentFrameGPU;
        ssaoContext.compute.pipeline = m_pipelineState.ssaoCompute.get();
        ssaoContext.compute.frameConstantsRoot = compactRoot ? 0u : 1u;
        ssaoContext.compute.srvTableRoot = compactRoot ? 1u : 3u;
        ssaoContext.compute.uavTableRoot = compactRoot ? 2u : 6u;
        ssaoContext.compute.target = m_ssaoResources.resources.texture.Get();
        ssaoContext.compute.depth = m_depthResources.resources.buffer.Get();
        ssaoContext.compute.srvTable = std::span<DescriptorHandle>(ssaoSrvTable.data(), srvTableSize);
        ssaoContext.compute.uavTable = std::span<DescriptorHandle>(ssaoUavTable.data(), uavTableSize);
    } else {
        ssaoContext.graphics.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        ssaoContext.graphics.commandList = m_commandResources.graphicsList.Get();
        ssaoContext.graphics.descriptorManager = m_services.descriptorManager.get();
        ssaoContext.graphics.rootSignature = m_pipelineState.rootSignature.get();
        ssaoContext.graphics.frameConstants = m_constantBuffers.currentFrameGPU;
        ssaoContext.graphics.pipeline = m_pipelineState.ssao.get();
        ssaoContext.graphics.target = m_ssaoResources.resources.texture.Get();
        ssaoContext.graphics.targetRtv = m_ssaoResources.resources.rtv;
        ssaoContext.graphics.depth = m_depthResources.resources.buffer.Get();
        ssaoContext.graphics.srvTable = std::span<DescriptorHandle>(ssaoSrvTable.data(), ssaoSrvTable.size());
    }
    (void)SSAOPass::AddToGraph(*m_services.renderGraph, ssaoContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "ssao_graph_stage_failed";
        if (stageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += stageError;
        }
    } else {
        m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_ssaoResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssaoHandle);
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("SSAO RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}



} // namespace Cortex::Graphics
