#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"

#include <chrono>

namespace Cortex::Graphics {

void Renderer::ExecutePostProcessingFramePhase(const FrameExecutionContext& frameCtx,
                                               const MainSceneEffectsResult& mainEffects) {
    using clock = std::chrono::high_resolution_clock;

    const FrameFeaturePlan& featurePlan = frameCtx.features;
    const char* frameNormalRoughnessResource = mainEffects.frameNormalRoughnessResource;

    if (featurePlan.runSSR) {
        const auto tSsrStart = clock::now();
        WriteBreadcrumb(GpuMarker::SSR);
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "SSR", "PostProcess");
        const auto ssrRgResult = ExecuteSSRInRenderGraph();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("SSR",
                        true,
                        ssrRgResult.executed,
                        1,
                        {"hdr_color", "depth", frameNormalRoughnessResource},
                        {"ssr_color"},
                        ssrRgResult.fallbackUsed,
                        ssrRgResult.fallbackUsed ? ssrRgResult.fallbackReason.c_str() : nullptr,
                        !ssrRgResult.fallbackUsed);
        MarkPassComplete("RenderSSR_Done");
        const auto tSsrEnd = clock::now();
        m_frameDiagnostics.timings.ssrMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tSsrEnd - tSsrStart).count() / 1000.0f;
    } else {
        m_frameDiagnostics.timings.ssrMs = 0.0f;
    }

    if (featurePlan.runParticles) {
        MarkPassComplete("RenderParticles_Begin");
        WriteBreadcrumb(GpuMarker::Particles);
        const uint32_t particleDrawsBefore = m_frameDiagnostics.contract.drawCounts.particleDraws;
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Particles", "Transparency");
        RenderParticles(frameCtx.registry);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("Particles",
                        true,
                        m_frameDiagnostics.contract.drawCounts.particleDraws > particleDrawsBefore,
                        m_frameDiagnostics.contract.drawCounts.particleDraws - particleDrawsBefore,
                        {"frame_constants", "hdr_color"},
                        {"hdr_color"},
                        false,
                        nullptr);
        MarkPassComplete("RenderParticles_Done");
    }

    {
        const auto tSsaoStart = clock::now();
        if (featurePlan.runSSAO) {
            WriteBreadcrumb(GpuMarker::SSAO);
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "SSAO", "PostProcess");
            const auto ssaoRgResult = ExecuteSSAOInRenderGraph();
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("SSAO",
                            true,
                            ssaoRgResult.executed,
                            1,
                            {"depth", "frame_constants"},
                            {"ssao"},
                            ssaoRgResult.fallbackUsed,
                            ssaoRgResult.fallbackUsed ? ssaoRgResult.fallbackReason.c_str() : nullptr,
                            !ssaoRgResult.fallbackUsed);
            MarkPassComplete("RenderSSAO_Done");
        } else {
            m_frameDiagnostics.timings.ssaoMs = 0.0f;
        }
        const auto tSsaoEnd = clock::now();
        m_frameDiagnostics.timings.ssaoMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tSsaoEnd - tSsaoStart).count() / 1000.0f;
    }

    const bool runBloomInEndFrameGraph =
        featurePlan.runBloom &&
        featurePlan.runPostProcess &&
        featurePlan.useRenderGraphPost &&
        m_services.renderGraph &&
        m_commandResources.graphicsList &&
        m_mainTargets.hdrColor &&
        m_services.window &&
        m_services.window->GetCurrentBackBuffer() &&
        m_pipelineState.postProcess &&
        m_pipelineState.bloomDownsample &&
        m_pipelineState.bloomBlurH &&
        m_pipelineState.bloomBlurV &&
        m_pipelineState.bloomComposite &&
        m_mainTargets.hdrSRV.IsValid() &&
        m_bloomResources.intensity > 0.0f &&
        m_bloomResources.texA[0] &&
        m_bloomResources.texB[0];

    {
        const auto tBloomStart = clock::now();
        if (featurePlan.runBloom && !runBloomInEndFrameGraph) {
            WriteBreadcrumb(GpuMarker::Bloom);
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Bloom", "PostProcess");
            const auto bloomRgResult = ExecuteBloomInRenderGraph();
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("Bloom",
                            true,
                            bloomRgResult.executed,
                            1,
                            {"hdr_color"},
                            {"bloom"},
                            bloomRgResult.fallbackUsed,
                            bloomRgResult.fallbackUsed ? bloomRgResult.fallbackReason.c_str() : nullptr,
                            !bloomRgResult.fallbackUsed);
            MarkPassComplete("RenderBloom_Done");
        } else if (runBloomInEndFrameGraph) {
            MarkPassComplete("RenderBloom_DeferToEndFrameGraph");
        } else {
            m_frameDiagnostics.timings.bloomMs = 0.0f;
        }
        const auto tBloomEnd = clock::now();
        m_frameDiagnostics.timings.bloomMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tBloomEnd - tBloomStart).count() / 1000.0f;
    }

    const EndFrameGraphResult endFrameGraph = ExecuteEndFrameInRenderGraph({
        mainEffects.rgHasPendingHzb,
        runBloomInEndFrameGraph,
        featurePlan.runPostProcess,
        featurePlan.useRenderGraphHZB,
        featurePlan.useRenderGraphPost,
        frameNormalRoughnessResource
    });

    if (featurePlan.runPostProcess) {
        if (!endFrameGraph.ranPostProcess) {
            if (runBloomInEndFrameGraph && !endFrameGraph.ranBloom) {
                const auto tBloomFallbackStart = clock::now();
                WriteBreadcrumb(GpuMarker::Bloom);
                FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Bloom", "PostProcess");
                RenderBloom();
                FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
                RecordFramePass("Bloom",
                                true,
                                true,
                                1,
                                {"hdr_color"},
                                {"bloom"},
                                endFrameGraph.fallbackUsed,
                                endFrameGraph.fallbackUsed ? endFrameGraph.fallbackReason.c_str() : nullptr,
                                false);
                MarkPassComplete("RenderBloom_Done");
                const auto tBloomFallbackEnd = clock::now();
                m_frameDiagnostics.timings.bloomMs =
                    std::chrono::duration_cast<std::chrono::microseconds>(tBloomFallbackEnd - tBloomFallbackStart).count() / 1000.0f;
            }

            const auto tPostOnlyStart = clock::now();
            WriteBreadcrumb(GpuMarker::PostProcess);
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "PostProcess", "PostProcess");
            RenderPostProcess();
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("PostProcess",
                            true,
                            true,
                            1,
                            {"hdr_color",
                             "ssao",
                             "ssr_color",
                             "bloom",
                             "taa_history",
                             "depth",
                             frameNormalRoughnessResource,
                             "vb_gbuffer_emissive_metallic",
                             "vb_gbuffer_material_ext1",
                             "vb_gbuffer_material_ext2",
                             "velocity",
                             "rt_reflection"},
                            {"back_buffer"});
            MarkPassComplete("RenderPostProcess_Done");
            const auto tPostOnlyEnd = clock::now();
            m_frameDiagnostics.timings.postMs =
                std::chrono::duration_cast<std::chrono::microseconds>(tPostOnlyEnd - tPostOnlyStart).count() / 1000.0f;
        } else {
            MarkPassComplete("RenderPostProcess_Done");
        }
    } else {
        m_frameDiagnostics.timings.postMs = 0.0f;
        MarkPassComplete("RenderPostProcess_Skipped");
    }
}

void Renderer::CompleteFrameExecutionPhase(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;

    // Debug overlay lines are rendered after all post-processing so they are
    // not affected by tone mapping, bloom, or TAA.
    if (featurePlan.runDebugLines) {
        WriteBreadcrumb(GpuMarker::DebugLines);
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "DebugLines", "UI");
        const uint32_t debugDrawsBefore = m_frameDiagnostics.contract.drawCounts.debugLineDraws;
        RenderDebugLines();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("DebugLines",
                        true,
                        m_frameDiagnostics.contract.drawCounts.debugLineDraws > debugDrawsBefore,
                        m_frameDiagnostics.contract.drawCounts.debugLineDraws - debugDrawsBefore,
                        {"back_buffer"},
                        {"back_buffer"});
        MarkPassComplete("RenderDebugLines_Done");
    }

    EndFrame();
}

} // namespace Cortex::Graphics
