#include "Renderer.h"
#include "Renderer_FramePhaseGpuScope.h"
#include "Graphics/Passes/DepthWriteTransitionPass.h"

#include <chrono>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::BeginMainSceneFramePhase(const FrameExecutionContext& frameCtx) {
    // This opens the broad MainPass GPU scope. The scope is closed in
    // Renderer::Render after the geometry, RT-reflection, temporal, and TAA
    // work that contribute to the main HDR scene have completed.
    FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "MainPass", "Main");
    PrepareMainPass();
    MarkPassComplete("PrepareMainPass_Done");

    // Draw environment background (skybox) into the HDR target before geometry.
    if (!frameCtx.features.debug.disableSkybox) {
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
    } else {
        RecordFramePass("Skybox",
                        true,
                        false,
                        0,
                        {"frame_constants", "environment"},
                        {"hdr_color"},
                        true,
                        "disabled_by_CORTEX_DISABLE_SKYBOX",
                        false);
        MarkPassComplete("RenderSkybox_Skipped");
    }
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

    const bool vbEnabled = featurePlan.runVisibilityBuffer && !featurePlan.debug.disableOpaqueGeometry;
    if (vbEnabled) {
        WriteBreadcrumb(GpuMarker::OpaqueGeometry);
        const uint32_t vbInstancesBefore = m_frameDiagnostics.contract.drawCounts.visibilityBufferInstances;
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "VisibilityBuffer", "Visibility");
        const RenderGraphPassResult vbGraphResult = ExecuteVisibilityBufferInRenderGraph(frameCtx.registry);
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("VisibilityBuffer",
                        true,
                        m_vb.State().renderedThisFrame,
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
    if (!featurePlan.debug.disableOpaqueGeometry && (!vbEnabled || !m_vb.State().renderedThisFrame)) {
        if (vbEnabled && !m_vb.State().renderedThisFrame && m_depthResources.resources.buffer && m_depthResources.resources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
            DepthWriteTransitionPass::TransitionContext depthTransitionContext{};
            depthTransitionContext.commandList = m_commandResources.graphicsList.Get();
            depthTransitionContext.depthBuffer = m_depthResources.resources.buffer.Get();
            depthTransitionContext.depthState = &m_depthResources.resources.resourceState;
            if (!DepthWriteTransitionPass::TransitionToDepthWrite(depthTransitionContext)) {
                spdlog::warn("Visibility-buffer fallback could not transition depth to write state");
            }
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
    } else if (featurePlan.debug.disableOpaqueGeometry) {
        RecordFramePass("ForwardSceneFallback",
                        true,
                        false,
                        0,
                        {"frame_constants", "renderables", "shadow_map"},
                        {"hdr_color", "depth", "gbuffer_normal_roughness"},
                        true,
                        "disabled_by_CORTEX_DISABLE_OPAQUE_GEOMETRY",
                        false);
        MarkPassComplete("RenderScene_Skipped");
    }

    // When VB debug visualization is active, keep the frame clean by skipping
    // passes that can obscure the intermediate buffer being inspected.
    if (m_vb.State().debugOverrideThisFrame) {
        return;
    }

    if (featurePlan.debug.disableAuxGeometry) {
        RecordFramePass("Overlays", true, false, 0, {"frame_constants", "depth"}, {"hdr_color"},
                        true, "disabled_by_CORTEX_DISABLE_AUX_GEOMETRY", false);
        RecordFramePass("Water", true, false, 0, {"frame_constants", "depth", "hdr_color"}, {"hdr_color"},
                        true, "disabled_by_CORTEX_DISABLE_AUX_GEOMETRY", false);
        RecordFramePass("Transparent", true, false, 0, {"frame_constants", "depth", "hdr_color"}, {"hdr_color"},
                        true, "disabled_by_CORTEX_DISABLE_AUX_GEOMETRY", false);
        MarkPassComplete("RenderAuxGeometry_Skipped");
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
        m_vb.State().renderedThisFrame ? "vb_gbuffer_normal_roughness" : "gbuffer_normal_roughness";

    if (m_framePlanning.rtPlan.enabled) {
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "RTReflections", "RayTracing");
        RenderRayTracedReflections();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        RecordFramePass("RTReflections",
                        m_rt.RuntimeState().reflectionsEnabled,
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

    {
        FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "Volumetrics", "PostProcess");
        RenderVolumetrics();
        FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
        const bool ranVolumetrics =
            m_volumetrics.resourcesValid &&
            m_pipelineState.volumetricInjectCompute &&
            m_pipelineState.volumetricIntegrateCompute &&
            m_pipelineState.volumetricCompositeCompute;
        RecordFramePass("VolumetricFroxels",
                        ranVolumetrics,
                        ranVolumetrics,
                        0,
                        {"frame_constants", "depth", "shadow_map", "hdr_color", "volumetric_history"},
                        {"hdr_color", "volumetric_injected", "volumetric_integrated", "volumetric_history"});
        MarkPassComplete(ranVolumetrics ? "RenderVolumetrics_Done" : "RenderVolumetrics_Skipped");
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
            m_depthResources.resources.buffer &&
            m_depthResources.descriptors.srv.IsValid()) {
            auto resResult = CreateHZBResources();
            if (resResult.IsErr()) {
                spdlog::warn("HZB RG: {}", resResult.Error());
            } else if (!m_hzb.State().resources.texture || m_hzb.State().resources.mipCount == 0 ||
                       m_hzb.State().descriptors.mipSRVStaging.size() != m_hzb.State().resources.mipCount ||
                       m_hzb.State().descriptors.mipUAVStaging.size() != m_hzb.State().resources.mipCount) {
                spdlog::warn("HZB RG: invalid resources (texture={}, mips={}, srvs={}, uavs={})",
                             static_cast<bool>(m_hzb.State().resources.texture),
                             m_hzb.State().resources.mipCount,
                             m_hzb.State().descriptors.mipSRVStaging.size(),
                             m_hzb.State().descriptors.mipUAVStaging.size());
            } else if (!m_hzb.State().descriptors.mipSRVStaging.empty() && !m_hzb.State().descriptors.mipSRVStaging[0].IsValid()) {
                spdlog::warn("HZB RG: staging SRV handle invalid (mip0 cpu ptr=0)");
            } else if (!m_hzb.State().descriptors.mipUAVStaging.empty() && !m_hzb.State().descriptors.mipUAVStaging[0].IsValid()) {
                spdlog::warn("HZB RG: staging UAV handle invalid (mip0 cpu ptr=0)");
            } else {
                result.rgHasPendingHzb = true;
            }
        } else {
            FramePhase::BeginGpuScope(m_commandResources.graphicsList.Get(), "HZB", "Visibility");
            BuildHZBFromDepth();
            FramePhase::EndGpuScope(m_commandResources.graphicsList.Get());
            RecordFramePass("HZB", true, m_hzb.State().resources.valid, 0, {"depth"}, {"hzb"});
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
