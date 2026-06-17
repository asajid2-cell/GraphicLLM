#include "Renderer.h"

#include "VisibilityBuffer.h"

// Thin forwarder to SSRSubsystem. The SSR target/descriptor table and immediate
// draw live in Graphics/Subsystems/SSRSubsystem; ExecuteSSRInRenderGraph stays
// in Renderer and reads m_ssr.State().
namespace Cortex::Graphics {

SSRRenderContext Renderer::MakeSSRRenderContext() {
    SSRRenderContext ctx{};
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.ssrPipeline = m_pipelineState.ssr.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.skipTransitions = m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions;
    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.hdrState = &m_mainTargets.hdr.resources.state;
    ctx.normalRoughness = m_mainTargets.normalRoughness.resources.texture.Get();
    ctx.normalRoughnessState = &m_mainTargets.normalRoughness.resources.state;
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.vbRenderedThisFrame = m_visibilityBufferState.renderedThisFrame;
    ctx.vbNormalRoughness = m_services.visibilityBuffer ? m_services.visibilityBuffer->GetNormalRoughnessBuffer() : nullptr;
    ctx.shadowAndEnvDescriptor = m_environmentState.shadowAndEnvDescriptors[0];
    return ctx;
}

void Renderer::RenderSSR() {
    m_ssr.RenderImmediate(MakeSSRRenderContext());
}

} // namespace Cortex::Graphics
