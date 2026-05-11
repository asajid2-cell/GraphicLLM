#include "Renderer.h"
#include "Core/Window.h"
#include "Debug/GPUProfiler.h"
#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Cortex::Graphics {

namespace {

std::filesystem::path GetRendererLogDirectory() {
    if (const char* overrideDir = std::getenv("CORTEX_LOG_DIR")) {
        if (*overrideDir) {
            std::filesystem::path logDir = std::filesystem::path(overrideDir);
            std::error_code ec;
            std::filesystem::create_directories(logDir, ec);
            if (!ec) {
                return logDir;
            }
        }
    }

    wchar_t exePathW[MAX_PATH]{};
    const DWORD exeLen = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
    std::filesystem::path logDir =
        (exeLen > 0 && exeLen < MAX_PATH)
            ? (std::filesystem::path(exePathW).parent_path() / "logs")
            : (std::filesystem::current_path() / "logs");

    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        logDir = std::filesystem::current_path() / "logs";
        ec.clear();
        std::filesystem::create_directories(logDir, ec);
    }
    return logDir;
}

uint64_t GetVisualValidationMinFrame() {
    constexpr uint64_t kDefaultMinFrame = 8;
    const char* env = std::getenv("CORTEX_VISUAL_VALIDATION_MIN_FRAME");
    if (!env || !*env) {
        return kDefaultMinFrame;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(env, &end, 10);
    if (end == env) {
        return kDefaultMinFrame;
    }
    return std::max<uint64_t>(1, static_cast<uint64_t>(parsed));
}

void WriteBackBufferBMP(const std::filesystem::path& path,
                        ID3D12Resource* readback,
                        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
                        uint32_t width,
                        uint32_t height) {
    if (!readback || width == 0 || height == 0) {
        return;
    }

    uint8_t* mapped = nullptr;
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(footprint.Footprint.RowPitch) * height};
    if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped))) || !mapped) {
        spdlog::warn("Visual validation capture: failed to map readback buffer");
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        readback->Unmap(0, nullptr);
        spdlog::warn("Visual validation capture: failed to open '{}'", path.string());
        return;
    }

#pragma pack(push, 1)
    struct BmpFileHeader {
        uint16_t type = 0x4D42;
        uint32_t size = 0;
        uint16_t reserved1 = 0;
        uint16_t reserved2 = 0;
        uint32_t offBits = 54;
    };
    struct BmpInfoHeader {
        uint32_t size = 40;
        int32_t width = 0;
        int32_t height = 0;
        uint16_t planes = 1;
        uint16_t bitCount = 32;
        uint32_t compression = 0;
        uint32_t sizeImage = 0;
        int32_t xPelsPerMeter = 0;
        int32_t yPelsPerMeter = 0;
        uint32_t clrUsed = 0;
        uint32_t clrImportant = 0;
    };
#pragma pack(pop)

    const uint32_t tightRowBytes = width * 4u;
    BmpFileHeader fileHeader{};
    BmpInfoHeader infoHeader{};
    infoHeader.width = static_cast<int32_t>(width);
    infoHeader.height = -static_cast<int32_t>(height);
    infoHeader.sizeImage = tightRowBytes * height;
    fileHeader.size = fileHeader.offBits + infoHeader.sizeImage;

    out.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    const uint8_t* srcBase = mapped + footprint.Offset;
    std::vector<uint8_t> row(tightRowBytes);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = srcBase + static_cast<size_t>(y) * footprint.Footprint.RowPitch;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t r = src[x * 4u + 0u];
            const uint8_t g = src[x * 4u + 1u];
            const uint8_t b = src[x * 4u + 2u];
            const uint8_t a = src[x * 4u + 3u];
            row[x * 4u + 0u] = b;
            row[x * 4u + 1u] = g;
            row[x * 4u + 2u] = r;
            row[x * 4u + 3u] = a;
        }
        out.write(reinterpret_cast<const char*>(row.data()), row.size());
    }

    readback->Unmap(0, nullptr);
}

} // namespace

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

