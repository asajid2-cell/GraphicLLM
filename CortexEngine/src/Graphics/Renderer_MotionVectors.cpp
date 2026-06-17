#include "Renderer.h"

// Thin forwarder to TemporalSubsystem (motion vectors share temporal screen
// state with TAA). See Graphics/Subsystems/TemporalSubsystem.
namespace Cortex::Graphics {

void Renderer::RenderMotionVectors() {
    m_temporal.RenderMotionVectors(MakeTemporalContext());
}

} // namespace Cortex::Graphics
