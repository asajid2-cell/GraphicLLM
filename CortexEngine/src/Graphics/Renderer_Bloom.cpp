#include "Renderer.h"
#include "Core/Window.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> Renderer::CreateBloomResources() {
    // Reset existing bloom resources to a known safe state.
    m_bloomTexA.Reset();
    m_bloomTexB.Reset();
    m_bloomRTV[0] = {};
    m_bloomRTV[1] = {};
    m_bloomSRV[0] = {};
    m_bloomSRV[1] = {};
    m_bloomState[0] = D3D12_RESOURCE_STATE_COMMON;
    m_bloomState[1] = D3D12_RESOURCE_STATE_COMMON;

    // For now, keep the implementation minimal and safe: bloom is effectively disabled
    // if no textures are created, and the rest of the renderer will fall back to using
    // the HDR color buffer directly (m_bloomSRV[0] remains invalid).
    spdlog::info("CreateBloomResources: bloom resources reset (stub implementation)");
    return Result<void>::Ok();
}

void Renderer::RenderBloom() {
    // Stubbed-out bloom pass: if bloom textures/pipelines are not set up, do nothing.
    // The main post-process path will detect an invalid bloom SRV and skip bloom.
    if (!m_hdrColor || !m_bloomDownsamplePipeline || !m_bloomBlurHPipeline || !m_bloomBlurVPipeline) {
        return;
    }
}

} // namespace Cortex::Graphics