void Renderer::EndFrame() {
    // Mark the start of end-of-frame work (RT history copies, back-buffer
    // transition, present) so device-removed diagnostics can distinguish
    // hangs that occur after all main passes have finished.
    WriteBreadcrumb(GpuMarker::EndFrame);
    Debug::GPUProfiler::Get().BeginScope(m_commandResources.graphicsList.Get(), "EndFrame", "Frame");

    // Before presenting, update the RT shadow history buffer so the next
    // frame's temporal smoothing has valid data.
    if (m_rtRuntimeState.supported && m_rtRuntimeState.enabled && !m_rtDenoiseState.shadowDenoisedThisFrame &&
        m_framePlanning.rtPlan.dispatchShadows && m_rtShadowTargets.mask && m_rtShadowTargets.history) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_rtShadowTargets.maskState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_rtShadowTargets.mask.Get();
            barriers[barrierCount].Transition.StateBefore = m_rtShadowTargets.maskState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }

        if (m_rtShadowTargets.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_rtShadowTargets.history.Get();
            barriers[barrierCount].Transition.StateBefore = m_rtShadowTargets.historyState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_rtShadowTargets.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        if (barrierCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
        }

        m_commandResources.graphicsList->CopyResource(m_rtShadowTargets.history.Get(), m_rtShadowTargets.mask.Get());

        // Return both resources to shader-resource state for the next frame.
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_rtShadowTargets.mask.Get();
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_rtShadowTargets.history.Get();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandResources.graphicsList->ResourceBarrier(2, barriers);

        m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtShadowTargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          MarkRTShadowHistoryValid();
      }

      // Update RT GI history buffer in lock-step with the RT GI color buffer
      // so temporal accumulation in the shader has a stable previous frame.
      if (m_rtRuntimeState.supported && m_rtRuntimeState.enabled && !m_rtDenoiseState.giDenoisedThisFrame &&
          m_framePlanning.rtPlan.dispatchGI && m_rtGITargets.color && m_rtGITargets.history) {
          D3D12_RESOURCE_BARRIER giBarriers[2] = {};
          UINT giBarrierCount = 0;

          if (m_rtGITargets.colorState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
              giBarriers[giBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              giBarriers[giBarrierCount].Transition.pResource = m_rtGITargets.color.Get();
              giBarriers[giBarrierCount].Transition.StateBefore = m_rtGITargets.colorState;
              giBarriers[giBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
              giBarriers[giBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++giBarrierCount;
              m_rtGITargets.colorState = D3D12_RESOURCE_STATE_COPY_SOURCE;
          }

          if (m_rtGITargets.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
              giBarriers[giBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              giBarriers[giBarrierCount].Transition.pResource = m_rtGITargets.history.Get();
              giBarriers[giBarrierCount].Transition.StateBefore = m_rtGITargets.historyState;
              giBarriers[giBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
              giBarriers[giBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++giBarrierCount;
              m_rtGITargets.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
          }

          if (giBarrierCount > 0) {
              m_commandResources.graphicsList->ResourceBarrier(giBarrierCount, giBarriers);
          }

          m_commandResources.graphicsList->CopyResource(m_rtGITargets.history.Get(), m_rtGITargets.color.Get());

          giBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          giBarriers[0].Transition.pResource = m_rtGITargets.color.Get();
          giBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
          giBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          giBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          giBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          giBarriers[1].Transition.pResource = m_rtGITargets.history.Get();
          giBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          giBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          giBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          m_commandResources.graphicsList->ResourceBarrier(2, giBarriers);

          m_rtGITargets.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtGITargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          MarkRTGIHistoryValid();
      }

      // Update RT reflection history after the DXR reflections pass has
      // populated the current RT reflection color buffer. This mirrors the
      // shadow / GI history updates above so the post-process shader can
      // blend against the previous frame when g_DebugMode.w indicates that
      // RT history is valid. If no reflection rays were traced this frame,
      // skip the copy so we do not treat uninitialized data as valid history.
      if (m_rtRuntimeState.supported && m_rtRuntimeState.enabled && !m_rtDenoiseState.reflectionDenoisedThisFrame &&
          m_frameLifecycle.rtReflectionWrittenThisFrame && m_rtReflectionTargets.color && m_rtReflectionTargets.history) {
          D3D12_RESOURCE_BARRIER reflBarriers[2] = {};
          UINT reflBarrierCount = 0;

          if (m_rtReflectionTargets.colorState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
              reflBarriers[reflBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              reflBarriers[reflBarrierCount].Transition.pResource = m_rtReflectionTargets.color.Get();
              reflBarriers[reflBarrierCount].Transition.StateBefore = m_rtReflectionTargets.colorState;
              reflBarriers[reflBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
              reflBarriers[reflBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++reflBarrierCount;
              m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_COPY_SOURCE;
          }

          if (m_rtReflectionTargets.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
              reflBarriers[reflBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              reflBarriers[reflBarrierCount].Transition.pResource = m_rtReflectionTargets.history.Get();
              reflBarriers[reflBarrierCount].Transition.StateBefore = m_rtReflectionTargets.historyState;
              reflBarriers[reflBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
              reflBarriers[reflBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++reflBarrierCount;
              m_rtReflectionTargets.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
          }

          if (reflBarrierCount > 0) {
              m_commandResources.graphicsList->ResourceBarrier(reflBarrierCount, reflBarriers);
          }

          m_commandResources.graphicsList->CopyResource(m_rtReflectionTargets.history.Get(), m_rtReflectionTargets.color.Get());

          reflBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          reflBarriers[0].Transition.pResource = m_rtReflectionTargets.color.Get();
          reflBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
          reflBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          reflBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          reflBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          reflBarriers[1].Transition.pResource = m_rtReflectionTargets.history.Get();
          reflBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          reflBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          reflBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          m_commandResources.graphicsList->ResourceBarrier(2, reflBarriers);

          m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtReflectionTargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          MarkRTReflectionHistoryValid();
      }

    // Ensure screen-space/post-process inputs are back in a shader-resource
    // state by the end of the frame so future passes (or diagnostics) never
    // observe them left in RENDER_TARGET / UNORDERED_ACCESS when Present is
    // called, even if the main post-process resolve was skipped.
    {
        D3D12_RESOURCE_BARRIER ppBarriers[8] = {};
        UINT ppCount = 0;

        if (m_ssaoResources.resources.texture && m_ssaoResources.resources.resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_ssaoResources.resources.texture.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_ssaoResources.resources.resourceState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_ssaoResources.resources.resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_ssrResources.resources.color && m_ssrResources.resources.resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_ssrResources.resources.color.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_ssrResources.resources.resourceState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_ssrResources.resources.resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_temporalScreenState.velocityBuffer && m_temporalScreenState.velocityState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_temporalScreenState.velocityBuffer.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_temporalScreenState.velocityState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_temporalScreenState.taaIntermediate && m_temporalScreenState.taaIntermediateState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_temporalScreenState.taaIntermediate.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_temporalScreenState.taaIntermediateState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

    if (m_rtReflectionTargets.color && m_rtReflectionTargets.colorState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        ppBarriers[ppCount].Transition.pResource = m_rtReflectionTargets.color.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_rtReflectionTargets.colorState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_mainTargets.normalRoughness.resources.texture && m_mainTargets.normalRoughness.resources.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_mainTargets.normalRoughness.resources.texture.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_mainTargets.normalRoughness.resources.state;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_mainTargets.normalRoughness.resources.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (ppCount > 0) {
            m_commandResources.graphicsList->ResourceBarrier(ppCount, ppBarriers);
        }
    }

    // Close GPU timing before automation-only readback/present transitions.
    // Visual-validation capture is a test harness cost; including it in the
    // frame timestamp makes smoke performance budgets nondeterministic.
    Debug::GPUProfiler::Get().EndScope(m_commandResources.graphicsList.Get()); // EndFrame
    Debug::GPUProfiler::Get().EndScope(m_commandResources.graphicsList.Get()); // Frame
    Debug::GPUProfiler::Get().EndFrame(m_commandResources.graphicsList.Get());

    // Transition back buffer to present state if it was used as a render
    // target this frame. When post-process or voxel paths are disabled, the
    // swap-chain buffer may remain in PRESENT state for the entire frame.
    Microsoft::WRL::ComPtr<ID3D12Resource> visualCaptureReadback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT visualCaptureFootprint{};
    uint32_t visualCaptureWidth = 0;
    uint32_t visualCaptureHeight = 0;
    std::filesystem::path visualCapturePath;
    const bool captureVisualValidation =
        std::getenv("CORTEX_CAPTURE_VISUAL_VALIDATION") != nullptr &&
        m_frameLifecycle.renderFrameCounter >= GetVisualValidationMinFrame() &&
        !m_frameLifecycle.visualValidationCaptured &&
        m_frameLifecycle.backBufferUsedAsRTThisFrame &&
        m_services.window &&
        m_services.window->GetCurrentBackBuffer() &&
        m_services.device &&
        m_services.device->GetDevice();

    if (m_frameLifecycle.backBufferUsedAsRTThisFrame) {
        ID3D12Resource* backBuffer = m_services.window->GetCurrentBackBuffer();
        if (captureVisualValidation) {
            const D3D12_RESOURCE_DESC backBufferDesc = backBuffer->GetDesc();
            visualCaptureWidth = static_cast<uint32_t>(backBufferDesc.Width);
            visualCaptureHeight = backBufferDesc.Height;

            UINT numRows = 0;
            UINT64 rowSizeBytes = 0;
            UINT64 totalBytes = 0;
            m_services.device->GetDevice()->GetCopyableFootprints(
                &backBufferDesc,
                0,
                1,
                0,
                &visualCaptureFootprint,
                &numRows,
                &rowSizeBytes,
                &totalBytes);

            D3D12_HEAP_PROPERTIES readbackHeap{};
            readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC readbackDesc{};
            readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            readbackDesc.Width = totalBytes;
            readbackDesc.Height = 1;
            readbackDesc.DepthOrArraySize = 1;
            readbackDesc.MipLevels = 1;
            readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
            readbackDesc.SampleDesc.Count = 1;
            readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            const HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&visualCaptureReadback));
            if (FAILED(hr)) {
                visualCaptureReadback.Reset();
                spdlog::warn("Visual validation capture: failed to create readback buffer");
            } else {
                D3D12_RESOURCE_BARRIER copyBarrier{};
                copyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                copyBarrier.Transition.pResource = backBuffer;
                copyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                copyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                copyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_commandResources.graphicsList->ResourceBarrier(1, &copyBarrier);

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = backBuffer;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = visualCaptureReadback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint = visualCaptureFootprint;

                m_commandResources.graphicsList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                visualCapturePath = GetRendererLogDirectory() / "visual_validation_rt_showcase.bmp";

                D3D12_RESOURCE_BARRIER presentBarrier{};
                presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                presentBarrier.Transition.pResource = backBuffer;
                presentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_commandResources.graphicsList->ResourceBarrier(1, &presentBarrier);
            }
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = visualCaptureReadback ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (!visualCaptureReadback) {
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        }
    }

    UpdateFrameContractHistories();
    ValidateFrameContract();

    // Close and execute command list
    m_commandResources.graphicsList->Close();
    m_frameRuntime.commandListOpen = false;
    m_services.commandQueue->ExecuteCommandList(m_commandResources.graphicsList.Get());
    Debug::GPUProfiler::Get().NotifyFrameSubmitted();

    if (visualCaptureReadback) {
        m_services.commandQueue->Flush();
        WriteBackBufferBMP(
            visualCapturePath,
            visualCaptureReadback.Get(),
            visualCaptureFootprint,
            visualCaptureWidth,
            visualCaptureHeight);
        m_frameLifecycle.visualValidationCaptured = true;
    }

    // Present
    m_services.window->Present();

    // Surface device-removed errors as close to present as possible. This
    // helps isolate hangs that occur in swap-chain or late-frame work.
    if (m_services.device && m_services.device->GetDevice()) {
        HRESULT reason = m_services.device->GetDevice()->GetDeviceRemovedReason();
        if (reason != S_OK) {
            CORTEX_REPORT_DEVICE_REMOVED("EndFrame_Present", reason);
            return;
        }
    }

    // Signal fence for this frame
    m_frameRuntime.fenceValues[m_frameRuntime.frameIndex] = m_services.commandQueue->Signal();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
