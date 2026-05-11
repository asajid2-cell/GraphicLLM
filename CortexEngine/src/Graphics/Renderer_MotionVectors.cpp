#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/MotionVectorPass.h"

#include <span>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::RenderMotionVectors() {
    if (!m_temporalScreenState.velocityBuffer) {
        return;
    }

    // When the visibility-buffer path is active, compute per-object motion vectors
    // from VB + barycentrics (better stability for TAA/SSR/RT).
    if (m_visibilityBufferState.enabled && m_services.visibilityBuffer && !m_visibilityBufferState.meshDraws.empty() && !m_visibilityBufferState.instances.empty()) {
        // Transition velocity buffer for UAV writes.
        if (m_temporalScreenState.velocityState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_temporalScreenState.velocityBuffer.Get();
            barrier.Transition.StateBefore = m_temporalScreenState.velocityState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        auto mvResult = m_services.visibilityBuffer->ComputeMotionVectors(
            m_commandResources.graphicsList.Get(),
            m_temporalScreenState.velocityBuffer.Get(),
            m_visibilityBufferState.meshDraws,
            m_constantBuffers.currentFrameGPU
        );
        if (mvResult.IsErr()) {
            spdlog::warn("VB motion vectors failed; falling back to camera-only: {}", mvResult.Error());
        } else {
            return;
        }
    }

    if (!m_pipelineState.motionVectors || !m_depthResources.resources.buffer) {
        return;
    }

    // Transition resources
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT barrierCount = 0;

    if (m_temporalScreenState.velocityState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_temporalScreenState.velocityBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_temporalScreenState.velocityState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (m_depthResources.resources.resourceState != kDepthSampleState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthResources.resources.buffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthResources.resources.resourceState;
        barriers[barrierCount].Transition.StateAfter = kDepthSampleState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_depthResources.resources.resourceState = kDepthSampleState;
    }

    if (barrierCount > 0) {
        m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
    }

    if (!m_temporalScreenState.motionVectorSrvTableValid) {
        spdlog::error("RenderMotionVectors: persistent SRV table is invalid");
        return;
    }
    auto& persistentTable = m_temporalScreenState.motionVectorSrvTables[m_frameRuntime.frameIndex % kFrameCount];

    if (!MotionVectorPass::Draw({
            m_services.device->GetDevice(),
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.motionVectors.get(),
            m_temporalScreenState.velocityBuffer.Get(),
            m_temporalScreenState.velocityRTV,
            m_depthResources.resources.buffer.Get(),
            std::span<DescriptorHandle>(persistentTable.data(), persistentTable.size()),
        })) {
        spdlog::error("RenderMotionVectors: pass draw failed");
        return;
    }

    // Motion vectors will be sampled in post-process
    m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

} // namespace Cortex::Graphics
