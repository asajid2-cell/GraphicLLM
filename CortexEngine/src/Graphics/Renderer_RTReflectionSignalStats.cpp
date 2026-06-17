#include "Renderer.h"

// Thin forwarders to RTSubsystem. See Graphics/Subsystems/RTSubsystem.
namespace Cortex::Graphics {

void Renderer::CaptureRTReflectionSignalStats() {
    m_rt.CaptureRTReflectionSignalStats(MakeRTContext());
}

void Renderer::CaptureRTReflectionHistorySignalStats() {
    m_rt.CaptureRTReflectionHistorySignalStats(MakeRTContext());
}

void Renderer::UpdateRTReflectionSignalStatsFromReadback() {
    m_rt.UpdateRTReflectionSignalStatsFromReadback(MakeRTContext());
}

} // namespace Cortex::Graphics
