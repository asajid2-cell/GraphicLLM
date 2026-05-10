#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::BeginMainSceneFramePhase(const FrameExecutionContext&) {
    // This opens the broad MainPass GPU scope. The scope is closed in
    // Renderer::Render after the geometry, RT-reflection, temporal, and TAA
    // work that contribute to the main HDR scene have completed.
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "MainPass", "Main");
    PrepareMainPass();
    MarkPassComplete("PrepareMainPass_Done");

    // Draw environment background (skybox) into the HDR target before geometry.
    WriteBreadcrumb(GpuMarker::Skybox);
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Skybox", "Main");
    RenderSkybox();
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
    RecordFramePass("Skybox",
                    true,
                    true,
                    1,
                    {"frame_constants", "environment"},
                    {"hdr_color"});
    MarkPassComplete("RenderSkybox_Done");
}

void Renderer::ExecuteGeometryFramePhase(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;
    bool drewWithHyper = false;

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    if (m_services.hyperGeometry) {
        auto buildResult = EnsureHyperGeometryScene(frameCtx.registry);
        if (buildResult.IsErr()) {
            spdlog::warn("Hyper-Geometry scene build failed: {}", buildResult.Error());
        } else {
            auto hyperResult = m_services.hyperGeometry->Render(m_commandResources.graphicsList.Get(), frameCtx.registry, m_services.window->GetAspectRatio());
            if (hyperResult.IsErr()) {
                spdlog::warn("Hyper-Geometry render failed: {}", hyperResult.Error());
            } else {
                drewWithHyper = true;
            }
        }
    }
#endif

    // Classic path now acts purely as fallback to avoid double-drawing/z-fighting.
    if (drewWithHyper) {
        return;
    }

    const bool vbEnabled = featurePlan.runVisibilityBuffer;
    if (vbEnabled) {
        WriteBreadcrumb(GpuMarker::OpaqueGeometry);
        const uint32_t vbInstancesBefore = m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances;
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "VisibilityBuffer", "Visibility");
        const RenderGraphPassResult vbGraphResult = ExecuteVisibilityBufferInRenderGraph(frameCtx.registry);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("VisibilityBuffer",
                        true,
                        m_visibilityBufferState.renderedThisFrame,
                        m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances - vbInstancesBefore,
                        {"frame_constants", "depth", "shadow_map", "renderables", "rt_shadow_mask", "rt_gi"},
                        {"hdr_color",
                         "visibility_buffer",
                         "vb_gbuffer_albedo",
                         "vb_gbuffer_normal_roughness",
                         "vb_gbuffer_emissive_metallic",
                         "vb_gbuffer_material_ext0",
                         "vb_gbuffer_material_ext1",
                         "vb_gbuffer_material_ext2",
                         "depth"},
                        vbGraphResult.fallbackUsed,
                        vbGraphResult.fallbackReason.c_str(),
                        !vbGraphResult.fallbackUsed);
        MarkPassComplete("VisibilityBuffer_Done");
    }

    // If VB is disabled or fails to produce a lit HDR frame, fall back to the
    // existing opaque render paths for robustness.
    if (!vbEnabled || !m_visibilityBufferState.renderedThisFrame) {
        if (vbEnabled && !m_visibilityBufferState.renderedThisFrame && m_depthResources.buffer && m_depthResources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_depthResources.buffer.Get();
            barrier.Transition.StateBefore = m_depthResources.resourceState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_depthResources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }

        if (featurePlan.runGpuCullingFallback) {
            using clock = std::chrono::steady_clock;
            static clock::time_point s_lastCullingPathLog{};
            const auto now = clock::now();
            if (s_lastCullingPathLog.time_since_epoch().count() == 0 ||
                (now - s_lastCullingPathLog) > std::chrono::seconds(20)) {
                spdlog::info("Taking GPU culling path");
                s_lastCullingPathLog = now;
            }
            WriteBreadcrumb(GpuMarker::OpaqueGeometry);
            const uint32_t indirectCallsBefore = m_frameDiagnostics.contract.drawCounts.indirectExecuteCalls;
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "SceneIndirect", "Visibility");
            RenderSceneIndirect(frameCtx.registry);
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("SceneIndirectFallback",
                            true,
                            true,
                            m_frameDiagnostics.contract.drawCounts.indirectExecuteCalls - indirectCallsBefore,
                            {"frame_constants", "renderables", "hzb"},
                            {"hdr_color", "depth"},
                            vbEnabled,
                            vbEnabled ? "visibility_buffer_not_rendered" : "");
            MarkPassComplete("RenderSceneIndirect_Done");
        } else {
            spdlog::info("Taking legacy forward rendering path");
            WriteBreadcrumb(GpuMarker::OpaqueGeometry);
            const uint32_t opaqueDrawsBefore = m_frameDiagnostics.contract.drawCounts.opaqueDraws;
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "ForwardScene", "Main");
            RenderScene(frameCtx.registry);
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("ForwardSceneFallback",
                            true,
                            true,
                            m_frameDiagnostics.contract.drawCounts.opaqueDraws - opaqueDrawsBefore,
                            {"frame_constants", "renderables", "shadow_map"},
                            {"hdr_color", "depth", "gbuffer_normal_roughness"},
                            vbEnabled,
                            vbEnabled ? "visibility_buffer_not_rendered" : "");
            MarkPassComplete("RenderScene_Done");
        }
    }

    // When VB debug visualization is active, keep the frame clean by skipping
    // passes that can obscure the intermediate buffer being inspected.
    if (m_visibilityBufferState.debugOverrideThisFrame) {
        return;
    }

    const uint32_t overlayDrawsBefore = m_frameDiagnostics.contract.drawCounts.overlayDraws;
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Overlays", "Main");
    RenderOverlays(frameCtx.registry);
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
    RecordFramePass("Overlays",
                    true,
                    true,
                    m_frameDiagnostics.contract.drawCounts.overlayDraws - overlayDrawsBefore,
                    {"frame_constants", "depth"},
                    {"hdr_color"});

    const uint32_t waterDrawsBefore = m_frameDiagnostics.contract.drawCounts.waterDraws;
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Water", "Transparency");
    RenderWaterSurfaces(frameCtx.registry);
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
    RecordFramePass("Water",
                    true,
                    true,
                    m_frameDiagnostics.contract.drawCounts.waterDraws - waterDrawsBefore,
                    {"frame_constants", "depth", "hdr_color"},
                    {"hdr_color"});

    WriteBreadcrumb(GpuMarker::TransparentGeom);
    const uint32_t transparentDrawsBefore = m_frameDiagnostics.contract.drawCounts.transparentDraws;
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Transparent", "Transparency");
    RenderTransparent(frameCtx.registry);
    FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
    RecordFramePass("Transparent",
                    true,
                    true,
                    m_frameDiagnostics.contract.drawCounts.transparentDraws - transparentDrawsBefore,
                    {"frame_constants", "depth", "hdr_color"},
                    {"hdr_color"});
    MarkPassComplete("RenderTransparent_Done");
}

