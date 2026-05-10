#include "Renderer.h"
#include "Core/Window.h"
#include "Debug/GPUProfiler.h"
#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Cortex::Graphics {

void Renderer::BeginFrame() {
    // Handle window/internal-resolution changes. The renderer owns a single
    // internal render size used by HDR, depth, SSAO and RT screen-space
    // targets; post-process resolves that image to the swap chain.
    const float renderScale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    const UINT expectedDepthWidth  = GetInternalRenderWidth();
    const UINT expectedDepthHeight = GetInternalRenderHeight();

    bool needDepthResize = false;
    bool needHDRResize   = false;
    bool needSSAOResize  = false;

    // Reset per-frame back-buffer state tracking; individual passes that
    // render directly to the swap-chain will set this when they transition
    // the back buffer from PRESENT to RENDER_TARGET.
    m_frameLifecycle.backBufferUsedAsRTThisFrame = false;

    // Reset per-frame RT reflection write flag so history updates only occur
    // on frames where the DXR reflections pass actually ran.
    m_frameLifecycle.rtReflectionWrittenThisFrame = false;
    m_rtDenoiseState.ResetFrame();
    m_temporalMaskState.builtThisFrame = false;
    m_frameDiagnostics.contract.temporalMask.built = false;
    m_rtReflectionSignalState.ResetFrame();
    m_rtReflectionReadiness.ResetFrame();

    // Wait for this frame's command allocator/descriptor segment to be available
    m_frameRuntime.frameIndex = m_services.window->GetCurrentBackBufferIndex();
    m_temporalHistory.manager.BeginFrame(m_frameLifecycle.renderFrameCounter);

    // Notify visibility buffer of current frame index for triple-buffered resources
    if (m_services.visibilityBuffer) {
        m_services.visibilityBuffer->SetFrameIndex(m_frameRuntime.frameIndex);
    }

    // Notify GPU culling of current frame index for triple-buffered visibility mask
    if (m_services.gpuCulling) {
        m_services.gpuCulling->SetFrameIndex(m_frameRuntime.frameIndex);
    }

    if (m_frameRuntime.fenceValues[m_frameRuntime.frameIndex] != 0) {
        uint64_t completedValue = m_services.commandQueue->GetLastCompletedFenceValue();
        uint64_t expectedValue = m_frameRuntime.fenceValues[m_frameRuntime.frameIndex];

        if (completedValue < expectedValue) {
            spdlog::debug("BeginFrame waiting for GPU: frameIndex={}, expected={}, completed={}, delta={}",
                          m_frameRuntime.frameIndex, expectedValue, completedValue, expectedValue - completedValue);
        }

        if (!m_services.commandQueue->WaitForFenceValue(m_frameRuntime.fenceValues[m_frameRuntime.frameIndex])) {
            spdlog::error("BeginFrame: timed out waiting for graphics frame fence; marking device removed");
            m_frameLifecycle.deviceRemoved = true;
            return;
        }
    }

    // Process deferred GPU resource deletion queue.
    // This releases resources that were queued for deletion N frames ago,
    // ensuring they are no longer referenced by any in-flight command lists.
    // This is the standard D3D12 pattern for safe resource lifetime management.
    DeferredGPUDeletionQueue::Instance().ProcessFrame();

    if (m_services.gpuCulling) {
        m_services.gpuCulling->UpdateVisibleCountFromReadback();
    }
    UpdateTemporalRejectionMaskStatsFromReadback();
    UpdateRTReflectionSignalStatsFromReadback();

    if (m_services.descriptorManager) {
        m_services.descriptorManager->BeginFrame(m_frameRuntime.frameIndex);
    }

    if (m_depthResources.buffer) {
        D3D12_RESOURCE_DESC depthDesc = m_depthResources.buffer->GetDesc();
        if (depthDesc.Width != expectedDepthWidth || depthDesc.Height != expectedDepthHeight) {
            needDepthResize = true;
        }
    }

    if (m_mainTargets.hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdrColor->GetDesc();
        if (hdrDesc.Width != expectedDepthWidth || hdrDesc.Height != expectedDepthHeight) {
            needHDRResize = true;
        }
    }

    // Check SSAO resize against the active budget profile. Balanced and
    // 4 GB profiles use half resolution; the 2 GB profile uses quarter
    // resolution to keep SSAO enabled without reallocating every frame.
    if (m_ssaoResources.texture) {
        D3D12_RESOURCE_DESC ssaoDesc = m_ssaoResources.texture->GetDesc();
        const auto budget = BudgetPlanner::BuildPlan(
            m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
            expectedDepthWidth,
            expectedDepthHeight);
        const UINT ssaoDivisor = std::max<UINT>(1, budget.ssaoDivisor);
        UINT expectedWidth  = std::max<UINT>(1, expectedDepthWidth  / ssaoDivisor);
        UINT expectedHeight = std::max<UINT>(1, expectedDepthHeight / ssaoDivisor);
        if (ssaoDesc.Width != expectedWidth || ssaoDesc.Height != expectedHeight) {
            needSSAOResize = true;
        }
    }

    // CRITICAL: Wait for GPU before destroying ANY render targets
    if ((needDepthResize || needHDRResize || needSSAOResize) && !m_frameLifecycle.deviceRemoved) {
        spdlog::info("BeginFrame: reallocating render targets for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        // Must wait for GPU to finish using old resources before destroying them
        // Normal frame fencing is NOT sufficient - Debug Layer proves we need explicit sync here
        WaitForGPU();
    }

    if (needDepthResize && m_depthResources.buffer) {
        spdlog::info("BeginFrame: recreating depth buffer for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        m_depthResources.buffer.Reset();
        auto depthResult = CreateDepthBuffer();
        if (depthResult.IsErr()) {
            spdlog::error("Failed to recreate depth buffer on resize: {}", depthResult.Error());
            // Treat this as a fatal condition for the current run.
            m_frameLifecycle.deviceRemoved = true;
            return;
        }
    }

    // Handle HDR target resize using the same effective render resolution.
    if (needHDRResize && m_mainTargets.hdrColor) {
        spdlog::info("BeginFrame: recreating HDR target for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        m_mainTargets.hdrColor.Reset();
        auto hdrResult = CreateHDRTarget();
        if (hdrResult.IsErr()) {
            spdlog::error("Failed to recreate HDR target on resize: {}", hdrResult.Error());
            m_frameLifecycle.deviceRemoved = true;
            return;
        }

        auto rtMaskResult = CreateRTShadowMask();
        if (rtMaskResult.IsErr()) {
            spdlog::warn("Failed to recreate RT shadow mask on resize: {}", rtMaskResult.Error());
        }

        if (m_rtRuntimeState.supported && m_services.rayTracingContext) {
            auto rtReflResult = CreateRTReflectionResources();
            if (rtReflResult.IsErr()) {
                spdlog::warn("Failed to recreate RT reflection buffer on resize: {}", rtReflResult.Error());
            }

            auto rtGiResult = CreateRTGIResources();
            if (rtGiResult.IsErr()) {
                spdlog::warn("Failed to recreate RT GI buffer on resize: {}", rtGiResult.Error());
            }
        }
    }
    // Handle SSAO target resize.
    if (needSSAOResize && m_ssaoResources.texture) {
        spdlog::info("BeginFrame: recreating SSAO target for active budget profile");
        m_ssaoResources.texture.Reset();
        auto ssaoResult = CreateSSAOResources();
        if (ssaoResult.IsErr()) {
            spdlog::error("Failed to recreate SSAO target on resize: {}", ssaoResult.Error());
            m_ssaoResources.enabled = false;
        }
    }
    // Propagate resize to ray tracing context so it can adjust any RT targets.
    if (m_services.rayTracingContext && m_services.window) {
        m_services.rayTracingContext->OnResize(expectedDepthWidth, expectedDepthHeight);
    }

    // Resize visibility buffer
    if (m_services.visibilityBuffer && m_services.window) {
        auto vbResizeResult = m_services.visibilityBuffer->Resize(expectedDepthWidth, expectedDepthHeight);
        if (vbResizeResult.IsErr()) {
            spdlog::warn("VisibilityBuffer resize failed: {}", vbResizeResult.Error());
        }
    }

    // Reset dynamic constant buffer offsets to the current frame's region
    // Each frame index has its own region in the buffer (triple-buffering)
    // to prevent overwriting data that the GPU is still reading
    m_constantBuffers.object.ResetOffset(m_frameRuntime.frameIndex);
    m_constantBuffers.material.ResetOffset(m_frameRuntime.frameIndex);
    m_constantBuffers.shadow.ResetOffset(m_frameRuntime.frameIndex);

    // Ensure outstanding uploads are complete before reusing upload allocator
    if (m_services.uploadQueue) {
        for (uint64_t fence : m_uploadCommands.fences) {
            if (fence != 0 && !m_services.uploadQueue->IsFenceComplete(fence)) {
                if (!m_services.uploadQueue->WaitForFenceValue(fence)) {
                    spdlog::error("BeginFrame: timed out waiting for upload queue fence {}; marking device removed", fence);
                    m_frameLifecycle.deviceRemoved = true;
                    return;
                }
            }
        }
    }
    std::fill(m_uploadCommands.fences.begin(), m_uploadCommands.fences.end(), 0);
    m_uploadCommands.pendingFence = 0;
    for (uint32_t i = 0; i < UploadCommandPoolState::kPoolSize; ++i) {
        if (m_uploadCommands.commandAllocators[i]) {
            m_uploadCommands.commandAllocators[i]->Reset();
        }
        if (m_uploadCommands.commandLists[i]) {
            m_uploadCommands.commandLists[i]->Reset(m_uploadCommands.commandAllocators[i].Get(), nullptr);
            m_uploadCommands.commandLists[i]->Close();
        }
    }

    // Increment the absolute frame index. This is used for tracking BLAS build
    // timing to ensure scratch buffers aren't released while the GPU is still
    // using them.
    ++m_frameRuntime.absoluteFrameIndex;

    // Now that the previous frame's GPU work is complete, release any BLAS
    // scratch buffers that were used for acceleration structure builds.
    // With triple buffering, when we've waited for the fence associated with
    // the current frame index,
    // frame (m_frameRuntime.absoluteFrameIndex - kFrameCount) is guaranteed complete.
    // We subtract kFrameCount to be safe: if we're at frame N, frames < N-2
    // have definitely finished.
    if (m_services.rayTracingContext) {
        uint64_t completedFrame = (m_frameRuntime.absoluteFrameIndex > kFrameCount)
            ? (m_frameRuntime.absoluteFrameIndex - kFrameCount)
            : 0;
        m_services.rayTracingContext->ReleaseScratchBuffers(completedFrame);
    }

    // Reset command allocator and list.
    // If the command list is already open (e.g., after ResetCommandList during scene switch),
    // we need to close it first before resetting the allocator.
    if (m_frameRuntime.commandListOpen) {
        m_commandResources.graphicsList->Close();
        m_frameRuntime.commandListOpen = false;
    }
    m_commandResources.graphicsAllocators[m_frameRuntime.frameIndex]->Reset();
    m_commandResources.graphicsList->Reset(m_commandResources.graphicsAllocators[m_frameRuntime.frameIndex].Get(), nullptr);
    m_frameRuntime.commandListOpen = true;

    // Root signature uses CBV/SRV/UAV heap direct indexing; bind heaps once
    // immediately after Reset() so subsequent Set*RootSignature calls satisfy
    // D3D12 validation (and so compute/RT paths inherit a valid heap binding).
    if (m_services.descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
        m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    }
}

} // namespace Cortex::Graphics
