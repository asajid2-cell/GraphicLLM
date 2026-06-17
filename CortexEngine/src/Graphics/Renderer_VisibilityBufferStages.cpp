#include "Renderer.h"

// Thin forwarders to VisibilityBufferSubsystem. See Graphics/Subsystems/VisibilityBufferSubsystem.
namespace Cortex::Graphics {

bool Renderer::RenderVisibilityBufferVisibilityStage(D3D12_GPU_VIRTUAL_ADDRESS cullMaskAddress,
                                                     uint32_t debugView,
                                                     bool& completedPath) {
    return m_vb.RenderVisibilityBufferVisibilityStage(cullMaskAddress, debugView, completedPath, MakeVisibilityBufferContext());
}

bool Renderer::RenderVisibilityBufferMaterialResolveStage(uint32_t debugView, bool& completedPath) {
    return m_vb.RenderVisibilityBufferMaterialResolveStage(debugView, completedPath, MakeVisibilityBufferContext());
}

} // namespace Cortex::Graphics
