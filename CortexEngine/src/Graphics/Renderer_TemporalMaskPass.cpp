#include "Renderer.h"
#include "Graphics/RendererGeometryUtils.h"

#include <cstdint>

namespace Cortex::Graphics {

void Renderer::BuildTemporalRejectionMask(const char* frameNormalRoughnessResource,
                                          bool skipTransitions,
                                          bool renderGraphOwned) {
    m_temporalMaskState.builtThisFrame = false;
    if (!m_services.temporalRejectionMask || !m_services.temporalRejectionMask->IsReady() ||
        !m_temporalMaskState.texture || !m_temporalMaskState.uav.IsValid() ||
        !m_depthResources.resources.buffer || !m_depthResources.descriptors.srv.IsValid() ||
        !m_temporalScreenState.velocityBuffer || !m_temporalScreenState.velocitySRV.IsValid() ||
        !m_services.device || !m_services.descriptorManager || !m_commandResources.graphicsList ||
        !m_temporalMaskState.descriptorTablesValid) {
        return;
    }

    DescriptorHandle normalSrv = m_mainTargets.gbufferNormalRoughnessSRV;
    ID3D12Resource* normalResource = m_mainTargets.gbufferNormalRoughness.Get();
    D3D12_RESOURCE_STATES* normalState = &m_mainTargets.gbufferNormalRoughnessState;
    VisibilityBufferRenderer::ResourceStateSnapshot vbStates{};
    bool usingVBNormal = false;
    if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
        const DescriptorHandle& vbNormalSrv = m_services.visibilityBuffer->GetNormalRoughnessSRVHandle();
        if (vbNormalSrv.IsValid()) {
            vbStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            normalSrv = vbNormalSrv;
            normalResource = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
            normalState = &vbStates.normalRoughness;
            usingVBNormal = true;
        }
    }
    if (!normalSrv.IsValid() || !normalResource || !normalState) {
        return;
    }

    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    auto transition = [&](ID3D12Resource* resource,
                          D3D12_RESOURCE_STATES& state,
                          D3D12_RESOURCE_STATES desired) {
        if (!resource || state == desired) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = desired;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        state = desired;
    };

