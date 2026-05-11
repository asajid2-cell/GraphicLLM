#include "Renderer.h"

#include "Passes/PostProcessTargetPass.h"
#include "Passes/PostProcessPass.h"

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
            {m_ssaoResources.resources.texture.Get(), &m_ssaoResources.resources.resourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_ssrResources.resources.color.Get(), &m_ssrResources.resources.resourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {wantsHzbDebug ? m_hzbResources.resources.texture.Get() : nullptr, &m_hzbResources.resources.resourceState, hzbDebugState},
            {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_temporalScreenState.taaIntermediate.Get(), &m_temporalScreenState.taaIntermediateState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_rtReflectionTargets.color.Get(), &m_rtReflectionTargets.colorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {m_rtReflectionTargets.history.Get(), &m_rtReflectionTargets.historyState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
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
    if (m_rtReflectionTargets.color) {
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
        if (rtReflDebugView && s_rtReflPostClearMode != 0 && m_services.descriptorManager && m_services.device && m_rtReflectionTargets.uav.IsValid()) {
            // Transition to UAV for the clear.
            if (m_rtReflectionTargets.colorState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_rtReflectionTargets.color.Get();
                barrier.Transition.StateBefore = m_rtReflectionTargets.colorState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
                m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            DescriptorHandle clearUav = m_rtReflectionTargets.postClearUAVs[m_frameRuntime.frameIndex % kFrameCount];
            if (clearUav.IsValid()) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                m_services.device->GetDevice()->CreateUnorderedAccessView(
                    m_rtReflectionTargets.color.Get(),
                    nullptr,
                    &uavDesc,
                    clearUav.cpu);

                ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
                m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);

                const float magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
                const float black[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
                const float* clear = (s_rtReflPostClearMode == 2) ? magenta : black;
                // ClearUnorderedAccessView requires a CPU-visible, CPU-readable descriptor handle.
                // Use the persistent staging UAV as the CPU handle and the persistent shader-visible
                // descriptor as the GPU handle.
                m_commandResources.graphicsList->ClearUnorderedAccessViewFloat(
                    clearUav.gpu,
                    m_rtReflectionTargets.uav.cpu,
                    m_rtReflectionTargets.color.Get(),
                    clear,
                    0,
                    nullptr);

                D3D12_RESOURCE_BARRIER clearBarrier{};
                clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                clearBarrier.UAV.pResource = m_rtReflectionTargets.color.Get();
                m_commandResources.graphicsList->ResourceBarrier(1, &clearBarrier);
            } else {
                spdlog::warn("Renderer: RT reflection post debug clear requested before persistent UAV descriptors were initialized");
            }

            // Transition back to SRV for sampling in post-process.
            if (m_rtReflectionTargets.colorState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_rtReflectionTargets.color.Get();
                barrier.Transition.StateBefore = m_rtReflectionTargets.colorState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
                m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
    if (!m_temporalScreenState.postProcessSrvTableValid) {
        spdlog::error("RenderPostProcess: persistent SRV table is invalid");
        return;
    }
    UpdatePostProcessDescriptorTable();
    auto& postTable = m_temporalScreenState.postProcessSrvTables[m_frameRuntime.frameIndex % kFrameCount];
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
