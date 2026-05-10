#include "Engine.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/Renderer.h"
#include "Scene/Components.h"
#include "UI/DebugMenu.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace Cortex {

void Engine::ShowCameraHelpOverlay() {
    if (m_cameraHelpShown || !m_window || m_engineEditorMode) {
        return;
    }
    if (std::getenv("CORTEX_SUPPRESS_CAMERA_HELP") ||
        !std::getenv("CORTEX_SHOW_CAMERA_HELP") ||
        m_maxFrames > 0 ||
        m_exitAfterVisualValidationCapture) {
        m_cameraHelpShown = true;
        return;
    }

    const char* message =
        "Camera controls:\n"
        "\n"
        "  Left mouse button   - Select entity under cursor\n"
        "  F                   - Frame selected entity (focus camera)\n"
        "  Right mouse button  - Orbit camera around focus (hold)\n"
        "  Middle mouse button - Pan focus point (hold)\n"
        "  Mouse wheel         - Zoom in/out around focus\n"
        "  G                   - Toggle drone/free-flight camera (auto-forward)\n"
        "  W / A / S / D       - Move forward / left / back / right\n"
        "  Space / Ctrl        - Move up / down (drone mode)\n"
        "  Q / E               - Roll left / right (drone mode)\n"
        "  Shift (hold)        - Sprint (faster movement)\n"
        "  F1                  - Reset camera to default\n"
        "\n"
        "Lighting & debug:\n"
        "  F3                  - Toggle shadows\n"
        "  F4                  - Cycle debug view (shaded/normal/rough/metal/albedo/cascades/IBL/SSAO/SSR/SceneGraph)\n"
        "  Z                   - Toggle temporal AA (TAA) on/off\n"
        "  R                   - Cycle gizmo mode (translate / rotate / resize)\n"
        "  U                   - Open scene editor window\n"
        "  F5                  - Increase shadow PCF radius\n"
        "  F7                  - Decrease shadow bias\n"
        "  F8                  - Open unified graphics settings\n"
        "  P                   - Open performance diagnostics\n"
        "  F9 / F10            - Adjust cascade split lambda\n"
        "  F11 / F12           - Adjust near cascade resolution scale\n"
        "  F2                  - Reset debug settings and show debug menu\n"
        "  B                   - Apply hero visual baseline (studio lighting, TAA, SSR/SSAO)\n"
        "  V                   - Toggle ray tracing (if supported)\n"
        "  C                   - Cycle environment preset\n"
        "  1 / 2 / 3           - Jump to hero camera bookmarks\n"
        "  F6                  - Toggle auto-demo orbit around hero scene\n"
        "  Print Screen        - Capture a screenshot to BMP\n"
        "\n"
        "Press OK to continue.";

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Camera & Shadow Controls",
        message,
        m_window->GetSDLWindow());

    m_cameraHelpShown = true;
}

void Engine::ApplyHeroVisualBaseline() {
    if (!m_renderer) {
        return;
    }

    Graphics::ApplyHeroVisualBaselineControls(*m_renderer);

    // Reflect the new renderer state into the debug menu so sliders stay in sync.
    SyncDebugMenuFromRenderer();

    spdlog::info("Hero visual baseline applied (studio environment, TAA, SSR+SSAO)");
}

