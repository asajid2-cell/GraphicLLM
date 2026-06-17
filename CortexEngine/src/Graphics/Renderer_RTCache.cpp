#include "Renderer.h"

// Thin forwarder to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

void Renderer::ClearBLASCache() {
    m_rt.ClearBLASCache(MakeRTContext());
}

} // namespace Cortex::Graphics
