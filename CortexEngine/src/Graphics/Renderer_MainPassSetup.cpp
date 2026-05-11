#include "Renderer.h"

#include "Core/Window.h"
#include "Graphics/Passes/MainPassTargetPass.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::PrepareMainPass() {
    MainPassTargetPass::PrepareContext context{};
    context.commandList = m_commandResources.graphicsList.Get();
    context.descriptorManager = m_services.descriptorManager.get();
    context.rootSignature = m_pipelineState.rootSignature.get();
    context.geometryPipeline = m_pipelineState.geometry.get();
    context.depthBuffer = m_depthResources.resources.buffer.Get();
    context.depthState = &m_depthResources.resources.resourceState;
    context.depthDsv = m_depthResources.descriptors.dsv;
    context.rtShadowMask = m_rtShadowTargets.mask.Get();
    context.rtShadowMaskState = &m_rtShadowTargets.maskState;
    context.rtGIColor = m_rtGITargets.color.Get();
    context.rtGIColorState = &m_rtGITargets.colorState;
    context.hdrColor = m_mainTargets.hdr.resources.color.Get();
    context.hdrState = &m_mainTargets.hdr.resources.state;
    context.hdrRtv = m_mainTargets.hdr.descriptors.rtv;
    context.normalRoughness = m_mainTargets.normalRoughness.resources.texture.Get();
    context.normalRoughnessState = &m_mainTargets.normalRoughness.resources.state;
    context.normalRoughnessRtv = m_mainTargets.normalRoughness.descriptors.rtv;
    context.backBuffer = m_services.window ? m_services.window->GetCurrentBackBuffer() : nullptr;
    context.backBufferRtv = m_services.window ? DescriptorHandle{m_services.window->GetCurrentRTV()} : DescriptorHandle{};
    context.backBufferWidth = m_services.window ? m_services.window->GetWidth() : 0;
    context.backBufferHeight = m_services.window ? m_services.window->GetHeight() : 0;
    context.backBufferUsedAsRTThisFrame = &m_frameLifecycle.backBufferUsedAsRTThisFrame;

    if (!MainPassTargetPass::Prepare(context)) {
        spdlog::error("PrepareMainPass: target setup failed; skipping frame");
    }
}

} // namespace Cortex::Graphics
