#include "Renderer.h"

#include "Scene/ECS_Registry.h"

// Thin forwarder to VisibilityBufferSubsystem. See Graphics/Subsystems/VisibilityBufferSubsystem.
namespace Cortex::Graphics {

void Renderer::CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry) {
    m_vb.CollectInstancesForVisibilityBuffer(registry, MakeVisibilityBufferContext());
}

} // namespace Cortex::Graphics
