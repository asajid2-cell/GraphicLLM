#include "Renderer.h"

#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateHDRTarget() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for HDR target creation");
    }

    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    const UINT width  = GetInternalRenderWidth();
    const UINT height = GetInternalRenderHeight();

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create HDR target");
    }

    auto hdrResult = m_mainTargets.hdr.CreateTarget(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height,
        scale,
        [this](HRESULT hr) {
            CORTEX_REPORT_DEVICE_REMOVED("CreateHDRTarget", hr);
        });
    if (hdrResult.IsErr()) {
        return hdrResult;
    }

    spdlog::info("HDR target created: {}x{} (scale {:.2f})", width, height, scale);

    auto normalRoughnessResult = m_mainTargets.normalRoughness.CreateTarget(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (normalRoughnessResult.IsErr()) {
        spdlog::warn("{}", normalRoughnessResult.Error());
    }

    InvalidateTAAHistory("resource_recreated");
    auto historyResult = m_temporalScreenState.CreateHistoryColor(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (historyResult.IsErr()) {
        spdlog::warn("{}", historyResult.Error());
    }

    auto taaIntermediateResult = m_temporalScreenState.CreateTAAIntermediate(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (taaIntermediateResult.IsErr()) {
        spdlog::warn("{}", taaIntermediateResult.Error());
    }

    auto ssrResult = m_ssrResources.resources.CreateTarget(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (ssrResult.IsErr()) {
        spdlog::warn("{}", ssrResult.Error());
    }

    auto velocityResult = m_temporalScreenState.CreateVelocityBuffer(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (velocityResult.IsErr()) {
        spdlog::warn("{}", velocityResult.Error());
    }

    auto temporalMaskResult = CreateTemporalRejectionMaskResources();
    if (temporalMaskResult.IsErr()) {
        spdlog::warn("Failed to create temporal rejection mask: {}", temporalMaskResult.Error());
    }

    // (Re)create bloom render targets that depend on HDR size
    auto bloomResult = CreateBloomResources();
    if (bloomResult.IsErr()) {
        spdlog::warn("Failed to create bloom resources: {}", bloomResult.Error());
    }

    // SSAO target depends on window size as well
    auto ssaoResult = CreateSSAOResources();
    if (ssaoResult.IsErr()) {
        spdlog::warn("Failed to create SSAO resources: {}", ssaoResult.Error());
    }

    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