    if (!skipTransitions) {
        transition(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, kDepthSampleState);
        transition(normalResource, *normalState, kSrvState);
        if (usingVBNormal && m_services.visibilityBuffer) {
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(vbStates);
        }
        transition(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, kSrvState);
        transition(m_temporalMaskState.texture.Get(),
                   m_temporalMaskState.resourceState,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    } else {
        m_depthResources.resources.resourceState = kDepthSampleState;
        *normalState = kSrvState;
        if (usingVBNormal && m_services.visibilityBuffer) {
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(vbStates);
        }
        m_temporalScreenState.velocityState = kSrvState;
        m_temporalMaskState.resourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    const D3D12_RESOURCE_DESC maskDesc = m_temporalMaskState.texture->GetDesc();
    if (maskDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        width = static_cast<uint32_t>(maskDesc.Width);
        height = maskDesc.Height;
    }

    TemporalRejectionMask::DispatchDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.frameConstants = m_constantBuffers.currentFrameGPU;
    desc.depthSRV = m_depthResources.descriptors.srv;
    desc.normalRoughnessSRV = normalSrv;
    desc.velocitySRV = m_temporalScreenState.velocitySRV;
    desc.outputUAV = m_temporalMaskState.uav;
    desc.depthResource = m_depthResources.resources.buffer.Get();
    desc.normalRoughnessResource = normalResource;
    desc.velocityResource = m_temporalScreenState.velocityBuffer.Get();
    desc.outputResource = m_temporalMaskState.texture.Get();
    desc.srvTable = m_temporalMaskState.srvTables[m_frameRuntime.frameIndex % kFrameCount][0];
    desc.uavTable = m_temporalMaskState.uavTables[m_frameRuntime.frameIndex % kFrameCount][0];

    const bool executed = m_services.temporalRejectionMask->Dispatch(
        m_commandResources.graphicsList.Get(),
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        desc);
    if (!executed) {
        return;
    }

    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_temporalMaskState.texture.Get();
    m_commandResources.graphicsList->ResourceBarrier(1, &uavBarrier);
    if (!skipTransitions) {
        transition(m_temporalMaskState.texture.Get(), m_temporalMaskState.resourceState, kSrvState);
    }
    m_temporalMaskState.builtThisFrame = true;
    if (!renderGraphOwned) {
        CaptureTemporalRejectionMaskStats();
    }

    RecordFramePass("TemporalRejectionMask",
                    true,
                    true,
                    1,
                    {"depth", frameNormalRoughnessResource ? frameNormalRoughnessResource : "normal_roughness", "velocity"},
                    {"temporal_rejection_mask"},
                    false,
                    nullptr,
                    renderGraphOwned);
}

void Renderer::CaptureTemporalRejectionMaskStats() {
    if (!m_temporalMaskState.builtThisFrame ||
        !m_services.temporalRejectionMask || !m_services.temporalRejectionMask->IsReady() ||
        !m_temporalMaskState.texture || !m_temporalMaskState.srv.IsValid() ||
        !m_temporalMaskState.statsBuffer || !m_temporalMaskState.statsUAV.IsValid() ||
        m_frameRuntime.frameIndex >= kFrameCount ||
        !m_temporalMaskState.statsReadback[m_frameRuntime.frameIndex] ||
        !m_services.device || !m_services.descriptorManager || !m_commandResources.graphicsList) {
        return;
    }
    if (!m_temporalMaskState.statsDescriptorTablesValid ||
        !m_temporalMaskState.statsSrvTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid() ||
        !m_temporalMaskState.statsUavTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid()) {
        return;
    }

    const D3D12_RESOURCE_DESC maskDesc = m_temporalMaskState.texture->GetDesc();
    const uint32_t width = static_cast<uint32_t>(maskDesc.Width);
    const uint32_t height = maskDesc.Height;
    if (width == 0 || height == 0) {
        return;
    }

    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    auto transition = [&](ID3D12Resource* resource,
                          D3D12_RESOURCE_STATES& state,
                          D3D12_RESOURCE_STATES desired) {
        if (!resource || state == desired) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = state;
        barrier.Transition.StateAfter = desired;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        state = desired;
    };

    transition(m_temporalMaskState.texture.Get(), m_temporalMaskState.resourceState, kSrvState);
    transition(m_temporalMaskState.statsBuffer.Get(),
               m_temporalMaskState.statsState,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    TemporalRejectionMask::StatsDispatchDesc statsDesc{};
    statsDesc.width = width;
    statsDesc.height = height;
    statsDesc.maskSRV = m_temporalMaskState.srv;
    statsDesc.statsUAV = m_temporalMaskState.statsUAV;
    statsDesc.maskResource = m_temporalMaskState.texture.Get();
    statsDesc.statsResource = m_temporalMaskState.statsBuffer.Get();
    statsDesc.srvTable = m_temporalMaskState.statsSrvTables[m_frameRuntime.frameIndex % kFrameCount][0];
    statsDesc.uavTable = m_temporalMaskState.statsUavTables[m_frameRuntime.frameIndex % kFrameCount][0];

    const bool executed = m_services.temporalRejectionMask->DispatchStats(
        m_commandResources.graphicsList.Get(),
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        statsDesc);
    if (!executed) {
        return;
    }

    D3D12_RESOURCE_BARRIER statsUavBarrier{};
    statsUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    statsUavBarrier.UAV.pResource = m_temporalMaskState.statsBuffer.Get();
    m_commandResources.graphicsList->ResourceBarrier(1, &statsUavBarrier);

    transition(m_temporalMaskState.statsBuffer.Get(),
               m_temporalMaskState.statsState,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_commandResources.graphicsList->CopyBufferRegion(
        m_temporalMaskState.statsReadback[m_frameRuntime.frameIndex].Get(),
        0,
        m_temporalMaskState.statsBuffer.Get(),
        0,
        32);

    m_temporalMaskState.statsReadbackPending[m_frameRuntime.frameIndex] = true;
    m_temporalMaskState.statsSampleFrame[m_frameRuntime.frameIndex] = m_frameLifecycle.renderFrameCounter;

    RecordFramePass("TemporalRejectionMaskStats",
                    true,
                    true,
                    0,
                    {"temporal_rejection_mask"},
                    {"temporal_rejection_mask_stats"},
                    false,
                    nullptr,
                    false);
}

} // namespace Cortex::Graphics
