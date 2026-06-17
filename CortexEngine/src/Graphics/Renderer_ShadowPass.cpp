#include "Renderer.h"

#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

// Thin forwarder to ShadowSubsystem. Shadow map state (cascades + local),
// cascade frame state, and the shadow depth pass live in
// Graphics/Subsystems/ShadowSubsystem. Material prep and the environment
// descriptor-table update are injected as services.
namespace Cortex::Graphics {

ShadowContext Renderer::MakeShadowContext() {
    ShadowContext ctx{};
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.device = m_services.device;
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.windowWidth = m_services.window ? m_services.window->GetWidth() : 0;
    ctx.windowHeight = m_services.window ? m_services.window->GetHeight() : 0;
    ctx.rootSignature = m_pipelineState.rootSignature ? m_pipelineState.rootSignature->GetRootSignature() : nullptr;
    ctx.cbvSrvUavHeap = m_services.descriptorManager ? m_services.descriptorManager->GetCBV_SRV_UAV_Heap() : nullptr;
    ctx.shadow = m_pipelineState.shadow.get();
    ctx.shadowDoubleSided = m_pipelineState.shadowDoubleSided.get();
    ctx.shadowAlpha = m_pipelineState.shadowAlpha.get();
    ctx.shadowAlphaDoubleSided = m_pipelineState.shadowAlphaDoubleSided.get();
    ctx.shadowPipelineValid = (m_pipelineState.shadow != nullptr);
    ctx.objectConstants = &m_constantBuffers.object;
    ctx.materialConstants = &m_constantBuffers.material;
    ctx.shadowConstants = &m_constantBuffers.shadow;
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.materialFallbacks = {
        m_materialFallbacks.albedo.get(),
        m_materialFallbacks.normal.get(),
        m_materialFallbacks.metallic.get(),
        m_materialFallbacks.roughness.get()
    };
    ctx.skipTransitions = m_frameDiagnostics.renderGraph.transitions.shadowPassSkipTransitions;
    ctx.sceneSnapshot = &m_framePlanning.sceneSnapshot;
    ctx.renderFrameCounter = m_frameLifecycle.renderFrameCounter;
    ctx.outShadowDraws = &m_frameDiagnostics.contract.drawCounts.shadowDraws;
    ctx.prepareMaterial = [this](Scene::RenderableComponent& r) { PrepareMaterialResources(r); };
    ctx.updateEnvironmentTable = [this]() { UpdateEnvironmentDescriptorTable(); };
    return ctx;
}

void Renderer::RenderShadowPass(Scene::ECS_Registry* registry) {
    m_shadows.RenderPass(registry, MakeShadowContext());
}

} // namespace Cortex::Graphics
