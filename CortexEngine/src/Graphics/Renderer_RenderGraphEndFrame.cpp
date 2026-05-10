#include "Renderer.h"

#include "Debug/GPUProfiler.h"
#include "Passes/BloomGraphPass.h"
#include "Passes/BloomPass.h"
#include "Passes/PostProcessGraphPass.h"
#include "Passes/RenderPassScope.h"
#include "RenderGraph.h"

#include <cstdlib>
#include <glm/geometric.hpp>
#include <utility>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kRenderGraphShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

} // namespace

Renderer::EndFrameGraphResult
Renderer::ExecuteEndFrameInRenderGraph(const EndFrameGraphInputs& inputs) {
    EndFrameGraphResult result{};

    const bool canRunRg = (m_services.renderGraph && m_services.device && m_commandResources.graphicsList && m_services.descriptorManager);
    const bool wantsRgHzbThisFrame =
        inputs.hzbPending && inputs.useRenderGraphHZB && canRunRg && m_depthResources.buffer &&
        m_depthResources.srv.IsValid() && m_hzbResources.texture;
    const bool wantsRgPostThisFrame =
        inputs.runPostProcess && inputs.useRenderGraphPost && canRunRg && m_pipelineState.postProcess &&
        m_mainTargets.hdrColor && m_services.window && m_services.window->GetCurrentBackBuffer();
    bool wantsFusedBloomThisFrame =
        inputs.runBloom && wantsRgPostThisFrame &&
        m_pipelineState.bloomDownsample && m_pipelineState.bloomBlurH && m_pipelineState.bloomBlurV &&
        m_pipelineState.bloomComposite && m_mainTargets.hdrSRV.IsValid() && m_bloomResources.intensity > 0.0f &&
        m_bloomResources.texA[0] && m_bloomResources.texB[0];
    const bool useFusedBloomTransients =
        wantsFusedBloomThisFrame &&
        std::getenv("CORTEX_DISABLE_BLOOM_TRANSIENTS") == nullptr;

    if (!wantsRgHzbThisFrame && !wantsRgPostThisFrame) {
        return result;
    }

    result.attempted = true;
    result.attemptedBloom = wantsFusedBloomThisFrame;
    Debug::GPUProfiler::Get().BeginScope(m_commandResources.graphicsList.Get(), "RenderGraphEndFrame", "RenderGraph");
    m_services.renderGraph->BeginFrame();

    RGResourceHandle depthHandle{};
    RGResourceHandle hzbHandle{};
    if (wantsRgHzbThisFrame) {
        depthHandle = m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth");
        hzbHandle = m_services.renderGraph->ImportResource(m_hzbResources.texture.Get(), m_hzbResources.resourceState, "HZB");
        AddHZBFromDepthPasses_RG(*m_services.renderGraph, depthHandle, hzbHandle);
    }

    RGResourceHandle hdrHandle{};
    RGResourceHandle ssaoHandle{};
    RGResourceHandle ssrHandle{};
    RGResourceHandle bloomHandle{};
    RGResourceHandle historyHandle{};
    RGResourceHandle depthPpHandle{};
    RGResourceHandle normalHandle{};
    RGResourceHandle emissiveMetallicHandle{};
    RGResourceHandle materialExt1Handle{};
    RGResourceHandle materialExt2Handle{};
    RGResourceHandle velocityHandle{};
    RGResourceHandle taaHandle{};
    RGResourceHandle rtReflHandle{};
    RGResourceHandle rtReflHistHandle{};
    RGResourceHandle backBufferHandle{};
    std::array<RGResourceHandle, kBloomLevels> bloomA{};
    std::array<RGResourceHandle, kBloomLevels> bloomB{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomA{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomB{};
    bool bloomStageFailed = false;
    std::string bloomStageError;
    VisibilityBufferRenderer::ResourceStateSnapshot vbPostInitialStates{};
    bool hasVBPostStates = false;

    auto failBloomStage = [&](const char* stage) {
        if (!bloomStageFailed) {
            bloomStageError = stage ? stage : "unknown";
        }
        bloomStageFailed = true;
    };

    if (useFusedBloomTransients) {
        for (uint32_t level = 0; level < kBloomLevels; ++level) {
            savedBloomA[level] = m_bloomResources.texA[level];
            savedBloomB[level] = m_bloomResources.texB[level];
        }

        static bool s_loggedFusedBloomTransients = false;
        if (!s_loggedFusedBloomTransients) {
            s_loggedFusedBloomTransients = true;
            spdlog::info("Bloom RG: fused graph-owned intermediates enabled (default)");
        }
    }

    auto bloomFullscreenContext = [&]() {
        BloomPass::FullscreenContext context{};
        context.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        context.commandList = m_commandResources.graphicsList.Get();
        context.descriptorManager = m_services.descriptorManager.get();
        context.rootSignature = m_pipelineState.rootSignature.get();
        context.frameConstants = m_constantBuffers.currentFrameGPU;
        context.srvTable = m_bloomResources.srvTables[m_frameRuntime.frameIndex % kFrameCount].data();
        context.srvTableCount = kBloomDescriptorSlots;
        context.srvTableValid = m_bloomResources.srvTableValid;
        return context;
    };

    if (wantsRgPostThisFrame) {
        hdrHandle = m_services.renderGraph->ImportResource(m_mainTargets.hdrColor.Get(), m_mainTargets.hdrState, "HDR");
        if (m_temporalScreenState.historyColor) {
            historyHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.historyColor.Get(), m_temporalScreenState.historyState, "TAAHistory");
        }
        if (m_depthResources.buffer) {
            depthPpHandle = depthHandle.IsValid()
                ? depthHandle
                : m_services.renderGraph->ImportResource(m_depthResources.buffer.Get(), m_depthResources.resourceState, "Depth_Post");
        }
        if (m_ssaoResources.texture) {
            ssaoHandle = m_services.renderGraph->ImportResource(m_ssaoResources.texture.Get(), m_ssaoResources.resourceState, "SSAO");
        }
        if (m_ssrResources.color) {
            ssrHandle = m_services.renderGraph->ImportResource(m_ssrResources.color.Get(), m_ssrResources.resourceState, "SSRColor");
        }
        if (wantsFusedBloomThisFrame) {
            if (!useFusedBloomTransients) {
                for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
                    if (m_bloomResources.texA[level]) {
                        bloomA[level] = m_services.renderGraph->ImportResource(
                            m_bloomResources.texA[level].Get(),
                            m_bloomResources.resourceState[level][0],
                            "BloomA_FusedPersistent" + std::to_string(level));
                    }
                    if (m_bloomResources.texB[level]) {
                        bloomB[level] = m_services.renderGraph->ImportResource(
                            m_bloomResources.texB[level].Get(),
                            m_bloomResources.resourceState[level][1],
                            "BloomB_FusedPersistent" + std::to_string(level));
                    }
                }
            }

            const uint32_t baseLevel = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
            std::array<ID3D12Resource*, kBloomLevels> bloomATemplates{};
            std::array<ID3D12Resource*, kBloomLevels> bloomBTemplates{};
            for (uint32_t level = 0; level < kBloomLevels; ++level) {
                bloomATemplates[level] = savedBloomA[level].Get();
                bloomBTemplates[level] = savedBloomB[level].Get();
            }

            BloomGraphPass::FusedBloomContext bloomContext{};
            bloomContext.hdr = hdrHandle;
            bloomContext.bloomA = std::span<RGResourceHandle>(bloomA.data(), bloomA.size());
            bloomContext.bloomB = std::span<RGResourceHandle>(bloomB.data(), bloomB.size());
            bloomContext.bloomATemplates =
                std::span<ID3D12Resource* const>(bloomATemplates.data(), bloomATemplates.size());
            bloomContext.bloomBTemplates =
                std::span<ID3D12Resource* const>(bloomBTemplates.data(), bloomBTemplates.size());
            bloomContext.graphRtv = m_bloomResources.graphRtv;
            bloomContext.fullscreen = bloomFullscreenContext();
            bloomContext.downsamplePipeline = m_pipelineState.bloomDownsample.get();
            bloomContext.blurHPipeline = m_pipelineState.bloomBlurH.get();
            bloomContext.blurVPipeline = m_pipelineState.bloomBlurV.get();
            bloomContext.compositePipeline = m_pipelineState.bloomComposite.get();
            bloomContext.activeLevels = m_bloomResources.activeLevels;
            bloomContext.stageLevels = kBloomLevels;
            bloomContext.baseLevel = baseLevel;
            bloomContext.useTransients = useFusedBloomTransients;
            bloomContext.markHdrShaderResource = [&]() {
                m_mainTargets.hdrState = kRenderGraphShaderResourceState;
            };
            bloomContext.failStage = failBloomStage;
            bloomContext.markBloomRan = [&]() {
                result.ranBloom = !bloomStageFailed;
            };

            bloomHandle = BloomGraphPass::AddFusedBloom(*m_services.renderGraph, bloomContext);
        } else if (m_bloomResources.intensity > 0.0f) {
            ID3D12Resource* bloomRes = (m_bloomResources.activeLevels > 1) ? m_bloomResources.texA[1].Get() : m_bloomResources.texA[0].Get();
            if (bloomRes) {
                const uint32_t level = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
                bloomHandle = m_services.renderGraph->ImportResource(bloomRes, m_bloomResources.resourceState[level][0], "BloomCombined");
            }
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer) {
            vbPostInitialStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            hasVBPostStates = true;
        }
        {
            ID3D12Resource* normalRes = m_mainTargets.gbufferNormalRoughness.Get();
            D3D12_RESOURCE_STATES normalState = m_mainTargets.gbufferNormalRoughnessState;
            if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
                normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
                normalState = vbPostInitialStates.normalRoughness;
            }
            if (normalRes) {
                normalHandle = m_services.renderGraph->ImportResource(normalRes, normalState, "NormalRoughness");
            }
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetEmissiveMetallicBuffer()) {
            emissiveMetallicHandle = m_services.renderGraph->ImportResource(
                m_services.visibilityBuffer->GetEmissiveMetallicBuffer(),
                vbPostInitialStates.emissiveMetallic,
                "EmissiveMetallic");
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetMaterialExt1Buffer()) {
            materialExt1Handle = m_services.renderGraph->ImportResource(
                m_services.visibilityBuffer->GetMaterialExt1Buffer(),
                vbPostInitialStates.materialExt1,
                "MaterialExt1");
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetMaterialExt2Buffer()) {
            materialExt2Handle = m_services.renderGraph->ImportResource(
                m_services.visibilityBuffer->GetMaterialExt2Buffer(),
                vbPostInitialStates.materialExt2,
                "MaterialExt2_SurfaceClass");
        }
        if (m_temporalScreenState.velocityBuffer) {
            velocityHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity");
        }
        if (m_temporalScreenState.taaIntermediate) {
            taaHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.taaIntermediate.Get(), m_temporalScreenState.taaIntermediateState, "TAAIntermediate");
        }
        if (m_rtReflectionTargets.color) {
            rtReflHandle = m_services.renderGraph->ImportResource(m_rtReflectionTargets.color.Get(), m_rtReflectionTargets.colorState, "RTReflection");
        }
        if (m_rtReflectionTargets.history) {
            rtReflHistHandle =
                m_services.renderGraph->ImportResource(m_rtReflectionTargets.history.Get(), m_rtReflectionTargets.historyState, "RTReflectionHistory");
        }

        backBufferHandle = m_services.renderGraph->ImportResource(
            m_services.window->GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            "BackBuffer");

        const bool wantsHzbDebug = (m_debugViewState.mode == 32u);
        if (wantsHzbDebug && m_hzbResources.texture && !hzbHandle.IsValid()) {
            hzbHandle = m_services.renderGraph->ImportResource(m_hzbResources.texture.Get(), m_hzbResources.resourceState, "HZB_Debug");
        }

        const PostProcessGraphPass::ResourceHandles postProcessResources{
            hdrHandle,
            bloomHandle,
            ssaoHandle,
            historyHandle,
            depthPpHandle,
            normalHandle,
            emissiveMetallicHandle,
            materialExt1Handle,
            materialExt2Handle,
            ssrHandle,
            velocityHandle,
            taaHandle,
            rtReflHandle,
            rtReflHistHandle,
            hzbHandle,
            backBufferHandle,
            wantsHzbDebug,
        };

        PostProcessGraphPass::ExecuteContext executeContext{};
        executeContext.useBloomOverride = wantsFusedBloomThisFrame && bloomHandle.IsValid();
        executeContext.bloom = bloomHandle;
        executeContext.renderWithBloomOverride = [&](ID3D12Resource* bloomOverride) {
            ScopedRenderPassValue<ID3D12Resource*> bloomOverrideScope(m_bloomResources.postProcessOverride, bloomOverride);
            ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.postProcessSkipTransitions, true);
            RenderPostProcess();
        };
        executeContext.renderDefault = [&]() {
            ScopedRenderPassValue<bool> skipTransitions(m_frameDiagnostics.renderGraph.transitions.postProcessSkipTransitions, true);
            RenderPostProcess();
        };
        executeContext.failBloomStage = failBloomStage;
        executeContext.markRan = [&]() {
            result.ranPostProcess = true;
        };

        PostProcessGraphPass::GraphContext postProcessContext{};
        postProcessContext.resources = postProcessResources;
        postProcessContext.execute = std::move(executeContext);
        postProcessContext.failStage = failBloomStage;
        const RGResourceHandle postProcessResult =
            PostProcessGraphPass::AddToGraph(*m_services.renderGraph, postProcessContext);
        if (!postProcessResult.IsValid()) {
            failBloomStage("post_process_graph_contract");
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats();
    Debug::GPUProfiler::Get().EndScope(m_commandResources.graphicsList.Get());

    if (execResult.IsErr()) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
        spdlog::warn("RenderGraph end-of-frame: Execute failed: {}", result.fallbackReason);
        RecordFramePass("RenderGraphEndFrame", true, false, 0,
                        {"depth", "hdr_color", "ssao", "ssr_color", "bloom", "taa_history", "velocity", "rt_reflection", "hzb"},
                        {"hzb", "back_buffer"},
                        true,
                        result.fallbackReason.c_str());
        result.ranBloom = false;
        m_services.renderGraph->EndFrame();
        return result;
    }

    if (wantsFusedBloomThisFrame && bloomStageFailed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        result.fallbackUsed = true;
        result.fallbackReason = "fused_bloom_graph_stage_failed: " + bloomStageError;
        result.ranBloom = false;
        result.ranPostProcess = false;
        spdlog::warn("RenderGraph end-of-frame: {}", result.fallbackReason);
    }

    static bool s_loggedHzbRg = false;
    if (!s_loggedHzbRg && wantsRgHzbThisFrame) {
        s_loggedHzbRg = true;
        spdlog::info("HZB RG: passes={}, barriers={}",
                     m_services.renderGraph->GetPassCount(), m_services.renderGraph->GetBarrierCount());
    }

    if (wantsRgHzbThisFrame) {
        m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_hzbResources.resourceState = m_services.renderGraph->GetResourceState(hzbHandle);
        m_hzbResources.valid = true;

        m_hzbResources.captureViewMatrix = m_constantBuffers.frameCPU.viewMatrix;
        m_hzbResources.captureViewProjMatrix = m_constantBuffers.frameCPU.viewProjectionMatrix;
        m_hzbResources.captureCameraPosWS = m_cameraState.positionWS;
        m_hzbResources.captureCameraForwardWS = glm::normalize(m_cameraState.forwardWS);
        m_hzbResources.captureNearPlane = m_cameraState.nearPlane;
        m_hzbResources.captureFarPlane = m_cameraState.farPlane;
        m_hzbResources.captureFrameCounter = m_frameLifecycle.renderFrameCounter;
        m_hzbResources.captureValid = true;
        result.ranHZB = true;
        RecordFramePass("HZB", true, true, 0,
                        {"depth"},
                        {"hzb"},
                        false,
                        nullptr,
                        true);
    }

    if (wantsRgPostThisFrame) {
        m_mainTargets.hdrState = m_services.renderGraph->GetResourceState(hdrHandle);
        if (bloomHandle.IsValid() && !wantsFusedBloomThisFrame) {
            const uint32_t level = (m_bloomResources.activeLevels > 1) ? 1u : 0u;
            m_bloomResources.resourceState[level][0] = m_services.renderGraph->GetResourceState(bloomHandle);
        }
        if (ssaoHandle.IsValid()) m_ssaoResources.resourceState = m_services.renderGraph->GetResourceState(ssaoHandle);
        if (ssrHandle.IsValid()) m_ssrResources.resourceState = m_services.renderGraph->GetResourceState(ssrHandle);
        if (historyHandle.IsValid()) m_temporalScreenState.historyState = m_services.renderGraph->GetResourceState(historyHandle);
        if (depthPpHandle.IsValid()) m_depthResources.resourceState = m_services.renderGraph->GetResourceState(depthPpHandle);
        if (!m_visibilityBufferState.renderedThisFrame && normalHandle.IsValid()) {
            m_mainTargets.gbufferNormalRoughnessState = m_services.renderGraph->GetResourceState(normalHandle);
        }
        if (hasVBPostStates) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            if (normalHandle.IsValid()) {
                finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            }
            if (emissiveMetallicHandle.IsValid()) {
                finalStates.emissiveMetallic = m_services.renderGraph->GetResourceState(emissiveMetallicHandle);
            }
            if (materialExt1Handle.IsValid()) {
                finalStates.materialExt1 = m_services.renderGraph->GetResourceState(materialExt1Handle);
            }
            if (materialExt2Handle.IsValid()) {
                finalStates.materialExt2 = m_services.renderGraph->GetResourceState(materialExt2Handle);
            }
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        }
        if (velocityHandle.IsValid()) m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        if (taaHandle.IsValid()) m_temporalScreenState.taaIntermediateState = m_services.renderGraph->GetResourceState(taaHandle);
        if (rtReflHandle.IsValid()) m_rtReflectionTargets.colorState = m_services.renderGraph->GetResourceState(rtReflHandle);
        if (rtReflHistHandle.IsValid()) m_rtReflectionTargets.historyState = m_services.renderGraph->GetResourceState(rtReflHistHandle);
        if (hzbHandle.IsValid() && (m_debugViewState.mode == 32u)) m_hzbResources.resourceState = m_services.renderGraph->GetResourceState(hzbHandle);
        if (wantsFusedBloomThisFrame && !useFusedBloomTransients) {
            for (uint32_t level = 0; level < m_bloomResources.activeLevels; ++level) {
                if (bloomA[level].IsValid()) {
                    m_bloomResources.resourceState[level][0] = m_services.renderGraph->GetResourceState(bloomA[level]);
                }
                if (bloomB[level].IsValid()) {
                    m_bloomResources.resourceState[level][1] = m_services.renderGraph->GetResourceState(bloomB[level]);
                }
            }
        }
        if (wantsFusedBloomThisFrame && result.ranBloom) {
            RecordFramePass("Bloom", true, true, 1,
                            {"hdr_color"},
                            {"bloom"},
                            false,
                            nullptr,
                            true);
        }
        RecordFramePass("PostProcess", true, result.ranPostProcess, result.ranPostProcess ? 1u : 0u,
                        {"hdr_color", "ssao", "ssr_color", "bloom", "taa_history", "depth",
                         inputs.frameNormalRoughnessResource,
                         "vb_gbuffer_emissive_metallic", "vb_gbuffer_material_ext1",
                         "vb_gbuffer_material_ext2", "velocity", "rt_reflection", "hzb"},
                        {"back_buffer"},
                        false,
                        nullptr,
                        true);
    }

    RecordFramePass("RenderGraphEndFrame", true, true, result.ranPostProcess ? 1u : 0u,
                    {"depth", "hdr_color", "ssao", "ssr_color", "bloom", "taa_history",
                     "velocity", "rt_reflection", "vb_gbuffer_material_ext1",
                     "vb_gbuffer_material_ext2", "hzb"},
                    {"hzb", "back_buffer"});

    m_services.renderGraph->EndFrame();
    return result;
}

} // namespace Cortex::Graphics
