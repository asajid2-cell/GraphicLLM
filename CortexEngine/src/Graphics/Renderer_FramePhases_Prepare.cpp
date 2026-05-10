#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"

#include <chrono>

namespace Cortex::Graphics {

void Renderer::ExecuteRayTracingFramePhase(const FrameExecutionContext& frameCtx) {
    using clock = std::chrono::high_resolution_clock;

    // Optional ray tracing path (DXR). When enabled, build BLAS/TLAS and
    // dispatch ray-traced passes using the current frame's depth buffer. The
    // depth prepass stays coupled here because TLAS/RT inputs must agree with
    // the depth-writing renderable set for the same frame.
    const auto tBeforeRT = clock::now();
    if (m_framePlanning.rtPlan.enabled) {
        const auto tDepthStart = clock::now();
        const uint32_t depthDrawsBefore = m_frameDiagnostics.contract.drawCounts.depthPrepassDraws;
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "DepthPrepass", "Depth");
        const RenderGraphPassResult depthGraphResult = ExecuteDepthPrepassInRenderGraph(frameCtx.registry);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("DepthPrepass",
                        true,
                        depthGraphResult.executed,
                        m_frameDiagnostics.contract.drawCounts.depthPrepassDraws - depthDrawsBefore,
                        {"frame_constants", "renderables"},
                        {"depth"},
                        depthGraphResult.fallbackUsed,
                        depthGraphResult.fallbackReason.c_str(),
                        !depthGraphResult.fallbackUsed);
        MarkPassComplete("RenderDepthPrepass_Done");
        const auto tDepthEnd = clock::now();
        m_frameDiagnostics.timings.depthPrepassMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tDepthEnd - tDepthStart).count() / 1000.0f;

        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "RTShadowsGI", "RayTracing");
        RenderRayTracing(frameCtx.registry);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("RTShadowsGI",
                        m_framePlanning.rtPlan.dispatchShadows || m_framePlanning.rtPlan.dispatchGI,
                        m_framePlanning.rtPlan.dispatchShadows || m_framePlanning.rtPlan.dispatchGI,
                        0,
                        {"depth", "renderables"},
                        {"rt_shadow_mask", "rt_gi", "acceleration_structures"});
        MarkPassComplete("RenderRayTracing_Done");
    }
    const auto tAfterRT = clock::now();
    m_frameDiagnostics.timings.rtPassMs =
        std::chrono::duration_cast<std::chrono::microseconds>(tAfterRT - tBeforeRT).count() / 1000.0f;
}

void Renderer::ExecuteShadowFramePhase(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;
    if (!featurePlan.runShadowPass) {
        m_frameDiagnostics.timings.shadowPassMs = 0.0f;
        return;
    }

    using clock = std::chrono::high_resolution_clock;

    const auto tShadowStart = clock::now();
    const uint32_t shadowDrawsBefore = m_frameDiagnostics.contract.drawCounts.shadowDraws;
    RenderGraphPassResult shadowGraphResult{};
    WriteBreadcrumb(GpuMarker::ShadowPass);
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "ShadowPass", "Shadow");

    if (featurePlan.useRenderGraphShadows) {
        shadowGraphResult = ExecuteShadowPassInRenderGraph(frameCtx.registry);
    } else {
        RenderShadowPass(frameCtx.registry);
        shadowGraphResult.executed = true;
    }
    RecordFramePass("ShadowPass",
                    true,
                    true,
                    m_frameDiagnostics.contract.drawCounts.shadowDraws - shadowDrawsBefore,
                    {"frame_constants", "renderables"},
                    {"shadow_map"},
                    shadowGraphResult.fallbackUsed,
                    shadowGraphResult.fallbackReason.c_str(),
                    featurePlan.useRenderGraphShadows && !shadowGraphResult.fallbackUsed);
    MarkPassComplete("RenderShadowPass_Done");
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
    const auto tShadowEnd = clock::now();
    m_frameDiagnostics.timings.shadowPassMs =
        std::chrono::duration_cast<std::chrono::microseconds>(tShadowEnd - tShadowStart).count() / 1000.0f;
}

} // namespace Cortex::Graphics
