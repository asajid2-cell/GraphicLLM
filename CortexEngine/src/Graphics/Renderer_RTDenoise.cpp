#include "Renderer.h"

// Thin forwarder to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

void Renderer::ExecuteRTDenoisePass(const char* frameNormalRoughnessResource) {
    m_rt.ExecuteRTDenoisePass(frameNormalRoughnessResource, MakeRTContext());
}

} // namespace Cortex::Graphics
