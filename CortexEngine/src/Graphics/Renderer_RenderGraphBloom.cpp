#include "Renderer.h"

#include "Passes/BloomGraphPass.h"
#include "Passes/BloomPass.h"
#include "Passes/RenderPassScope.h"
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
    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_mainTargets.hdrColor ||
        !m_pipelineState.bloomDownsample || !m_pipelineState.bloomBlurH || !m_pipelineState.bloomBlurV ||
        !m_pipelineState.bloomComposite || !m_mainTargets.hdrSRV.IsValid() || m_bloomResources.controls.intensity <= 0.0f ||
        !m_bloomResources.resources.texA[0] || !m_bloomResources.resources.texB[0]) {
        result.fallbackUsed = true;
        result.fallbackReason = "render_graph_bloom_prerequisites_missing";
        RenderBloom();
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

    std::array<RGResourceHandle, kBloomLevels> bloomA{};
    std::array<RGResourceHandle, kBloomLevels> bloomB{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomA{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomB{};
    std::array<std::array<D3D12_RESOURCE_STATES, 2>, kBloomLevels> savedBloomStates{};
    std::array<std::array<DescriptorHandle, 2>, kBloomLevels> savedBloomRTV{};
    std::array<std::array<DescriptorHandle, 2>, kBloomLevels> savedBloomSRV{};
    for (uint32_t level = 0; level < kBloomLevels; ++level) {
        savedBloomA[level] = m_bloomResources.resources.texA[level];
        savedBloomB[level] = m_bloomResources.resources.texB[level];
        savedBloomStates[level][0] = m_bloomResources.resources.resourceState[level][0];
        savedBloomStates[level][1] = m_bloomResources.resources.resourceState[level][1];
        savedBloomRTV[level][0] = m_bloomResources.resources.rtv[level][0];
        savedBloomRTV[level][1] = m_bloomResources.resources.rtv[level][1];
        savedBloomSRV[level][0] = m_bloomResources.resources.srv[level][0];
        savedBloomSRV[level][1] = m_bloomResources.resources.srv[level][1];
    }

    m_services.renderGraph->BeginFrame();
    const RGResourceHandle hdrHandle =
        m_services.renderGraph->ImportResource(m_mainTargets.hdrColor.Get(), m_mainTargets.hdrState, "HDR_Bloom");
    const uint32_t baseLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
    const bool useTransientBloom = (std::getenv("CORTEX_DISABLE_BLOOM_TRANSIENTS") == nullptr);
    static bool s_loggedBloomTransientDefault = false;
    if (useTransientBloom && !s_loggedBloomTransientDefault) {
        s_loggedBloomTransientDefault = true;
        spdlog::info("Bloom RG: graph-owned transient intermediates enabled (default)");
    }
    auto bloomPingIsTransient = [&](uint32_t level, uint32_t ping) {
        if (!useTransientBloom) {
            return false;
        }
        if (ping == 0 && level == baseLevel) {
            return false;
        }
        return true;
    };

    if (useTransientBloom) {
        for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
            for (uint32_t ping = 0; ping < 2u; ++ping) {
                if (!bloomPingIsTransient(level, ping)) {
                    continue;
                }
                if (!m_bloomResources.resources.graphRtv[level][ping].IsValid()) {
                    auto rtvResult = m_services.descriptorManager->AllocateRTV();
                    if (rtvResult.IsErr()) {
                        result.fallbackUsed = true;
                        result.fallbackReason = "bloom_graph_rtv_allocation_failed: " + rtvResult.Error();
                        RenderBloom();
                        result.executed = true;
                        return result;
                    }
                    m_bloomResources.resources.graphRtv[level][ping] = rtvResult.Value();
                }
                if (!m_bloomResources.resources.graphSrv[level][ping].IsValid()) {
                    auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
                    if (srvResult.IsErr()) {
                        result.fallbackUsed = true;
                        result.fallbackReason = "bloom_graph_srv_allocation_failed: " + srvResult.Error();
                        RenderBloom();
                        result.executed = true;
                        return result;
                    }
                    m_bloomResources.resources.graphSrv[level][ping] = srvResult.Value();
                }
                m_bloomResources.resources.rtv[level][ping] = m_bloomResources.resources.graphRtv[level][ping];
                m_bloomResources.resources.srv[level][ping] = m_bloomResources.resources.graphSrv[level][ping];
            }
        }
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

    bool bloomTransientsDeclared = false;
    auto declareBloomTransients = [&](RGPassBuilder& builder) {
        if (bloomTransientsDeclared) {
            return;
        }
        bloomTransientsDeclared = true;
        if (!useTransientBloom) {
            return;
        }
        for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
            if (!bloomA[level].IsValid() && savedBloomA[level]) {
                bloomA[level] = builder.CreateTransient(
                    BloomPass::MakeTextureDesc(savedBloomA[level].Get(), "BloomA_Transient" + std::to_string(level)));
            }
            if (!bloomB[level].IsValid() && savedBloomB[level]) {
                bloomB[level] = builder.CreateTransient(
                    BloomPass::MakeTextureDesc(savedBloomB[level].Get(), "BloomB_Transient" + std::to_string(level)));
            }
        }
    };

    auto rewriteBloomViews = [&](ID3D12Resource* resource, uint32_t level, uint32_t ping) -> bool {
        if (!resource || level >= kBloomLevels || ping >= 2u || !m_services.device || !m_services.device->GetDevice()) {
            return false;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_services.device->GetDevice()->CreateRenderTargetView(resource, &rtvDesc, m_bloomResources.resources.rtv[level][ping].cpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_services.device->GetDevice()->CreateShaderResourceView(resource, &srvDesc, m_bloomResources.resources.srv[level][ping].cpu);

        if (ping == 0) {
            m_bloomResources.resources.texA[level] = resource;
        } else {
            m_bloomResources.resources.texB[level] = resource;
        }
        return true;
    };

    auto bindBloomGraphResources = [&](const RenderGraph& graph) -> bool {
        for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
            if (bloomA[level].IsValid()) {
                if (!rewriteBloomViews(graph.GetResource(bloomA[level]), level, 0)) {
                    return false;
                }
            }
            if (bloomB[level].IsValid()) {
                if (!rewriteBloomViews(graph.GetResource(bloomB[level]), level, 1)) {
                    return false;
                }
            }
        }
        return true;
    };

    auto restoreBloomResources = [&]() {
        for (uint32_t level = 0; level < kBloomLevels; ++level) {
            m_bloomResources.resources.texA[level] = savedBloomA[level];
            m_bloomResources.resources.texB[level] = savedBloomB[level];
            m_bloomResources.resources.resourceState[level][0] = savedBloomStates[level][0];
            m_bloomResources.resources.resourceState[level][1] = savedBloomStates[level][1];
            m_bloomResources.resources.rtv[level][0] = savedBloomRTV[level][0];
            m_bloomResources.resources.rtv[level][1] = savedBloomRTV[level][1];
            m_bloomResources.resources.srv[level][0] = savedBloomSRV[level][0];
            m_bloomResources.resources.srv[level][1] = savedBloomSRV[level][1];
            if (savedBloomA[level]) {
                (void)rewriteBloomViews(savedBloomA[level].Get(), level, 0);
            }
            if (savedBloomB[level]) {
                (void)rewriteBloomViews(savedBloomB[level].Get(), level, 1);
            }
        }
        const uint32_t combinedLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
        m_bloomResources.resources.combinedSrv = m_bloomResources.resources.srv[combinedLevel][0];
    };

    BloomGraphPass::StandaloneBloomContext bloomContext{};
    bloomContext.hdr = hdrHandle;
    bloomContext.bloomA = std::span<RGResourceHandle>(bloomA.data(), bloomA.size());
    bloomContext.bloomB = std::span<RGResourceHandle>(bloomB.data(), bloomB.size());
    bloomContext.activeLevels = m_bloomResources.resources.activeLevels;
    bloomContext.baseLevel = baseLevel;
    bloomContext.declareTransients = declareBloomTransients;
    bloomContext.failStage = failStage;
    bloomContext.renderDownsampleBase = [&](const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_downsample_base");
            return false;
        }
        m_mainTargets.hdrState = kBloomGraphShaderResourceState;
        m_bloomResources.resources.resourceState[0][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, true);
        return PrepareBloomPassState() && RenderBloomDownsampleBase(true);
    };
    bloomContext.renderDownsampleLevel = [&](uint32_t level, const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_downsample_chain");
            return false;
        }
        m_bloomResources.resources.resourceState[level - 1][0] = kBloomGraphShaderResourceState;
        m_bloomResources.resources.resourceState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, true);
        return PrepareBloomPassState() && RenderBloomDownsampleLevel(level, true);
    };
    bloomContext.renderBlurHorizontal = [&](uint32_t level, const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_blur_horizontal");
            return false;
        }
        m_bloomResources.resources.resourceState[level][0] = kBloomGraphShaderResourceState;
        m_bloomResources.resources.resourceState[level][1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, true);
        return PrepareBloomPassState() && RenderBloomBlurHorizontal(level, true);
    };
    bloomContext.renderBlurVertical = [&](uint32_t level, const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_blur_vertical");
            return false;
        }
        m_bloomResources.resources.resourceState[level][1] = kBloomGraphShaderResourceState;
        m_bloomResources.resources.resourceState[level][0] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, true);
        return PrepareBloomPassState() && RenderBloomBlurVertical(level, true);
    };
    bloomContext.renderComposite = [&](const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_composite");
            return false;
        }
        for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
            if (bloomA[level].IsValid()) {
                m_bloomResources.resources.resourceState[level][0] = kBloomGraphShaderResourceState;
            }
        }
        m_bloomResources.resources.resourceState[baseLevel][1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, true);
        return PrepareBloomPassState() && RenderBloomComposite(true);
    };
    bloomContext.copyCombined = [&](const RenderGraph& graph) {
        if (!bindBloomGraphResources(graph)) {
            failStage("bind_copy_combined");
            return false;
        }
        m_bloomResources.resources.resourceState[baseLevel][1] = D3D12_RESOURCE_STATE_COPY_SOURCE;
        m_bloomResources.resources.resourceState[baseLevel][0] = D3D12_RESOURCE_STATE_COPY_DEST;
        return CopyBloomCompositeToCombined(true);
    };
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
        m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(hdrHandle);
        const D3D12_RESOURCE_STATES combinedState = m_services.renderGraph->GetResourceState(bloomA[baseLevel]);
        restoreBloomResources();
        m_bloomResources.resources.resourceState[baseLevel][0] = combinedState;
        result.executed = true;
    }
    if (result.fallbackUsed) {
        restoreBloomResources();
    }
    m_services.renderGraph->EndFrame();

    if (result.fallbackUsed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        spdlog::warn("Bloom RG: {} (falling back to legacy path)", result.fallbackReason);
        ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.bloomSkipTransitions, false);
        RenderBloom();
        result.executed = true;
    }

    return result;
}


} // namespace Cortex::Graphics
