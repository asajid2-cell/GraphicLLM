#include "Renderer.h"

// Thin forwarders to ShadowSubsystem for shadow-map resource lifecycle.
namespace Cortex::Graphics {

Result<void> Renderer::CreateShadowMapResources() {
    return m_shadows.CreateResources(MakeShadowContext());
}

void Renderer::RecreateShadowMapResourcesForCurrentSize() {
    m_shadows.RecreateForCurrentSize(MakeShadowContext());
}

} // namespace Cortex::Graphics
