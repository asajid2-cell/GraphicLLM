#include "Renderer.h"
#include "Core/Window.h"
#include "Debug/GPUProfiler.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/Passes/ReadbackBuffer.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Cortex::Graphics {

void Renderer::UpdateTemporalRejectionMaskStatsFromReadback() {
    if (m_frameRuntime.frameIndex >= kFrameCount ||
        !m_temporalMaskState.statsReadbackPending[m_frameRuntime.frameIndex] ||
        !m_temporalMaskState.statsReadback[m_frameRuntime.frameIndex]) {
        return;
    }

    constexpr SIZE_T kReadBytes = 5u * sizeof(uint32_t);
    const D3D12_RANGE readRange{0, kReadBytes};
    auto mappedReadback = ReadbackBuffer::MapRange(
        m_temporalMaskState.statsReadback[m_frameRuntime.frameIndex].Get(),
        readRange,
        "Temporal rejection mask stats");
    if (!mappedReadback.IsValid()) {
        m_temporalMaskState.statsReadbackPending[m_frameRuntime.frameIndex] = false;
        return;
    }
    const uint32_t* mapped = mappedReadback.As<const uint32_t>();

    constexpr float kStatsScale = 256.0f;
    const uint32_t acceptedQ = mapped[0];
    const uint32_t disocclusionQ = mapped[1];
    const uint32_t highMotionQ = mapped[2];
    const uint32_t outOfBoundsQ = mapped[3];
    const uint32_t pixels = mapped[4];
    mappedReadback.Reset();
    m_temporalMaskState.statsReadbackPending[m_frameRuntime.frameIndex] = false;

    if (pixels == 0) {
        m_frameDiagnostics.contract.temporalMask.valid = false;
        m_frameDiagnostics.contract.temporalMask.pixelCount = 0;
        return;
    }

    const float denom = static_cast<float>(pixels) * kStatsScale;
    auto normalized = [&](uint32_t value) {
        return std::clamp(static_cast<float>(value) / denom, 0.0f, 1.0f);
    };

    FrameContract::TemporalMaskInfo info{};
    info.valid = true;
    info.sampleFrame = m_temporalMaskState.statsSampleFrame[m_frameRuntime.frameIndex];
    info.pixelCount = pixels;
    info.acceptedRatio = normalized(acceptedQ);
    info.disocclusionRatio = normalized(disocclusionQ);
    info.highMotionRatio = normalized(highMotionQ);
    info.outOfBoundsRatio = normalized(outOfBoundsQ);
    info.readbackLatencyFrames =
        (m_frameLifecycle.renderFrameCounter >= info.sampleFrame)
            ? static_cast<uint32_t>(m_frameLifecycle.renderFrameCounter - info.sampleFrame)
            : 0u;
    m_frameDiagnostics.contract.temporalMask = info;
}

} // namespace Cortex::Graphics