void Engine::ApplyVRAMQualityGovernor() {
    if (!m_renderer) {
        return;
    }

    // Reset flag; it will be raised again if any step takes effect.
    m_qualityAutoReduced = false;

    const float estimatedMB = m_renderer->GetEstimatedVRAMMB();
    // Soft limit for the portfolio demo path. The goal is to remain usable on
    // 2-4 GB adapters by reducing resolution/shadow residency before removing
    // renderer features from the frame.
    constexpr float kSoftLimitMB = 4096.0f;
    if (estimatedMB <= kSoftLimitMB) {
        return;
    }

    bool changed = false;

    const float currentScale = m_renderer->GetRenderScale();
    if (currentScale > 0.51f) {
        const float nextScale = std::max(0.50f, currentScale - 0.10f);
        changed = Graphics::ApplyRenderScaleControl(*m_renderer, nextScale);
        spdlog::warn("VRAM governor: reducing internal render scale {:.2f} -> {:.2f} (est VRAM {:.0f} MB > {:.0f} MB)",
                     currentScale, m_renderer->GetRenderScale(), estimatedMB, kSoftLimitMB);
    } else {
        // At minimum scale, clamp persistent shadow residency while preserving
        // RT/SSR/SSAO/fog feature flags. This keeps the engine honest: the
        // expensive features are still running, just inside a smaller budget.
        Graphics::ApplySafeQualityPresetControl(*m_renderer);
        changed = true;
        spdlog::warn("VRAM governor: applied memory-budget preset (est VRAM {:.0f} MB > {:.0f} MB)",
                     estimatedMB, kSoftLimitMB);
    }

    if (changed) {
        m_qualityAutoReduced = true;
        // Keep debug UI in sync with any toggles we just changed.
        SyncDebugMenuFromRenderer();
    }
}

void Engine::ApplyPerfQualityGovernor() {
    if (!m_renderer) {
        return;
    }

    m_perfScaleReduced = false;

    // Startup frames include shader warmup, texture uploads, BLAS/TLAS setup,
    // and optional visual-validation readback. Those are not steady-state
    // rendering cost, so do not let them resize render targets and create the
    // exact hitch/reallocation loop the governor is meant to avoid.
    constexpr uint64_t kPerfGovernorWarmupFrames = 120;
    if (m_totalFrameCount < kPerfGovernorWarmupFrames) {
        m_avgFrameTimeMs = 0.0f;
        return;
    }

    const float frameMs = m_frameTime * 1000.0f;
    if (frameMs <= 0.0f || !std::isfinite(frameMs)) {
        return;
    }

    if (m_avgFrameTimeMs <= 0.0f) {
        m_avgFrameTimeMs = frameMs;
    } else {
        m_avgFrameTimeMs = (m_avgFrameTimeMs * 0.90f) + (frameMs * 0.10f);
    }

    constexpr float kTargetFrameMs = 16.67f; // 60 Hz target for the tech demo.
    constexpr float kHysteresisMs = 4.0f;
    if (m_avgFrameTimeMs < (kTargetFrameMs + kHysteresisMs)) {
        return;
    }

    const float currentScale = m_renderer->GetRenderScale();
    if (currentScale <= 0.51f) {
        return;
    }

    const float nextScale = std::max(0.50f, currentScale - 0.05f);
    Graphics::ApplyRenderScaleControl(*m_renderer, nextScale);
    m_perfScaleReduced = (m_renderer->GetRenderScale() < currentScale);

    if (m_perfScaleReduced) {
        spdlog::warn("Perf governor: reducing internal render scale {:.2f} -> {:.2f} (avg frame {:.2f} ms)",
                     currentScale, m_renderer->GetRenderScale(), m_avgFrameTimeMs);
        SyncDebugMenuFromRenderer();
    }
}

