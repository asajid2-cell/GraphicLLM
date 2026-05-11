#include "Renderer.h"

#include "Graphics/Passes/RTReflectionDispatchPass.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

void Renderer::RenderRayTracedReflections() {
    auto setReflectionReadinessReason = [&](const char* reason) {
        if (m_rtReflectionReadiness.reason.empty() && reason) {
            m_rtReflectionReadiness.reason = reason;
        }
    };

    m_rtReflectionReadiness.hasPipeline =
        m_services.rayTracingContext && m_services.rayTracingContext->HasReflectionPipeline();
    m_rtReflectionReadiness.hasTLAS =
        m_services.rayTracingContext && m_services.rayTracingContext->HasTLAS();
    m_rtReflectionReadiness.hasMaterialBuffer =
        m_services.rayTracingContext && m_services.rayTracingContext->HasRTMaterialBuffer();
    m_rtReflectionReadiness.hasOutput =
        m_rtReflectionTargets.color != nullptr && m_rtReflectionTargets.uav.IsValid();
    m_rtReflectionReadiness.hasDepth =
        m_depthResources.resources.buffer != nullptr && m_depthResources.descriptors.srv.IsValid();
    m_rtReflectionReadiness.hasFrameConstants =
        m_constantBuffers.currentFrameGPU != 0;
    m_rtReflectionReadiness.hasDispatchDescriptors =
        m_services.rayTracingContext && m_services.rayTracingContext->HasDispatchDescriptorTables();

    if (!m_framePlanning.rtPlan.dispatchReflections || !m_services.rayTracingContext) {
        setReflectionReadinessReason(!m_framePlanning.rtPlan.dispatchReflections
            ? "not_scheduled_this_frame"
            : "ray_tracing_context_missing");
        return;
    }

    if (!m_rtRuntimeState.reflectionsEnabled || !m_rtReflectionTargets.color || !m_rtReflectionTargets.uav.IsValid()) {
        setReflectionReadinessReason(!m_rtRuntimeState.reflectionsEnabled
            ? "feature_disabled"
            : "reflection_output_missing");
        return;
    }

    if (!m_services.rayTracingContext->HasReflectionPipeline()) {
        setReflectionReadinessReason("reflection_pipeline_missing");
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = m_commandResources.graphicsList.As(&rtCmdList);
    if (FAILED(hr) || !rtCmdList) {
        setReflectionReadinessReason("dxr_command_list_missing");
        return;
    }

    DescriptorHandle normalSrv = m_mainTargets.normalRoughness.descriptors.srv;
    DescriptorHandle materialExt2Srv{};
    ID3D12Resource* normalResource = m_mainTargets.normalRoughness.resources.texture.Get();
    D3D12_RESOURCE_STATES* normalState = &m_mainTargets.normalRoughness.resources.state;
    ID3D12Resource* materialExt2Resource = nullptr;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer) {
        const DescriptorHandle& vbNormal = m_services.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormal.IsValid()) {
            normalSrv = vbNormal;
            normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
            normalState = nullptr;
        }
        const DescriptorHandle& vbMaterialExt2 = m_services.visibilityBuffer->GetMaterialExt2SRVHandle();
        if (vbMaterialExt2.IsValid()) {
            materialExt2Srv = vbMaterialExt2;
            materialExt2Resource = m_services.visibilityBuffer->GetMaterialExt2Buffer();
        }
    }
    m_rtReflectionReadiness.hasNormalRoughness =
        normalResource != nullptr && normalSrv.IsValid();
    m_rtReflectionReadiness.hasMaterialExt2 =
        materialExt2Resource != nullptr && materialExt2Srv.IsValid();

    if (!m_depthResources.descriptors.srv.IsValid() || !normalSrv.IsValid()) {
        setReflectionReadinessReason(!m_depthResources.descriptors.srv.IsValid()
            ? "depth_srv_missing"
            : "normal_roughness_srv_missing");
        return;
    }

    RTReflectionDispatchPass::PrepareContext prepareContext{};
    prepareContext.commandList = rtCmdList.Get();
    prepareContext.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState};
    prepareContext.normalRoughness = {normalResource, normalState};
    prepareContext.reflectionOutput = {m_rtReflectionTargets.color.Get(), &m_rtReflectionTargets.colorState};
    prepareContext.transitionNormal = !m_visibilityBufferState.renderedThisFrame;
    if (!RTReflectionDispatchPass::PrepareInputsAndOutput(prepareContext)) {
        setReflectionReadinessReason("reflection_resource_transition_failed");
        return;
    }

    static bool s_checkedRtReflDebug = false;
    static int  s_rtReflClearMode = 0;
    static bool s_rtReflSkipDispatch = false;
    if (!s_checkedRtReflDebug) {
        s_checkedRtReflDebug = true;
        if (const char* mode = std::getenv("CORTEX_RTREFL_CLEAR")) {
            s_rtReflClearMode = std::atoi(mode);
            if (s_rtReflClearMode != 0) {
                spdlog::warn("Renderer: CORTEX_RTREFL_CLEAR={} set; clearing RT reflection target each frame (0=off,1=black,2=magenta)",
                             s_rtReflClearMode);
            }
        }
        if (std::getenv("CORTEX_RTREFL_SKIP_DXR")) {
            s_rtReflSkipDispatch = true;
            spdlog::warn("Renderer: CORTEX_RTREFL_SKIP_DXR set; skipping DXR reflection dispatch (debug)");
        }
    }

    const bool rtReflDebugView =
        (m_debugViewState.mode == 20u || m_debugViewState.mode == 30u || m_debugViewState.mode == 31u);

    // Optional debug clear to eliminate stale-tile/rectangle artifacts. This also
    // lets debug view 20 validate that the post-process SRV binding (t8) is correct.
    if (rtReflDebugView && s_rtReflClearMode != 0 && m_services.descriptorManager && m_services.device && m_rtReflectionTargets.uav.IsValid()) {
        DescriptorHandle clearUav = m_rtReflectionTargets.dispatchClearUAVs[m_frameRuntime.frameIndex % kFrameCount];
        if (clearUav.IsValid()) {
            RTReflectionDispatchPass::DebugClearContext clearContext{};
            clearContext.commandList = rtCmdList.Get();
            clearContext.device = m_services.device->GetDevice();
            clearContext.descriptorHeap = m_services.descriptorManager->GetCBV_SRV_UAV_Heap();
            clearContext.reflectionOutput = {m_rtReflectionTargets.color.Get(), &m_rtReflectionTargets.colorState};
            clearContext.shaderVisibleUav = clearUav;
            clearContext.cpuUav = m_rtReflectionTargets.uav;
            clearContext.clearMode = s_rtReflClearMode;
            if (!RTReflectionDispatchPass::ClearOutputForDebugView(clearContext)) {
                setReflectionReadinessReason("reflection_debug_clear_failed");
                return;
            }
        } else {
            spdlog::warn("Renderer: RT reflection dispatch debug clear requested before persistent UAV descriptors were initialized");
        }
    }

    if (const EnvironmentMaps* env = m_environmentState.ActiveEnvironment()) {
        RTReflectionDispatchPass::EnsureTextureNonPixelReadable(rtCmdList.Get(), env->diffuseIrradiance);
        RTReflectionDispatchPass::EnsureTextureNonPixelReadable(rtCmdList.Get(), env->specularPrefiltered);
    }

    // Ensure the descriptor table (space1, t0-t6) is up to date before DXR
    // dispatch. If environments are loaded/evicted asynchronously, the table
    // can otherwise temporarily point at null SRVs.
    UpdateEnvironmentDescriptorTable();

    DescriptorHandle envTable = m_environmentState.shadowAndEnvDescriptors[0];
    D3D12_RESOURCE_DESC reflDesc = m_rtReflectionTargets.color->GetDesc();
    const uint32_t reflW = m_framePlanning.rtPlan.budget.reflectionWidth > 0
        ? std::min(m_framePlanning.rtPlan.budget.reflectionWidth, static_cast<uint32_t>(reflDesc.Width))
        : static_cast<uint32_t>(reflDesc.Width);
    const uint32_t reflH = m_framePlanning.rtPlan.budget.reflectionHeight > 0
        ? std::min(m_framePlanning.rtPlan.budget.reflectionHeight, static_cast<uint32_t>(reflDesc.Height))
        : static_cast<uint32_t>(reflDesc.Height);
    m_rtReflectionReadiness.hasEnvironmentTable = envTable.IsValid();
    m_rtReflectionReadiness.dispatchWidth = reflW;
    m_rtReflectionReadiness.dispatchHeight = reflH;
    m_rtReflectionReadiness.ready =
        m_rtReflectionReadiness.hasPipeline &&
        m_rtReflectionReadiness.hasTLAS &&
        m_rtReflectionReadiness.hasMaterialBuffer &&
        m_rtReflectionReadiness.hasOutput &&
        m_rtReflectionReadiness.hasDepth &&
        m_rtReflectionReadiness.hasNormalRoughness &&
        m_rtReflectionReadiness.hasEnvironmentTable &&
        m_rtReflectionReadiness.hasFrameConstants &&
        m_rtReflectionReadiness.hasDispatchDescriptors &&
        reflW > 0 &&
        reflH > 0;
    if (!m_rtReflectionReadiness.ready) {
        setReflectionReadinessReason("reflection_inputs_incomplete");
        return;
    }

    if (!(rtReflDebugView && s_rtReflSkipDispatch)) {
        m_services.rayTracingContext->DispatchReflections(
            rtCmdList.Get(),
            m_depthResources.descriptors.srv,
            m_rtReflectionTargets.uav,
            m_constantBuffers.currentFrameGPU,
            envTable,
            normalSrv,
            materialExt2Srv,
            normalResource,
            materialExt2Resource,
            reflW,
            reflH);
    } else {
        setReflectionReadinessReason("debug_skip_dxr");
    }

    if (!RTReflectionDispatchPass::FinalizeOutputWrites(rtCmdList.Get(), m_rtReflectionTargets.color.Get())) {
        setReflectionReadinessReason("reflection_output_finalize_failed");
        return;
    }

    m_frameLifecycle.rtReflectionWrittenThisFrame = true;
    if (m_rtReflectionReadiness.reason.empty()) {
        m_rtReflectionReadiness.reason = "ready";
    }
}

} // namespace Cortex::Graphics
