#include "Renderer.h"

// Thin forwarders to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

Result<void> Renderer::CreateRTShadowMask() {
    return m_rt.CreateRTShadowMask(MakeRTContext());
}

Result<void> Renderer::CreateRTGIResources() {
    return m_rt.CreateRTGIResources(MakeRTContext());
}

Result<void> Renderer::CreateRTReflectionResources() {
    return m_rt.CreateRTReflectionResources(MakeRTContext());
}

} // namespace Cortex::Graphics
