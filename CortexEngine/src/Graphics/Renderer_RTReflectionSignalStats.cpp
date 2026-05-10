#include "Renderer.h"

#include <algorithm>
#include <cstdint>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::CaptureRTReflectionSignalStats() {
    if (!m_frameLifecycle.rtReflectionWrittenThisFrame ||
        !m_services.rtReflectionSignalStats || !m_services.rtReflectionSignalStats->IsReady() ||
        !m_rtReflectionTargets.color || !m_rtReflectionTargets.srv.IsValid() ||
        !m_rtReflectionSignalState.rawStatsBuffer || !m_rtReflectionSignalState.rawStatsUAV.IsValid() ||
        m_frameRuntime.frameIndex >= kFrameCount ||
        !m_rtReflectionSignalState.rawReadback[m_frameRuntime.frameIndex] ||
        !m_services.device || !m_services.descriptorManager || !m_commandResources.graphicsList) {
        return;
    }
    if (!m_rtReflectionSignalState.descriptorTablesValid ||
        !m_rtReflectionSignalState.descriptorSrvTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid() ||
        !m_rtReflectionSignalState.descriptorUavTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid()) {
        return;
    }

    const D3D12_RESOURCE_DESC reflectionDesc = m_rtReflectionTargets.color->GetDesc();
    const uint32_t width = static_cast<uint32_t>(reflectionDesc.Width);
    const uint32_t height = reflectionDesc.Height;
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

    transition(m_rtReflectionTargets.color.Get(), m_rtReflectionTargets.colorState, kSrvState);
    transition(m_rtReflectionSignalState.rawStatsBuffer.Get(),
               m_rtReflectionSignalState.rawStatsResourceState,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RTReflectionSignalStats::DispatchDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.target = RTReflectionSignalStats::SignalTarget::Raw;
    desc.reflectionSRV = m_rtReflectionTargets.srv;
    desc.statsUAV = m_rtReflectionSignalState.rawStatsUAV;
    desc.reflectionResource = m_rtReflectionTargets.color.Get();
    desc.statsResource = m_rtReflectionSignalState.rawStatsBuffer.Get();
    desc.srvTable = m_rtReflectionSignalState.descriptorSrvTables[m_frameRuntime.frameIndex % kFrameCount][0];
    desc.uavTable = m_rtReflectionSignalState.descriptorUavTables[m_frameRuntime.frameIndex % kFrameCount][0];

    const bool executed = m_services.rtReflectionSignalStats->Dispatch(
        m_commandResources.graphicsList.Get(),
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        desc);
    if (!executed) {
        return;
    }

    D3D12_RESOURCE_BARRIER statsUavBarrier{};
    statsUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    statsUavBarrier.UAV.pResource = m_rtReflectionSignalState.rawStatsBuffer.Get();
    m_commandResources.graphicsList->ResourceBarrier(1, &statsUavBarrier);

    transition(m_rtReflectionSignalState.rawStatsBuffer.Get(),
               m_rtReflectionSignalState.rawStatsResourceState,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_commandResources.graphicsList->CopyBufferRegion(
        m_rtReflectionSignalState.rawReadback[m_frameRuntime.frameIndex].Get(),
        0,
        m_rtReflectionSignalState.rawStatsBuffer.Get(),
        0,
        RTReflectionSignalStats::kStatsBytes);

    m_rtReflectionSignalState.rawReadbackPending[m_frameRuntime.frameIndex] = true;
    m_rtReflectionSignalState.rawSampleFrame[m_frameRuntime.frameIndex] = m_frameLifecycle.renderFrameCounter;
    m_rtReflectionSignalState.rawCapturedThisFrame = true;

    RecordFramePass("RTReflectionSignalStats",
                    true,
                    true,
                    0,
                    {"rt_reflection"},
                    {"rt_reflection_signal_stats"});
}

void Renderer::CaptureRTReflectionHistorySignalStats() {
    if (!m_rtDenoiseState.reflectionDenoisedThisFrame ||
        !m_services.rtReflectionSignalStats || !m_services.rtReflectionSignalStats->IsReady() ||
        !m_rtReflectionTargets.history || !m_rtReflectionTargets.historySRV.IsValid() ||
        !m_rtReflectionSignalState.historyStatsBuffer || !m_rtReflectionSignalState.historyStatsUAV.IsValid() ||
        m_frameRuntime.frameIndex >= kFrameCount ||
        !m_rtReflectionSignalState.historyReadback[m_frameRuntime.frameIndex] ||
        !m_services.device || !m_services.descriptorManager || !m_commandResources.graphicsList) {
        return;
    }
    if (!m_rtReflectionSignalState.descriptorTablesValid ||
        !m_rtReflectionSignalState.descriptorSrvTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid() ||
        !m_rtReflectionSignalState.descriptorUavTables[m_frameRuntime.frameIndex % kFrameCount][0].IsValid()) {
        return;
    }

    const D3D12_RESOURCE_DESC reflectionDesc = m_rtReflectionTargets.history->GetDesc();
    const uint32_t width = static_cast<uint32_t>(reflectionDesc.Width);
    const uint32_t height = reflectionDesc.Height;
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

    transition(m_rtReflectionTargets.history.Get(), m_rtReflectionTargets.historyState, kSrvState);
    transition(m_rtReflectionSignalState.historyStatsBuffer.Get(),
               m_rtReflectionSignalState.historyStatsResourceState,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RTReflectionSignalStats::DispatchDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.target = RTReflectionSignalStats::SignalTarget::History;
    desc.reflectionSRV = m_rtReflectionTargets.historySRV;
    desc.statsUAV = m_rtReflectionSignalState.historyStatsUAV;
    desc.reflectionResource = m_rtReflectionTargets.history.Get();
    desc.statsResource = m_rtReflectionSignalState.historyStatsBuffer.Get();
    desc.srvTable = m_rtReflectionSignalState.descriptorSrvTables[m_frameRuntime.frameIndex % kFrameCount][0];
    desc.uavTable = m_rtReflectionSignalState.descriptorUavTables[m_frameRuntime.frameIndex % kFrameCount][0];

    const bool executed = m_services.rtReflectionSignalStats->Dispatch(
        m_commandResources.graphicsList.Get(),
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        desc);
    if (!executed) {
        return;
    }

    D3D12_RESOURCE_BARRIER statsUavBarrier{};
    statsUavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    statsUavBarrier.UAV.pResource = m_rtReflectionSignalState.historyStatsBuffer.Get();
    m_commandResources.graphicsList->ResourceBarrier(1, &statsUavBarrier);

    transition(m_rtReflectionSignalState.historyStatsBuffer.Get(),
               m_rtReflectionSignalState.historyStatsResourceState,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_commandResources.graphicsList->CopyBufferRegion(
        m_rtReflectionSignalState.historyReadback[m_frameRuntime.frameIndex].Get(),
        0,
        m_rtReflectionSignalState.historyStatsBuffer.Get(),
        0,
        RTReflectionSignalStats::kStatsBytes);

    m_rtReflectionSignalState.historyReadbackPending[m_frameRuntime.frameIndex] = true;
    m_rtReflectionSignalState.historySampleFrame[m_frameRuntime.frameIndex] = m_frameLifecycle.renderFrameCounter;
    m_rtReflectionSignalState.historyCapturedThisFrame = true;

    RecordFramePass("RTReflectionHistorySignalStats",
                    true,
                    true,
                    0,
                    {"rt_reflection_history"},
                    {"rt_reflection_history_signal_stats"});
}

void Renderer::UpdateRTReflectionSignalStatsFromReadback() {
    if (m_frameRuntime.frameIndex >= kFrameCount) {
        return;
    }

    constexpr float kStatsScale = 256.0f;
    const D3D12_RANGE readRange{0, RTReflectionSignalStats::kStatsBytes};
    const D3D12_RANGE writeRange{0, 0};

    auto readStats = [&](ID3D12Resource* readback,
                         bool& pending,
                         const char* label,
                         uint32_t& pixels,
                         float& avgLuma,
                         float& maxLuma,
                         float& nonZeroRatio,
                         float& brightRatio,
                         float& outlierRatio) -> bool {
        if (!pending || !readback) {
            return false;
        }

        uint32_t* mapped = nullptr;
        HRESULT hr = readback->Map(
            0,
            &readRange,
            reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            spdlog::warn("{}: failed to map readback buffer", label);
            pending = false;
            return false;
        }

        const uint32_t lumaSumQ = mapped[0];
        const uint32_t nonZero = mapped[1];
        const uint32_t bright = mapped[2];
        const uint32_t mappedPixels = mapped[3];
        const uint32_t maxLumaQ = mapped[4];
        const uint32_t outliers = mapped[5];
        readback->Unmap(0, &writeRange);
        pending = false;

        pixels = mappedPixels;
        if (pixels == 0) {
            avgLuma = 0.0f;
            maxLuma = 0.0f;
            nonZeroRatio = 0.0f;
            brightRatio = 0.0f;
            outlierRatio = 0.0f;
            return false;
        }

        const float pixelDenom = static_cast<float>(pixels);
        avgLuma = std::max(0.0f, static_cast<float>(lumaSumQ) / (pixelDenom * kStatsScale));
        maxLuma = std::max(0.0f, static_cast<float>(maxLumaQ) / kStatsScale);
        nonZeroRatio = std::clamp(static_cast<float>(nonZero) / pixelDenom, 0.0f, 1.0f);
        brightRatio = std::clamp(static_cast<float>(bright) / pixelDenom, 0.0f, 1.0f);
        outlierRatio = std::clamp(static_cast<float>(outliers) / pixelDenom, 0.0f, 1.0f);
        return true;
    };

    uint32_t pixels = 0;
    float avgLuma = 0.0f;
    float maxLuma = 0.0f;
    float nonZeroRatio = 0.0f;
    float brightRatio = 0.0f;
    float outlierRatio = 0.0f;
    const bool rawHadPendingReadback =
        m_rtReflectionSignalState.rawReadbackPending[m_frameRuntime.frameIndex] &&
        m_rtReflectionSignalState.rawReadback[m_frameRuntime.frameIndex];
    const bool rawValid = readStats(
        m_rtReflectionSignalState.rawReadback[m_frameRuntime.frameIndex].Get(),
        m_rtReflectionSignalState.rawReadbackPending[m_frameRuntime.frameIndex],
        "RT reflection signal stats",
        pixels,
        avgLuma,
        maxLuma,
        nonZeroRatio,
        brightRatio,
        outlierRatio);
    if (rawValid) {
        m_rtReflectionSignalState.raw.valid = true;
        m_rtReflectionSignalState.raw.sampleFrame = m_rtReflectionSignalState.rawSampleFrame[m_frameRuntime.frameIndex];
        m_rtReflectionSignalState.raw.pixelCount = pixels;
        m_rtReflectionSignalState.raw.avgLuma = avgLuma;
        m_rtReflectionSignalState.raw.maxLuma = maxLuma;
        m_rtReflectionSignalState.raw.nonZeroRatio = nonZeroRatio;
        m_rtReflectionSignalState.raw.brightRatio = brightRatio;
        m_rtReflectionSignalState.raw.outlierRatio = outlierRatio;
        m_rtReflectionSignalState.raw.readbackLatencyFrames =
            (m_frameLifecycle.renderFrameCounter >= m_rtReflectionSignalState.raw.sampleFrame)
                ? static_cast<uint32_t>(m_frameLifecycle.renderFrameCounter - m_rtReflectionSignalState.raw.sampleFrame)
                : 0u;
    } else if (rawHadPendingReadback && pixels == 0) {
        m_rtReflectionSignalState.raw.valid = false;
        m_rtReflectionSignalState.raw.pixelCount = 0;
    }

    uint32_t historyPixels = 0;
    float historyAvgLuma = 0.0f;
    float historyMaxLuma = 0.0f;
    float historyNonZeroRatio = 0.0f;
    float historyBrightRatio = 0.0f;
    float historyOutlierRatio = 0.0f;
    const bool historyHadPendingReadback =
        m_rtReflectionSignalState.historyReadbackPending[m_frameRuntime.frameIndex] &&
        m_rtReflectionSignalState.historyReadback[m_frameRuntime.frameIndex];
    const bool historyValid = readStats(
        m_rtReflectionSignalState.historyReadback[m_frameRuntime.frameIndex].Get(),
        m_rtReflectionSignalState.historyReadbackPending[m_frameRuntime.frameIndex],
        "RT reflection history signal stats",
        historyPixels,
        historyAvgLuma,
        historyMaxLuma,
        historyNonZeroRatio,
        historyBrightRatio,
        historyOutlierRatio);
    if (historyValid) {
        m_rtReflectionSignalState.history.valid = true;
        m_rtReflectionSignalState.history.sampleFrame = m_rtReflectionSignalState.historySampleFrame[m_frameRuntime.frameIndex];
        m_rtReflectionSignalState.history.pixelCount = historyPixels;
        m_rtReflectionSignalState.history.avgLuma = historyAvgLuma;
        m_rtReflectionSignalState.history.maxLuma = historyMaxLuma;
        m_rtReflectionSignalState.history.nonZeroRatio = historyNonZeroRatio;
        m_rtReflectionSignalState.history.brightRatio = historyBrightRatio;
        m_rtReflectionSignalState.history.outlierRatio = historyOutlierRatio;
        m_rtReflectionSignalState.history.readbackLatencyFrames =
            (m_frameLifecycle.renderFrameCounter >= m_rtReflectionSignalState.history.sampleFrame)
                ? static_cast<uint32_t>(m_frameLifecycle.renderFrameCounter - m_rtReflectionSignalState.history.sampleFrame)
                : 0u;
    } else if (historyHadPendingReadback && historyPixels == 0) {
        m_rtReflectionSignalState.history.valid = false;
        m_rtReflectionSignalState.history.pixelCount = 0;
    }

    if (m_rtReflectionSignalState.raw.valid && m_rtReflectionSignalState.history.valid) {
        m_rtReflectionSignalState.history.avgLumaDelta =
            m_rtReflectionSignalState.history.avgLuma - m_rtReflectionSignalState.raw.avgLuma;
    }
}

} // namespace Cortex::Graphics
