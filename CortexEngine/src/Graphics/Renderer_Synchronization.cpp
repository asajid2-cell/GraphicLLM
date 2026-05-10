#include "Renderer.h"

namespace Cortex::Graphics {

bool Renderer::IsCommandListOpen() const {
    return m_frameRuntime.commandListOpen;
}

void Renderer::WaitForAllFrames() {
    // Wait for ALL in-flight frames to complete, not just the current one.
    // With triple buffering, frames N-1 and N-2 might still be executing
    // and holding references to resources we're about to delete.
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_frameRuntime.fenceValues[i] > 0 && m_services.commandQueue) {
            if (!m_services.commandQueue->WaitForFenceValue(m_frameRuntime.fenceValues[i])) {
                spdlog::error("WaitForAllFrames: timed out waiting for graphics frame fence {}", m_frameRuntime.fenceValues[i]);
                m_frameLifecycle.deviceRemoved = true;
                return;
            }
        }
    }

    // Also flush any pending upload work
    if (m_services.uploadQueue) {
        m_services.uploadQueue->Flush();
    }
}

void Renderer::ResetCommandList() {
    // If we are mid-frame when a scene change occurs, the command list might
    // reference objects we are about to delete. We need to:
    // 1. Wait for ALL in-flight frames (not just current one) to complete
    // 2. Reset ALL command allocators to clear internal resource references
    // 3. Reset the command list with a fresh allocator
    // 4. Clear pending GPU jobs that hold raw pointers
    //
    // NOTE: BLAS cache and mesh asset keys are NOT cleared here - they are
    // cleared separately by the scene rebuild process to avoid timing issues
    // with the command list still referencing BLAS resources.

    if (!m_commandResources.graphicsList || !m_services.commandQueue) {
        return;
    }

    // Step 1: Wait for ALL in-flight GPU work to complete
    // This ensures no GPU operations are still using resources we'll delete.
    WaitForAllFrames();

    // Step 2: Close the command list if it's open, then reset ALL allocators.
    // We reset ALL allocators because we don't know which one the command list
    // was using when it recorded RT commands.
    if (m_frameRuntime.commandListOpen) {
        m_commandResources.graphicsList->Close();
        m_frameRuntime.commandListOpen = false;
    }

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_commandResources.graphicsAllocators[i]) {
            m_commandResources.graphicsAllocators[i]->Reset();
        }
    }

    // Step 3: Reset the command list with a fresh allocator
    // This clears all internal references - the command list is now EMPTY
    if (m_frameRuntime.frameIndex < kFrameCount && m_commandResources.graphicsAllocators[m_frameRuntime.frameIndex]) {
        m_commandResources.graphicsList->Reset(m_commandResources.graphicsAllocators[m_frameRuntime.frameIndex].Get(), nullptr);
        m_frameRuntime.commandListOpen = true;
    }

    // Step 4: Clear pending GPU jobs that contain raw pointers to mesh data.
    m_assetRuntime.gpuJobs.Clear();
}

void Renderer::WaitForGPU() {
    // Block until the main graphics queue has finished all submitted work so
    // large reallocations (depth/HDR/RT targets) do not temporarily overlap
    // with resources still in use on the GPU. This is used sparingly, only
    // on resolution changes, to avoid unnecessary stalls.
    if (m_services.commandQueue) {
        m_services.commandQueue->Flush();
    }
    if (m_services.uploadQueue) {
        m_services.uploadQueue->Flush();
    }
    if (m_services.computeQueue) {
        m_services.computeQueue->Flush();
    }
}

} // namespace Cortex::Graphics
