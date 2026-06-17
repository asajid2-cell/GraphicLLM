#include "Renderer.h"

#include "Scene/ECS_Registry.h"

// Thin forwarders to VisibilityBufferSubsystem. See Graphics/Subsystems/VisibilityBufferSubsystem.
namespace Cortex::Graphics {

Renderer::VisibilityBufferDeferredLightingInputs
Renderer::PrepareVisibilityBufferDeferredLighting(Scene::ECS_Registry* registry) {
    return m_vb.PrepareVisibilityBufferDeferredLighting(registry, MakeVisibilityBufferContext());
}

void Renderer::ApplyVisibilityBufferDeferredLighting(const VisibilityBufferDeferredLightingInputs& inputs) {
    m_vb.ApplyVisibilityBufferDeferredLighting(inputs, MakeVisibilityBufferContext());
}

void Renderer::RenderVisibilityBufferDeferredLightingStage(Scene::ECS_Registry* registry) {
    m_vb.RenderVisibilityBufferDeferredLightingStage(registry, MakeVisibilityBufferContext());
}

} // namespace Cortex::Graphics