void Engine::RenderHUD() {
    if (!m_window || !m_registry || !m_renderer) {
        return;
    }

    // Gather camera information
    glm::vec3 camPos(0.0f);
    float camFov = 60.0f;
    bool haveCamera = false;

    if (m_activeCameraEntity != entt::null &&
        m_registry->HasComponent<Scene::TransformComponent>(m_activeCameraEntity) &&
        m_registry->HasComponent<Scene::CameraComponent>(m_activeCameraEntity)) {
        auto& transform = m_registry->GetComponent<Scene::TransformComponent>(m_activeCameraEntity);
        auto& camera = m_registry->GetComponent<Scene::CameraComponent>(m_activeCameraEntity);
        camPos = transform.position;
        camFov = camera.fov;
        haveCamera = true;
    }

    // Renderer state
    auto* renderer = m_renderer.get();
    const auto quality = renderer->GetQualityState();
    const auto features = renderer->GetFeatureState();
    const auto rt = renderer->GetRayTracingState();
    float exposure = quality.exposure;
    bool shadows = quality.shadowsEnabled;
    int debugMode = quality.debugViewMode;
    float shadowBias = quality.shadowBias;
    float shadowPCF = quality.shadowPCFRadius;
    float cascadeLambda = quality.cascadeSplitLambda;
    float cascade0Scale = quality.cascade0ResolutionScale;
    float bloomIntensity = quality.bloomIntensity;
    bool pcss = features.pcssEnabled;
    bool fxaa = features.fxaaEnabled;
    bool taa = features.taaEnabled;
    bool ssr = features.ssrEnabled;
    bool ssao = features.ssaoEnabled;
    bool ibl = features.iblEnabled;
    bool fog = features.fogEnabled;
    bool rtSupported = rt.supported;
    bool rtEnabled = rt.enabled;
    std::string envNameUtf8 = renderer->GetCurrentEnvironmentName();

    // Approximate FPS from last frame time
    float fps = (m_frameTime > 0.0f) ? (1.0f / m_frameTime) : 0.0f;
    const auto vram = renderer->GetEstimatedVRAMBreakdown();
    constexpr double kBytesToMB = 1.0 / (1024.0 * 1024.0);
    const double vramMB = static_cast<double>(vram.TotalBytes()) * kBytesToMB;
    const double targetMB = static_cast<double>(vram.renderTargetBytes) * kBytesToMB;
    const double postMB = static_cast<double>(vram.postProcessBytes) * kBytesToMB;
    const double assetMB = static_cast<double>(
        vram.textureBytes + vram.environmentBytes + vram.geometryBytes) * kBytesToMB;

    HWND hwnd = m_window->GetHWND();
    if (!hwnd) {
        return;
    }

    HDC dc = GetDC(hwnd);
    if (!dc) {
        return;
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 255, 0));

    int lineY = 8;
    auto drawLine = [&](const wchar_t* text) {
        TextOutW(dc, 8, lineY, text, static_cast<int>(wcslen(text)));
        lineY += 16;
    };

    wchar_t buffer[256];

    // Always show top-level FPS / frame time and an approximate VRAM estimate
    swprintf_s(buffer, L"FPS: %.1f  Frame: %.2f ms", fps, m_frameTime * 1000.0f);
    drawLine(buffer);

    swprintf_s(buffer, L"VRAM (est): %.0f MB  tgt %.0f  post %.0f  asset %.0f  rt %.0f",
               vramMB, targetMB, postMB, assetMB,
               static_cast<double>(vram.rtStructureBytes) * kBytesToMB);
    drawLine(buffer);

    if (haveCamera) {
        swprintf_s(buffer, L"Camera: (%.2f, %.2f, %.2f) FOV: %.1f",
                   camPos.x, camPos.y, camPos.z, camFov);
        drawLine(buffer);
    } else {
        drawLine(L"Camera: <none>");
    }

    // High-level render mode and quality summary.
    auto debugViewLabel = [](int mode) -> const wchar_t* {
        switch (mode) {
            case 0:  return L"Shaded";
            case 1:  return L"Normals";
            case 2:  return L"Roughness";
            case 3:  return L"Metallic";
            case 4:  return L"Albedo";
            case 5:  return L"Cascades";
            case 6:  return L"DebugScreen";
            case 13: return L"SSAO_Only";
            case 14: return L"SSAO_Overlay";
            case 15: return L"SSR_Only";
            case 16: return L"SSR_Overlay";
            case 18: return L"RT_ShadowMask";
            case 19: return L"RT_ShadowHistory";
            case 20: return L"RT_Reflections";
            case 21: return L"RT_GI";
            case 22: return L"Shaded_NoRTGI";
            case 23: return L"Shaded_NoRTRefl";
            case 24: return L"RT_ReflectionRays";
            default: return L"Other";
        }
    };

    std::wstring envName;
    if (!envNameUtf8.empty()) {
        envName.assign(envNameUtf8.begin(), envNameUtf8.end());
    } else {
        envName = L"<none>";
    }

    swprintf_s(buffer, L"View: %s (%d)  RTX: %s%s",
               debugViewLabel(debugMode),
               debugMode,
               rtEnabled ? L"ON" : L"OFF",
               !rtSupported ? L" [Not Supported]" : L"");
    drawLine(buffer);

    swprintf_s(buffer, L"Env: %s  IBL: %s  Fog: %s",
               envName.c_str(),
               ibl ? L"ON" : L"OFF",
               fog ? L"ON" : L"OFF");
    drawLine(buffer);

    const wchar_t* aaLabel = taa ? L"TAA" : (fxaa ? L"FXAA" : L"None");
    swprintf_s(buffer, L"AA: %s  SSR: %s  SSAO: %s",
               aaLabel,
               ssr ? L"ON" : L"OFF",
               ssao ? L"ON" : L"OFF");
    drawLine(buffer);

    // Scene preset summary and quick hint for switching.
    const wchar_t* sceneLabel =
        (m_currentScenePreset == ScenePreset::CornellBox)
            ? L"Cornell Box"
            : L"Dragon Over Water Studio";
    swprintf_s(buffer, L"Scene: %s  (press N to switch)", sceneLabel);
    drawLine(buffer);

    // Only show detailed renderer/light/command information in debug screen mode
    if (debugMode == 6) {
        swprintf_s(buffer, L"Exposure (EV): %.2f  Bloom: %.2f", exposure, bloomIntensity);
        drawLine(buffer);

        swprintf_s(buffer, L"Shadows: %s  DebugView: %d  PCSS: %s  FXAA: %s",
                   shadows ? L"ON" : L"OFF",
                   debugMode,
                   pcss ? L"ON" : L"OFF",
                   fxaa ? L"ON" : L"OFF");
        drawLine(buffer);

        swprintf_s(buffer, L"Shadow Bias: %.6f  PCF Radius: %.2f  Cascade \u03bb: %.2f  NearCascScale: %.2f",
                   shadowBias, shadowPCF, cascadeLambda, cascade0Scale);
        drawLine(buffer);

        // Light count (from registry)
        size_t lightCount = 0;
        if (m_registry) {
            auto lightView = m_registry->View<Scene::LightComponent>();
            lightCount = static_cast<size_t>(lightView.size());
        }
        swprintf_s(buffer, L"Lights: %zu", lightCount);
        drawLine(buffer);

        // Per-light summary (up to two lights)
        if (m_registry && lightCount > 0) {
            drawLine(L"Light details:");
            auto view = m_registry->View<Scene::LightComponent>();
            size_t shown = 0;
            for (auto entity : view) {
                const auto& light = view.get<Scene::LightComponent>(entity);

                const wchar_t* typeLabel = L"Point";
                if (light.type == Scene::LightType::Directional) typeLabel = L"Dir";
                else if (light.type == Scene::LightType::Spot)   typeLabel = L"Spot";
                else if (light.type == Scene::LightType::AreaRect) typeLabel = L"Area";

                glm::vec3 pos(0.0f);
                if (m_registry->HasComponent<Scene::TransformComponent>(entity)) {
                    pos = m_registry->GetComponent<Scene::TransformComponent>(entity).position;
                }

                std::wstring name;
                if (m_registry->HasComponent<Scene::TagComponent>(entity)) {
                    const auto& tag = m_registry->GetComponent<Scene::TagComponent>(entity).tag;
                    name.assign(tag.begin(), tag.end());
                } else {
                    name = L"<unnamed>";
                }

                swprintf_s(buffer, L"  %s (%s) I=%.2f Pos=(%.1f, %.1f, %.1f)",
                           name.c_str(),
                           typeLabel,
                           light.intensity,
                           pos.x, pos.y, pos.z);
                drawLine(buffer);

                if (++shown >= 2) {
                    break;
                }
            }
        }

        if (!m_recentCommandMessages.empty()) {
            drawLine(L"Last commands:");
            for (const auto& msg : m_recentCommandMessages) {
                std::wstring wmsg(msg.begin(), msg.end());
                if (wmsg.size() > 80) {
                    wmsg.resize(80);
                }
                TextOutW(dc, 16, lineY, wmsg.c_str(), static_cast<int>(wmsg.size()));
                lineY += 16;
            }
        }
    }

    // Selection / camera mode / controls hint (always shown)
    std::wstring selName = L"<none>";
    if (m_selectedEntity != entt::null &&
        m_registry->HasComponent<Scene::TagComponent>(m_selectedEntity)) {
        const auto& tag = m_registry->GetComponent<Scene::TagComponent>(m_selectedEntity);
        selName.assign(tag.tag.begin(), tag.tag.end());
    }

    swprintf_s(buffer, L"Selected: %s  Focus: %hs  Mode: %hs",
               selName.c_str(),
               m_focusTargetName.empty() ? "<none>" : m_focusTargetName.c_str(),
               m_droneFlightEnabled ? "Drone" : "Orbit");
    drawLine(buffer);

    // When an object is selected, expose its material numerically.
    if (m_selectedEntity != entt::null &&
        m_registry->HasComponent<Scene::RenderableComponent>(m_selectedEntity)) {
        const auto& renderable = m_registry->GetComponent<Scene::RenderableComponent>(m_selectedEntity);
        std::wstring preset;
        if (!renderable.presetName.empty()) {
            preset.assign(renderable.presetName.begin(), renderable.presetName.end());
        } else {
            preset = L"<none>";
        }
        swprintf_s(buffer, L"Material: preset=%s  base=(%.2f, %.2f, %.2f)  metal=%.2f  rough=%.2f  ao=%.2f",
                   preset.c_str(),
                   renderable.albedoColor.r,
                   renderable.albedoColor.g,
                   renderable.albedoColor.b,
                   renderable.metallic,
                   renderable.roughness,
                   renderable.ao);
        drawLine(buffer);
    }

    drawLine(L"LMB: select  F: frame  G: drone  RMB: orbit  MMB: pan");

    // When the GPU settings overlay is visible (M / F2), render a textual
    // legend so it is obvious what each row controls and what the current
    // values are. The colored bars themselves are drawn in the post-process
    // shader; this HUD pass just annotates them.
    if (UI::DebugMenu::IsVisible()) {
        UI::DebugMenuState state = UI::DebugMenu::GetState();

        drawLine(L"[Settings overlay active: M / F2]");
        drawLine(L"Use UP/DOWN to select row, LEFT/RIGHT to tweak, SPACE/ENTER to toggle.");

        int panelX = static_cast<int>(m_window->GetWidth()) - 320;
        int y = 48;

        auto drawPanelLine = [&](const wchar_t* text, COLORREF color) {
            SetTextColor(dc, color);
            TextOutW(dc, panelX + 12, y, text, static_cast<int>(wcslen(text)));
            y += 18;
        };

        struct Row {
            const wchar_t* label;
            float          value;
            bool           isBool;
            int            sectionIndex;
        };

        Row rows[] = {
            { L"[Render] Exposure (EV)",           state.exposure,                       false, 0 },
            { L"[Render] Bloom Intensity",         state.bloomIntensity,                 false, 1 },
            { L"[Shadows] Shadows Enabled",        state.shadowsEnabled ? 1.0f : 0.0f,   true,  2 },
            { L"[Shadows] PCSS (Soft Shadows)",    state.pcssEnabled ? 1.0f : 0.0f,      true,  3 },
            { L"[Shadows] Bias",                   state.shadowBias,                     false, 4 },
            { L"[Shadows] PCF Radius",             state.shadowPCFRadius,                false, 5 },
            { L"[Shadows] Cascade Lambda",         state.cascadeLambda,                  false, 6 },
            { L"[AA] FXAA",                        state.fxaaEnabled ? 1.0f : 0.0f,      true,  7 },
            { L"[AA] TAA",                         state.taaEnabled ? 1.0f : 0.0f,       true,  8 },
            { L"[Reflections] SSR",                state.ssrEnabled ? 1.0f : 0.0f,       true,  9 },
            { L"[AO] SSAO",                        state.ssaoEnabled ? 1.0f : 0.0f,      true,  10 },
            { L"[Environment] IBL",                state.iblEnabled ? 1.0f : 0.0f,       true,  11 },
            { L"[Environment] Fog",                state.fogEnabled ? 1.0f : 0.0f,       true,  12 },
            { L"[Camera] Base Speed",              state.cameraBaseSpeed,                false, 13 },
            { L"[Advanced] Ray Tracing",           state.rayTracingEnabled ? 1.0f : 0.0f,true,  14 }
        };

        const int rowCount = static_cast<int>(std::size(rows));
        for (int i = 0; i < rowCount; ++i) {
            const Row& r = rows[i];
            wchar_t lineText[256];

            if (r.isBool) {
                const bool on = (r.value > 0.5f);
                swprintf_s(lineText, L"%2d) %s : %s", r.sectionIndex, r.label, on ? L"ON" : L"OFF");
            } else {
                swprintf_s(lineText, L"%2d) %s : %.3f", r.sectionIndex, r.label, r.value);
            }

            COLORREF color = (m_settingsSection == r.sectionIndex)
                ? RGB(255, 255, 0)
                : RGB(200, 200, 200);

            drawPanelLine(lineText, color);
        }
    }

    ReleaseDC(hwnd, dc);
}