Renderer::MainSceneEffectsResult Renderer::ExecuteMainSceneEffectsFramePhase(const FrameExecutionContext& frameCtx) {
    const FrameFeaturePlan& featurePlan = frameCtx.features;
    MainSceneEffectsResult result{};
    result.frameNormalRoughnessResource =
        m_visibilityBufferState.renderedThisFrame ? "vb_gbuffer_normal_roughness" : "gbuffer_normal_roughness";

    if (m_framePlanning.rtPlan.enabled) {
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "RTReflections", "RayTracing");
        RenderRayTracedReflections();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("RTReflections",
                        m_rtRuntimeState.reflectionsEnabled,
                        m_frameLifecycle.rtReflectionWrittenThisFrame,
                        0,
                        {"depth",
                         result.frameNormalRoughnessResource,
                         "vb_gbuffer_material_ext2",
                         "environment",
                         "acceleration_structures"},
                        {"rt_reflection"});
        CaptureRTReflectionSignalStats();
        MarkPassComplete("RenderRTReflections_Done");
    }

    if (featurePlan.runMotionVectors) {
        WriteBreadcrumb(GpuMarker::MotionVectors);
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "MotionVectors", "PostProcess");
        const auto motionRgResult = ExecuteMotionVectorsInRenderGraph();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("MotionVectors",
                        true,
                        motionRgResult.executed,
                        0,
                        {"depth", "frame_constants", "visibility_buffer"},
                        {"velocity"},
                        motionRgResult.fallbackUsed,
                        motionRgResult.fallbackUsed ? motionRgResult.fallbackReason.c_str() : nullptr,
                        !motionRgResult.fallbackUsed);
        MarkPassComplete("RenderMotionVectors_Done");

        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "TemporalRejectionMask", "PostProcess");
        (void)ExecuteTemporalRejectionMaskInRenderGraph(result.frameNormalRoughnessResource);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        MarkPassComplete("TemporalRejectionMask_Done");
    }

    if (m_framePlanning.rtPlan.enabled) {
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "RTDenoise", "RayTracing");
        ExecuteRTDenoisePass(result.frameNormalRoughnessResource);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        CaptureRTReflectionHistorySignalStats();
        MarkPassComplete("RTDenoise_Done");
    }

    if (featurePlan.runHZB) {
        if (featurePlan.useRenderGraphHZB &&
            m_services.renderGraph &&
            m_services.device &&
            m_commandResources.graphicsList &&
            m_services.descriptorManager &&
            m_depthResources.buffer &&
            m_depthResources.srv.IsValid()) {
            auto resResult = CreateHZBResources();
            if (resResult.IsErr()) {
                spdlog::warn("HZB RG: {}", resResult.Error());
            } else if (!m_hzbResources.texture || m_hzbResources.mipCount == 0 ||
                       m_hzbResources.mipSRVStaging.size() != m_hzbResources.mipCount ||
                       m_hzbResources.mipUAVStaging.size() != m_hzbResources.mipCount) {
                spdlog::warn("HZB RG: invalid resources (texture={}, mips={}, srvs={}, uavs={})",
                             static_cast<bool>(m_hzbResources.texture),
                             m_hzbResources.mipCount,
                             m_hzbResources.mipSRVStaging.size(),
                             m_hzbResources.mipUAVStaging.size());
            } else if (!m_hzbResources.mipSRVStaging.empty() && !m_hzbResources.mipSRVStaging[0].IsValid()) {
                spdlog::warn("HZB RG: staging SRV handle invalid (mip0 cpu ptr=0)");
            } else if (!m_hzbResources.mipUAVStaging.empty() && !m_hzbResources.mipUAVStaging[0].IsValid()) {
                spdlog::warn("HZB RG: staging UAV handle invalid (mip0 cpu ptr=0)");
            } else {
                result.rgHasPendingHzb = true;
            }
        } else {
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "HZB", "Visibility");
            BuildHZBFromDepth();
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("HZB", true, m_hzbResources.valid, 0, {"depth"}, {"hzb"});
        }
    }

    if (featurePlan.runTAA) {
        WriteBreadcrumb(GpuMarker::TAAResolve);
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "TAA", "PostProcess");
        const auto taaRgResult = ExecuteTAAInRenderGraph();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("TAA",
                        true,
                        taaRgResult.executed,
                        1,
                        {"hdr_color", "taa_history", "velocity", "temporal_rejection_mask"},
                        {"hdr_color", "taa_history"},
                        taaRgResult.fallbackUsed,
                        taaRgResult.fallbackUsed ? taaRgResult.fallbackReason.c_str() : nullptr,
                        !taaRgResult.fallbackUsed);
        MarkPassComplete("RenderTAA_Done");
    }

    return result;
}

} // namespace Cortex::Graphics
