#include "Renderer.h"

#include "Passes/BloomGraphPass.h"
#include "Passes/BloomPass.h"
#include "RenderGraph.h"

#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>
#include <span>
#include <string>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kBloomGraphShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::RenderGraphPassResult
Renderer::ExecuteBloomInRenderGraph() {
    RenderGraphPassResult result{};
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_mainTargets.hdr.resources.color ||
        !m_pipelineState.bloomDownsample || !m_pipelineState.bloomBlurH || !m_pipelineState.bloomBlurV ||
        !m_pipelineState.bloomComposite || !m_mainTargets.hdr.descriptors.srv.IsValid() || m_bloomResources.controls.intensity <= 0.0f ||
        !m_bloomResources.resources.texA[0] || !m_bloomResources.resources.texB[0]) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_bloom_prerequisites_missing";
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

    std::array<RGResourceHandle, kBloomLevels> bloomA{};
    std::array<RGResourceHandle, kBloomLevels> bloomB{};
    std::array<ID3D12Resource*, kBloomLevels> bloomATemplates{};
    std::array<ID3D12Resource*, kBloomLevels> bloomBTemplates{};
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        bloomATemplates[level] = m_bloomResources.resources.texA[level].Get();
        bloomBTemplates[level] = m_bloomResources.resources.texB[level].Get();
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.resources.state, "HDR_Bloom");
    const uint32_t baseLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
    const bool useTransientBloom = (std::getenv("CORTEX_DISABLE_BLOOM_TRANSIENTS") == nullptr);
    static bool s_loggedBloomTransientDefault = false;
    if (useTransientBloom && !s_loggedBloomTransientDefault) {
        s_loggedBloomTransientDefault = true;
        spdlog::info("Bloom RG: graph-owned transient intermediates enabled (default)");
    }
    for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
        const bool keepAForPostProcess = !useTransientBloom || (level == baseLevel);
        if (m_bloomResources.resources.texA[level] && keepAForPostProcess) {
            bloomA[level] = m_services.renderGraph->ImportResource(
                m_bloomResources.resources.texA[level].Get(), m_bloomResources.resources.resourceState[level][0], ("BloomA" + std::to_string(level)).c_str());
        }
        if (!useTransientBloom && m_bloomResources.resources.texB[level]) {
            bloomB[level] = m_services.renderGraph->ImportResource(
                m_bloomResources.resources.texB[level].Get(), m_bloomResources.resources.resourceState[level][1], ("BloomB" + std::to_string(level)).c_str());
        }
    }

    BloomGraphPass::StandaloneBloomContext bloomContext{};
    bloomContext.hdr = hdrHandle;
    bloomContext.bloomA = std::span<RGResourceHandle>(bloomA.data(), bloomA.size());
    bloomContext.bloomB = std::span<RGResourceHandle>(bloomB.data(), bloomB.size());
    bloomContext.bloomATemplates = std::span<ID3D12Resource* const>(bloomATemplates.data(), bloomATemplates.size());
    bloomContext.bloomBTemplates = std::span<ID3D12Resource* const>(bloomBTemplates.data(), bloomBTemplates.size());
    bloomContext.targetRtv = m_bloomResources.resources.graphRtv;
    bloomContext.fullscreen.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    bloomContext.fullscreen.commandList = m_commandResources.graphicsList.Get();
    bloomContext.fullscreen.descriptorManager = m_services.descriptorManager.get();
    bloomContext.fullscreen.rootSignature = m_pipelineState.rootSignature.get();
    bloomContext.fullscreen.frameConstants = m_constantBuffers.currentFrameGPU;
    bloomContext.fullscreen.srvTable = m_bloomResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount].data();
    bloomContext.fullscreen.srvTableCount = kBloomDescriptorSlots;
    bloomContext.fullscreen.srvTableValid = m_bloomResources.descriptors.srvTableValid;
    bloomContext.downsamplePipeline = m_pipelineState.bloomDownsample.get();
    bloomContext.blurHPipeline = m_pipelineState.bloomBlurH.get();
    bloomContext.blurVPipeline = m_pipelineState.bloomBlurV.get();
    bloomContext.compositePipeline = m_pipelineState.bloomComposite.get();
    bloomContext.activeLevels = m_bloomResources.resources.activeLevels;
    bloomContext.stageLevels = kBloomLevels;
    bloomContext.baseLevel = baseLevel;
    bloomContext.useTransients = useTransientBloom;
    bloomContext.failStage = failStage;
    bloomContext.hdrResourceState = &m_mainTargets.hdr.resources.state;
    bloomContext.hdrShaderResourceState = kBloomGraphShaderResourceState;
    (void)BloomGraphPass::AddStandaloneBloom(*m_services.renderGraph, bloomContext);

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats(&result);

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
    } else if (stageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "bloom_graph_stage_failed: " + stageError;
    } else {
        m_mainTargets.hdr.resources.state = m_services.renderGraph->GetResourceState(hdrHandle);
        const D3D12_RESOURCE_STATES combinedState = m_services.renderGraph->GetResourceState(bloomA[baseLevel]);
        m_bloomResources.resources.resourceState[baseLevel][0] = combinedState;
        result.executed = true;
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        spdlog::warn("Bloom RG: {} (graph path did not execute)", result.fallbackReason);
    }

    return result;
}


} // namespace Cortex::Graphics
