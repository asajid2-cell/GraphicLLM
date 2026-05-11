#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Passes/SSRPass.h"

#include <span>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::RenderSSR() {
    if (!m_pipelineState.ssr || !m_ssrResources.resources.color || !m_mainTargets.hdrColor || !m_depthResources.resources.buffer) {
        return;
    }

    ID3D12Resource* normalResource = m_mainTargets.gbufferNormalRoughness.Get();
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
    }
    if (!normalResource) {
        return;
    }

    // Transition resources to appropriate states
    D3D12_RESOURCE_BARRIER barriers[4] = {};
    UINT barrierCount = 0;

    if (!m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions &&
        m_ssrResources.resources.resourceState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_ssrResources.resources.color.Get();
        barriers[barrierCount].Transition.StateBefore = m_ssrResources.resources.resourceState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
    }

    if (!m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions &&
        m_mainTargets.hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_mainTargets.hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_mainTargets.hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
    }

    if (!m_visibilityBufferState.renderedThisFrame) {
        if (!m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions &&
            m_mainTargets.gbufferNormalRoughness &&
            m_mainTargets.gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_mainTargets.gbufferNormalRoughness.Get();
            barriers[barrierCount].Transition.StateBefore = m_mainTargets.gbufferNormalRoughnessState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }
    }

    if (!m_frameDiagnostics.renderGraph.transitions.ssrSkipTransitions && m_depthResources.resources.resourceState != kDepthSampleState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthResources.resources.buffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthResources.resources.resourceState;
        barriers[barrierCount].Transition.StateAfter = kDepthSampleState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
    }

    if (barrierCount > 0) {
        m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
    }

    m_ssrResources.resources.resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_mainTargets.hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (!m_visibilityBufferState.renderedThisFrame && m_mainTargets.gbufferNormalRoughness) {
        m_mainTargets.gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    m_depthResources.resources.resourceState = kDepthSampleState;

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
            m_mainTargets.hdrColor.Get(),
            m_depthResources.resources.buffer.Get(),
            normalResource,
            std::span<DescriptorHandle>(persistentTable.data(), persistentTable.size()),
            m_environmentState.shadowAndEnvDescriptors[0],
        })) {
        spdlog::error("RenderSSR: pass execution failed");
    }
}

} // namespace Cortex::Graphics
