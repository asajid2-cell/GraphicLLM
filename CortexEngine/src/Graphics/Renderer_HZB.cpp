#include "Renderer.h"

#include "RenderGraph.h"

// Thin forwarders to HZBSubsystem. HZB pyramid resources and the depth
// downsample build now live in Graphics/Subsystems/HZBSubsystem.
namespace Cortex::Graphics {

HZBContext Renderer::MakeHZBContext() {
    HZBContext ctx{};
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.depthSrv = m_depthResources.descriptors.srv;
    ctx.compactRootSignature = m_pipelineState.singleSrvUavComputeRootSignature.Get();
    ctx.fallbackRootSignature = m_pipelineState.computeRootSignature.get();
    ctx.initPipeline = m_pipelineState.hzbInit.get();
    ctx.downsamplePipeline = m_pipelineState.hzbDownsample.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.captureViewMatrix = m_constantBuffers.frameCPU.viewMatrix;
    ctx.captureViewProjMatrix = m_constantBuffers.frameCPU.viewProjectionMatrix;
    ctx.captureCameraPosWS = m_cameraState.positionWS;
    ctx.captureCameraForwardWS = m_cameraState.forwardWS;
    ctx.captureNearPlane = m_cameraState.nearPlane;
    ctx.captureFarPlane = m_cameraState.farPlane;
    ctx.captureFrameCounter = m_frameLifecycle.renderFrameCounter;
    return ctx;
}

Result<void> Renderer::CreateHZBResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_depthResources.resources.buffer) {
        return Result<void>::Err("CreateHZBResources: renderer not initialized or depth buffer missing");
    }
    return m_hzb.CreateResources(MakeHZBContext());
}

void Renderer::BuildHZBFromDepth() {
    m_hzb.BuildFromDepth(MakeHZBContext());
}

void Renderer::AddHZBFromDepthPasses_RG(RenderGraph& graph, RGResourceHandle depthHandle, RGResourceHandle hzbHandle) {
    m_hzb.AddFromDepthPasses(graph, depthHandle, hzbHandle, MakeHZBContext());
}

} // namespace Cortex::Graphics
