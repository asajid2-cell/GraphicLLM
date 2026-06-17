#include "Renderer.h"

// Thin forwarder to VisibilityBufferSubsystem. See Graphics/Subsystems/VisibilityBufferSubsystem.
namespace Cortex::Graphics {

void Renderer::LogVisibilityBufferFirstFrame() {
    m_vb.LogVisibilityBufferFirstFrame();
}

} // namespace Cortex::Graphics
