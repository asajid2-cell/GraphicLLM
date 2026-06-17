#include "Renderer.h"

// Thin forwarder to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

void Renderer::RenderRayTracedReflections() {
    m_rt.RenderRayTracedReflections(MakeRTContext());
}

} // namespace Cortex::Graphics
