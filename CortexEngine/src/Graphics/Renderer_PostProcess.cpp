#include "Renderer.h"

#include "Passes/PostProcessTargetPass.h"
#include "Passes/PostProcessPass.h"
#include "Passes/RTReflectionDebugClearPass.h"

#include <cstdlib>

namespace Cortex::Graphics {

void Renderer::RenderPostProcess() {
    if (!m_pipelineState.postProcess || !m_mainTargets.hdr.resources.color) {
        // No HDR/post-process configured; main pass may have rendered directly to back buffer
        return;
    }

    if (m_frameDiagnostics.renderGraph.transitions.postProcessSkipTransitions) {
        // RenderGraph is responsible for resource transitions in this mode.
        m_frameLifecycle.backBufferUsedAsRTThisFrame = true;
    } else {
        const bool wantsHzbDebug = (m_debugViewState.mode == 32u);
        const D3D12_RESOURCE_STATES hzbDebugState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const PostProcessTargetPass::ResourceStateRef shaderResources[] = {
            {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_ssao.State().resources.texture.Get(), &m_ssao.State().resources.resourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_ssr.State().resources.color.Get(), &m_ssr.State().resources.resourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {wantsHzbDebug ? m_hzb.State().resources.texture.Get() : nullptr, &m_hzb.State().resources.resourceState, hzbDebugState},
            {m_temporal.ScreenState().velocityBuffer.Get(), &m_temporal.ScreenState().velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_temporal.ScreenState().taaIntermediate.Get(), &m_temporal.ScreenState().taaIntermediateState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_rt.ReflectionTargets().color.Get(), &m_rt.ReflectionTargets().colorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_rt.ReflectionTargets().history.Get(), &m_rt.ReflectionTargets().historyState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
        };
        PostProcessTargetPass::PrepareContext prepareContext{};
        prepareContext.commandList = m_commandResources.graphicsList.Get();
        prepareContext.shaderResources = shaderResources;
        prepareContext.shaderResourceCount = sizeof(shaderResources) / sizeof(shaderResources[0]);
        prepareContext.backBuffer = m_services.window->GetCurrentBackBuffer();
        prepareContext.backBufferUsedAsRenderTarget = &m_frameLifecycle.backBufferUsedAsRTThisFrame;
        if (!PostProcessTargetPass::PrepareInputsAndBackBuffer(prepareContext)) {
            spdlog::error("RenderPostProcess: target transition failed");
            return;
        }
    }

    // Optional diagnostic clear for RT reflections: this runs even when the DXR
    // reflection dispatch is disabled so debug view 20 can validate SRV binding.
    // NOTE: This is gated behind env vars and debug view modes; it should not
    // affect normal rendering.
    if (m_rt.ReflectionTargets().color) {
        static bool s_checkedRtReflPostClear = false;
        static int  s_rtReflPostClearMode = 0;
        if (!s_checkedRtReflPostClear) {
            s_checkedRtReflPostClear = true;
            if (const char* mode = std::getenv("CORTEX_RTREFL_CLEAR")) {
                s_rtReflPostClearMode = std::atoi(mode);
                if (s_rtReflPostClearMode != 0) {
                    spdlog::warn("Renderer: CORTEX_RTREFL_CLEAR={} set; post-process will clear RT reflection buffer for debug view validation",
                                 s_rtReflPostClearMode);
                }
            }
        }

        const bool rtReflDebugView =
            (m_debugViewState.mode == 20u || m_debugViewState.mode == 30u || m_debugViewState.mode == 31u);
        if (rtReflDebugView && s_rtReflPostClearMode != 0 && m_services.descriptorManager && m_services.device && m_rt.ReflectionTargets().uav.IsValid()) {
            DescriptorHandle clearUav = m_rt.ReflectionTargets().postClearUAVs[m_frameRuntime.frameIndex % kFrameCount];
            if (clearUav.IsValid()) {
                RTReflectionDebugClearPass::ClearContext clearContext{};
                clearContext.commandList = m_commandResources.graphicsList.Get();
                clearContext.device = m_services.device->GetDevice();
                clearContext.descriptorHeap = m_services.descriptorManager->GetCBV_SRV_UAV_Heap();
                clearContext.reflectionColor = m_rt.ReflectionTargets().color.Get();
                clearContext.reflectionState = &m_rt.ReflectionTargets().colorState;
                clearContext.shaderVisibleUav = clearUav;
                clearContext.cpuUav = m_rt.ReflectionTargets().uav;
                clearContext.clearMode = s_rtReflPostClearMode;
                if (!RTReflectionDebugClearPass::ClearForDebugView(clearContext)) {
                    spdlog::warn("Renderer: RT reflection post debug clear failed");
                }
            } else {
                spdlog::warn("Renderer: RT reflection post debug clear requested before persistent UAV descriptors were initialized");
            }
        }
    }

    // Bind a stable SRV table for the post-process shader (t0..t12). The shader
    // samples many slots unconditionally (e.g., RT reflections), so the table
    // must keep fixed slot indices even when certain features are disabled.
    if (!m_mainTargets.hdr.descriptors.srv.IsValid()) {
        spdlog::error("RenderPostProcess: HDR SRV is invalid");
        return;
    }
    if (!m_temporal.ScreenState().postProcessSrvTableValid) {
        spdlog::error("RenderPostProcess: persistent SRV table is invalid");
        return;
    }
    UpdatePostProcessDescriptorTable();
    auto& postTable = m_temporal.ScreenState().postProcessSrvTables[m_frameRuntime.frameIndex % kFrameCount];
    if (!PostProcessPass::Draw({
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.postProcess.get(),
            m_services.window->GetWidth(),
            m_services.window->GetHeight(),
            m_services.window->GetCurrentRTV(),
            std::span<DescriptorHandle>(postTable.data(), postTable.size()),
            m_environmentState.shadowAndEnvDescriptors[0],
        })) {
        spdlog::error("RenderPostProcess: pass draw failed");
    }
}

} // namespace Cortex::Graphics
