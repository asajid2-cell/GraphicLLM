#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/MotionVectorTargetPass.h"
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
        MotionVectorTargetPass::VelocityUAVContext uavContext{};
        uavContext.commandList = m_commandResources.graphicsList.Get();
        uavContext.velocity = {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState};
        if (!MotionVectorTargetPass::TransitionVelocityToUnorderedAccess(uavContext)) {
            spdlog::warn("VB motion vectors failed; falling back to camera-only: velocity target transition failed");
        } else {
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
    }

    if (!m_pipelineState.motionVectors || !m_depthResources.resources.buffer) {
        return;
    }

    MotionVectorTargetPass::CameraTargetContext cameraTargetContext{};
    cameraTargetContext.commandList = m_commandResources.graphicsList.Get();
    cameraTargetContext.velocity = {m_temporalScreenState.velocityBuffer.Get(), &m_temporalScreenState.velocityState};
    cameraTargetContext.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState};
    cameraTargetContext.depthSampleState = kDepthSampleState;
    if (!MotionVectorTargetPass::TransitionCameraTargets(cameraTargetContext)) {
        spdlog::error("RenderMotionVectors: target transition failed");
        return;
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
