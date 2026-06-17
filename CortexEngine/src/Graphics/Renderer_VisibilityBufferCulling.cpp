#include "Renderer.h"

// Thin forwarders to VisibilityBufferSubsystem. See Graphics/Subsystems/VisibilityBufferSubsystem.
namespace Cortex::Graphics {

uint32_t Renderer::GetVisibilityBufferDebugView() const {
    return m_vb.GetVisibilityBufferDebugView(m_debugViewState.mode);
}

D3D12_GPU_VIRTUAL_ADDRESS Renderer::ResolveVisibilityBufferCullMask(uint32_t debugView) {
    return m_vb.ResolveVisibilityBufferCullMask(debugView, MakeVisibilityBufferContext());
}

} // namespace Cortex::Graphics
