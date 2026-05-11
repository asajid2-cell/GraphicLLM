#include "Renderer.h"

#include "Graphics/Passes/DepthPrepassTargetPass.h"

#include <algorithm>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateDepthBuffer() {
    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    return DepthPrepassTargetPass::CreateResources({
        m_services.device ? m_services.device->GetDevice() : nullptr,
        m_services.descriptorManager.get(),
        &m_depthResources,
        GetInternalRenderWidth(),
        GetInternalRenderHeight(),
        scale,
        [this](HRESULT hr) {
            CORTEX_REPORT_DEVICE_REMOVED("CreateDepthBuffer", hr);
        }});
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
