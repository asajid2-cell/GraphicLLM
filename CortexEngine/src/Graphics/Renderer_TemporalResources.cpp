#include "Renderer.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateTemporalRejectionMaskResources() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for temporal rejection mask creation");
    }

    const UINT width = GetInternalRenderWidth();
    const UINT height = GetInternalRenderHeight();
    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create temporal rejection mask");
    }

    m_frameDiagnostics.contract.temporalMask = {};
    auto result = m_temporalMaskState.CreateResources(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height,
        [this](const char* context, HRESULT hr) {
            CORTEX_REPORT_DEVICE_REMOVED(context, hr);
        });
    if (result.IsErr()) {
        return result;
    }

    spdlog::info("Temporal rejection mask created: {}x{}", width, height);
    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