void Engine::CaptureScreenshot() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t filenameW[MAX_PATH];
    swprintf_s(filenameW, L"screenshot_%04d%02d%02d_%02d%02d%02d_%03d.bmp",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    CaptureScreenshot(std::filesystem::path(filenameW));
}

void Engine::CaptureScreenshot(const std::filesystem::path& outputPath) {
    if (!m_window) {
        spdlog::warn("CaptureScreenshot: window not available");
        return;
    }

    HWND hwnd = m_window->GetHWND();
    if (!hwnd) {
        spdlog::warn("CaptureScreenshot: HWND is null");
        return;
    }

    RECT rect{};
    if (!GetClientRect(hwnd, &rect)) {
        spdlog::warn("CaptureScreenshot: GetClientRect failed");
        return;
    }

    int width  = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        spdlog::warn("CaptureScreenshot: invalid client size");
        return;
    }

    HDC hdcWindow = GetDC(hwnd);
    if (!hdcWindow) {
        spdlog::warn("CaptureScreenshot: GetDC failed");
        return;
    }

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    if (!hdcMem) {
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: CreateCompatibleDC failed");
        return;
    }

    HBITMAP hbm = CreateCompatibleBitmap(hdcWindow, width, height);
    if (!hbm) {
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: CreateCompatibleBitmap failed");
        return;
    }

    HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

    BITMAP bmp{};
    GetObject(hbm, sizeof(BITMAP), &bmp);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = -bmp.bmHeight; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(bmp.bmWidth) * static_cast<size_t>(bmp.bmHeight) * 4u);
    if (!GetDIBits(hdcWindow, hbm, 0, static_cast<UINT>(bmp.bmHeight), pixels.data(),
                   reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS)) {
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: GetDIBits failed");
        return;
    }

    std::error_code ec;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            SelectObject(hdcMem, oldBmp);
            DeleteObject(hbm);
            DeleteDC(hdcMem);
            ReleaseDC(hwnd, hdcWindow);
            spdlog::warn("CaptureScreenshot: failed to create output directory '{}': {}",
                         outputPath.parent_path().string(),
                         ec.message());
            return;
        }
    }

    HANDLE hFile = CreateFileW(outputPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWindow);
        spdlog::warn("CaptureScreenshot: failed to create output file");
        return;
    }

    BITMAPFILEHEADER bmf{};
    bmf.bfType = 0x4D42; // 'BM'
    DWORD dibSize = static_cast<DWORD>(pixels.size());
    bmf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmf.bfSize = bmf.bfOffBits + dibSize;

    DWORD written = 0;
    WriteFile(hFile, &bmf, sizeof(bmf), &written, nullptr);
    WriteFile(hFile, &bi, sizeof(bi), &written, nullptr);
    WriteFile(hFile, pixels.data(), dibSize, &written, nullptr);

    CloseHandle(hFile);

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWindow);

    spdlog::info("Screenshot captured to {}", outputPath.string());
}

} // namespace Cortex
