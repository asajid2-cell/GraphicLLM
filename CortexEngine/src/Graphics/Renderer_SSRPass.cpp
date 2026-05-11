#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/SSRPass.h"

#include <span>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::RenderSSR() {
    if (!m_pipelineState.ssr || !m_ssrResources.resources.color || !m_mainTargets.hdr.resources.color || !m_depthResources.resources.buffer) {
        return;
    }

    ID3D12Resource* normalResource = m_mainTargets.normalRoughness.resources.texture.Get();
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }
    if (!normalResource) {
        return;
    }

    SSRPass::PrepareContext prepareContext{};
    prepareContext.commandList = m_commandResources.graphicsList.Get();
    prepareContext.skipTransitions = m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions;
    prepareContext.ssrTarget = {m_ssrResources.resources.color.Get(), &m_ssrResources.resources.resourceState, D3D12_RESOURCE_STATE_RENDER_TARGET};
    prepareContext.hdr = {m_mainTargets.hdr.resources.color.Get(), &m_mainTargets.hdr.resources.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    prepareContext.normalRoughness = {
        m_visibilityBufferState.renderedThisFrame ? nullptr : m_mainTargets.normalRoughness.resources.texture.Get(),
        m_visibilityBufferState.renderedThisFrame ? nullptr : &m_mainTargets.normalRoughness.resources.state,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    prepareContext.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState, kDepthSampleState};
    if (!SSRPass::PrepareTargets(prepareContext)) {
        spdlog::error("RenderSSR: target transition failed");
        return;
    }

    if (!m_ssrResources.descriptors.srvTableValid) {
        spdlog::error("RenderSSR: persistent SRV table is invalid");
        return;
    }

    auto& persistentTable = m_ssrResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];
    if (!SSRPass::Draw({
            m_services.device->GetDevice(),
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.ssr.get(),
            m_ssrResources.resources.color.Get(),
            m_ssrResources.resources.rtv,
            m_mainTargets.hdr.resources.color.Get(),
            m_depthResources.resources.buffer.Get(),
            normalResource,
            std::span<DescriptorHandle>(persistentTable.data(), persistentTable.size()),
            m_environmentState.shadowAndEnvDescriptors[0],
        })) {
        spdlog::error("RenderSSR: pass execution failed");
    }
}

} // namespace Cortex::Graphics
