#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

// Thin forwarder to WaterSubsystem. Water/liquid CPU params and the forward
// liquid overlay draw live in Graphics/Subsystems/WaterSubsystem. Material prep
// and constant allocation are injected as services (the material system stays a
// Renderer-provided service: it cascades into the texture-upload/asset chain).
namespace Cortex::Graphics {

WaterRenderContext Renderer::MakeWaterRenderContext() {
    WaterRenderContext ctx{};
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.rootSignature = m_pipelineState.rootSignature ? m_pipelineState.rootSignature->GetRootSignature() : nullptr;
    ctx.waterPipelineState = m_pipelineState.waterOverlay ? m_pipelineState.waterOverlay->GetPipelineState() : nullptr;
    ctx.cbvSrvUavHeap = m_services.descriptorManager ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap() : nullptr;
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.shadowEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
    ctx.hdrColor = m_mainTargets.hdr.resources.color.Get();
    ctx.hdrState = &m_mainTargets.hdr.resources.state;
    ctx.hdrRtv = m_mainTargets.hdr.descriptors.rtv;
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.depthDsv = m_depthResources.descriptors.dsv;
    ctx.readOnlyDepthDsv = m_depthResources.descriptors.readOnlyDsv;
    ctx.viewProjectionNoJitter = m_constantBuffers.frameCPU.viewProjectionNoJitter;
    ctx.sceneSnapshot = &m_framePlanning.sceneSnapshot;
    ctx.renderFrameCounter = m_frameLifecycle.renderFrameCounter;
    ctx.materialFallbackTable = m_materialFallbacks.descriptorTable[0];
    ctx.prepareMaterial = [this](Scene::RenderableComponent& r) { PrepareMaterialResources(r); };
    ctx.allocObjectConstants = [this](const ObjectConstants& o) { return m_constantBuffers.object.AllocateAndWrite(o); };
    ctx.allocMaterialConstants = [this](const MaterialConstants& m) { return m_constantBuffers.material.AllocateAndWrite(m); };
    ctx.outWaterDraws = &m_frameDiagnostics.contract.drawCounts.waterDraws;
    return ctx;
}

void Renderer::RenderWaterSurfaces(Scene::ECS_Registry* registry) {
    m_water.Render(registry, MakeWaterRenderContext());
}

} // namespace Cortex::Graphics
