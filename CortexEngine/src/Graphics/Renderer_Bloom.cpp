#include "Renderer.h"

// Thin forwarders to BloomSubsystem. The bloom pyramid resources and the
// immediate downsample/blur/composite chain live in
// Graphics/Subsystems/BloomSubsystem. The render-graph bloom paths
// (ExecuteBloomInRenderGraph + the fused end-frame-graph bloom) stay in the
// renderer and read m_bloom.State().
namespace Cortex::Graphics {

BloomContext Renderer::MakeBloomContext() {
    BloomContext ctx{};
    ctx.device = m_services.device;
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.downsample = m_pipelineState.bloomDownsample.get();
    ctx.blurH = m_pipelineState.bloomBlurH.get();
    ctx.blurV = m_pipelineState.bloomBlurV.get();
    ctx.composite = m_pipelineState.bloomComposite.get();
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.internalWidth = GetInternalRenderWidth();
    ctx.internalHeight = GetInternalRenderHeight();
    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.hdrState = &m_mainTargets.hdr.resources.state;
    ctx.hdrSrvValid = m_mainTargets.hdr.descriptors.srv.IsValid();
    return ctx;
}

Result<void> Renderer::CreateBloomResources() {
    return m_bloom.CreateResources(MakeBloomContext());
}

void Renderer::RenderBloom() {
    m_bloom.Render(MakeBloomContext());
}

} // namespace Cortex::Graphics
