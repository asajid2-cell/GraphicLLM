#include "Renderer.h"

#include "VisibilityBuffer.h"

// Thin forwarders to TemporalSubsystem. Motion-vector + TAA screen/AA state and
// the MV/TAA passes live in Graphics/Subsystems/TemporalSubsystem. The temporal-
// history manager (shared with RT) and the visibility buffer are injected; the
// render-graph temporal paths stay in Renderer.
namespace Cortex::Graphics {

TemporalContext Renderer::MakeTemporalContext() {
    TemporalContext ctx{};
    ctx.device = m_services.device;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.taaPipeline = m_pipelineState.taa.get();
    ctx.motionVectorsPipeline = m_pipelineState.motionVectors.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.hasWindow = (m_services.window != nullptr);
    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.hdrState = &m_mainTargets.hdr.resources.state;
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.normalRoughness = m_mainTargets.normalRoughness.resources.texture.Get();
    ctx.normalRoughnessState = &m_mainTargets.normalRoughness.resources.state;
    ctx.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    ctx.maskTexture = m_temporalMaskState.texture.Get();
    ctx.maskState = &m_temporalMaskState.resourceState;
    ctx.historyManager = &m_temporalHistory.manager;
    ctx.renderFrameCounter = m_frameLifecycle.renderFrameCounter;
    ctx.visibilityBuffer = m_services.visibilityBuffer.get();
    ctx.vbState = &m_visibilityBufferState;
    ctx.updateResolveTable = [this]() { UpdateTAAResolveDescriptorTable(); };
    return ctx;
}

void Renderer::RenderTAA() {
    m_temporal.RenderTAA(MakeTemporalContext());
}

} // namespace Cortex::Graphics
