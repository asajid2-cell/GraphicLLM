#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

namespace {

void GetTextureSize(ID3D12Resource* resource, uint32_t& width, uint32_t& height) {
    width = 0;
    height = 0;
    if (!resource) {
        return;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        width = static_cast<uint32_t>(desc.Width);
        height = desc.Height;
    }
}

} // namespace
void Renderer::ExecuteRTDenoisePass(const char* frameNormalRoughnessResource) {
    const bool planned =
        m_framePlanning.rtPlan.denoiseShadows ||
        m_framePlanning.rtPlan.denoiseReflections ||
        m_framePlanning.rtPlan.denoiseGI;
    if (!planned) {
        return;
    }

    bool executed = false;
    std::string fallbackReason;

    auto markFallback = [&](const char* reason) {
        if (fallbackReason.empty() && reason) {
            fallbackReason = reason;
        }
    };

    if (!m_services.rtDenoiser || !m_services.rtDenoiser->IsReady() || !m_services.device || !m_services.descriptorManager || !m_commandResources.graphicsList) {
        markFallback("rt_denoiser_not_ready");
    } else if (!m_rtDenoiseState.descriptorTablesValid) {
        markFallback("rt_denoiser_descriptor_tables_unavailable");
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

    if (!normalSrv.IsValid() || !m_depthResources.srv.IsValid() || !m_temporalScreenState.velocitySRV.IsValid() ||
        !m_temporalMaskState.builtThisFrame || !m_temporalMaskState.srv.IsValid()) {
        markFallback("rt_denoiser_missing_inputs");
    }

    if (fallbackReason.empty()) {
        transition(m_depthResources.buffer.Get(), m_depthResources.resourceState, kDepthSampleState);
        if (normalResource && normalState) {
            transition(normalResource, *normalState, kSrvState);
            if (usingVBNormal && m_services.visibilityBuffer) {
                m_services.visibilityBuffer->ApplyResourceStateSnapshot(vbStates);
            }
        }
        transition(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, kSrvState);
        transition(m_temporalMaskState.texture.Get(), m_temporalMaskState.resourceState, kSrvState);

        auto dispatchSignal = [&](RTDenoiser::Signal signal,
                                  bool shouldRun,
                                  bool historyValid,
                                  ID3D12Resource* current,
                                  D3D12_RESOURCE_STATES& currentState,
                                  DescriptorHandle currentSRV,
                                  ID3D12Resource* history,
                                  D3D12_RESOURCE_STATES& historyState,
                                  DescriptorHandle historySRV,
                                  DescriptorHandle historyUAV) -> RTDenoiser::DispatchResult {
            RTDenoiser::DispatchResult result{};
            if (!shouldRun) {
                return result;
            }
            if (!current || !history || !currentSRV.IsValid() || !historySRV.IsValid() || !historyUAV.IsValid()) {
                markFallback("rt_denoiser_missing_signal_resource");
                return result;
            }

            uint32_t width = 0;
            uint32_t height = 0;
            GetTextureSize(history, width, height);
            if (width == 0 || height == 0) {
                markFallback("rt_denoiser_invalid_history_size");
                return result;
            }

            transition(current, currentState, kSrvState);
            transition(history, historyState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            RTDenoiser::DispatchDesc desc{};
            desc.signal = signal;
            desc.historyValid = historyValid;
            desc.width = width;
            desc.height = height;
            desc.frameConstants = m_constantBuffers.currentFrameGPU;
            desc.currentSRV = currentSRV;
            desc.historySRV = historySRV;
            desc.depthSRV = m_depthResources.srv;
            desc.normalRoughnessSRV = normalSrv;
            desc.velocitySRV = m_temporalScreenState.velocitySRV;
            desc.temporalMaskSRV = m_temporalMaskState.srv;
            desc.historyUAV = historyUAV;
            desc.currentResource = current;
            desc.historyResource = history;
            desc.depthResource = m_depthResources.buffer.Get();
            desc.normalRoughnessResource = normalResource;
            desc.velocityResource = m_temporalScreenState.velocityBuffer.Get();
            desc.temporalMaskResource = m_temporalMaskState.texture.Get();
            desc.srvTable = m_rtDenoiseState.srvTables[m_frameRuntime.frameIndex % kFrameCount][0];
            desc.uavTable = m_rtDenoiseState.uavTables[m_frameRuntime.frameIndex % kFrameCount][0];
            if (signal == RTDenoiser::Signal::Reflection) {
                desc.accumulationAlpha = m_rtDenoiseState.reflectionHistoryAlpha;
            }

            result = m_services.rtDenoiser->Dispatch(
                m_commandResources.graphicsList.Get(),
                m_services.device->GetDevice(),
                m_services.descriptorManager.get(),
                desc);

            if (result.executed) {
                D3D12_RESOURCE_BARRIER uavBarrier{};
                uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                uavBarrier.UAV.pResource = history;
                m_commandResources.graphicsList->ResourceBarrier(1, &uavBarrier);
                transition(history, historyState, kSrvState);

                executed = true;
                m_rtDenoiseState.executedThisFrame = true;
                ++m_rtDenoiseState.passCountThisFrame;
                m_rtDenoiseState.usedDepthNormalRejectionThisFrame =
                    m_rtDenoiseState.usedDepthNormalRejectionThisFrame || result.usedDepthNormalRejection;
                m_rtDenoiseState.usedVelocityThisFrame =
                    m_rtDenoiseState.usedVelocityThisFrame || result.usedVelocityReprojection;
                m_rtDenoiseState.usedDisocclusionRejectionThisFrame =
                    m_rtDenoiseState.usedDisocclusionRejectionThisFrame || result.usedDisocclusionRejection;
            }
            return result;
        };

        const RTDenoiser::DispatchResult shadowResult = dispatchSignal(
            RTDenoiser::Signal::Shadow,
            m_framePlanning.rtPlan.denoiseShadows,
            m_temporalHistory.manager.CanReproject(TemporalHistoryId::RTShadow),
            m_rtShadowTargets.mask.Get(),
            m_rtShadowTargets.maskState,
            m_rtShadowTargets.maskSRV,
            m_rtShadowTargets.history.Get(),
            m_rtShadowTargets.historyState,
            m_rtShadowTargets.historySRV,
            m_rtShadowTargets.historyUAV);
        if (shadowResult.executed) {
            m_rtDenoiseState.shadowDenoisedThisFrame = true;
            m_rtDenoiseState.shadowAlpha = shadowResult.accumulationAlpha;
            MarkRTShadowHistoryValid();
        }

        const RTDenoiser::DispatchResult reflectionResult = dispatchSignal(
            RTDenoiser::Signal::Reflection,
            m_framePlanning.rtPlan.denoiseReflections,
            m_temporalHistory.manager.CanReproject(TemporalHistoryId::RTReflection),
            m_rtReflectionTargets.color.Get(),
            m_rtReflectionTargets.colorState,
            m_rtReflectionTargets.srv,
            m_rtReflectionTargets.history.Get(),
            m_rtReflectionTargets.historyState,
            m_rtReflectionTargets.historySRV,
            m_rtReflectionTargets.historyUAV);
        if (reflectionResult.executed) {
            m_rtDenoiseState.reflectionDenoisedThisFrame = true;
            m_rtDenoiseState.reflectionAlpha = reflectionResult.accumulationAlpha;
            MarkRTReflectionHistoryValid();
        }

        const RTDenoiser::DispatchResult giResult = dispatchSignal(
            RTDenoiser::Signal::GI,
            m_framePlanning.rtPlan.denoiseGI,
            m_temporalHistory.manager.CanReproject(TemporalHistoryId::RTGI),
            m_rtGITargets.color.Get(),
            m_rtGITargets.colorState,
            m_rtGITargets.srv,
            m_rtGITargets.history.Get(),
            m_rtGITargets.historyState,
            m_rtGITargets.historySRV,
            m_rtGITargets.historyUAV);
        if (giResult.executed) {
            m_rtDenoiseState.giDenoisedThisFrame = true;
            m_rtDenoiseState.giAlpha = giResult.accumulationAlpha;
            MarkRTGIHistoryValid();
        }
    }

    RecordFramePass("RTDenoise",
                    planned,
                    executed,
                    m_rtDenoiseState.passCountThisFrame,
                    {"depth",
                     frameNormalRoughnessResource ? frameNormalRoughnessResource : "gbuffer_normal_roughness",
                     "velocity",
                     "temporal_rejection_mask",
                     "rt_shadow_mask",
                     "rt_shadow_history",
                     "rt_reflection",
                     "rt_reflection_history",
                     "rt_gi",
                     "rt_gi_history"},
                    {"rt_shadow_history",
                     "rt_reflection_history",
                     "rt_gi_history"},
                    !fallbackReason.empty(),
                    fallbackReason.empty() ? nullptr : fallbackReason.c_str());
}

} // namespace Cortex::Graphics
