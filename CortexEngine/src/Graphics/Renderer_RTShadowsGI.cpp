#include "Renderer.h"

#include "Scene/ECS_Registry.h"

// Thin forwarder to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

void Renderer::RenderRayTracing(Scene::ECS_Registry* registry) {
    m_rt.RenderRayTracing(registry, MakeRTContext());
}

} // namespace Cortex::Graphics
