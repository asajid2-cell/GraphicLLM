#include "Renderer.h"

#include "Scene/ECS_Registry.h"

namespace Cortex::Graphics {

void Renderer::BeginFrameForEditor() {
    BeginFrame();
}

void Renderer::EndFrameForEditor() {
    EndFrame();
}

void Renderer::PrepareMainPassForEditor() {
    PrepareMainPass();
}

void Renderer::UpdateFrameConstantsForEditor(float deltaTime, Scene::ECS_Registry* registry) {
    UpdateFrameConstants(deltaTime, registry);
}

void Renderer::RenderSkyboxForEditor() {
    RenderSkybox();
}

void Renderer::RenderShadowPassForEditor(Scene::ECS_Registry* registry) {
    RenderShadowPass(registry);
}

void Renderer::RenderSceneForEditor(Scene::ECS_Registry* registry) {
    RenderScene(registry);
}

void Renderer::RenderSSAOForEditor() {
    RenderSSAO();
}

void Renderer::RenderBloomForEditor() {
    RenderBloom();
}

void Renderer::RenderPostProcessForEditor() {
    RenderPostProcess();
}

void Renderer::RenderDebugLinesForEditor() {
    RenderDebugLines();
}

void Renderer::RenderTAAForEditor() {
    RenderTAA();
}

void Renderer::RenderSSRForEditor() {
    RenderSSR();
}

void Renderer::PrewarmMaterialDescriptorsForEditor(Scene::ECS_Registry* registry) {
    PrewarmMaterialDescriptors(registry);
}

} // namespace Cortex::Graphics