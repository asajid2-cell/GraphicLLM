#include "QualitySettingsWindow.h"

#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Graphics/Renderer.h"

#include <commctrl.h>
#include <cmath>

namespace Cortex::UI {

namespace {

// Control identifiers for the quality / performance window.
enum ControlIdQuality : int {
    IDC_QL_RENDER_SCALE = 3001,

    IDC_QL_RT_MASTER    = 3101,
    IDC_QL_RT_REFLECT   = 3102,
    IDC_QL_RT_GI        = 3103,
    IDC_QL_TAA          = 3104,
    IDC_QL_SSR          = 3105,
    IDC_QL_SSAO         = 3106,
    IDC_QL_IBL          = 3107,
    IDC_QL_FOG          = 3108,

    IDC_QL_STATS_FPS    = 3201,
    IDC_QL_STATS_VRAM   = 3202,
    IDC_QL_SAFE_PRESET  = 3203,
};

struct QualityState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HFONT font = nullptr;

    HWND sliderRenderScale = nullptr;

    HWND chkRTMaster = nullptr;
    HWND chkRTRefl   = nullptr;
    HWND chkRTGI     = nullptr;
    HWND chkTAA      = nullptr;
    HWND chkSSR      = nullptr;
    HWND chkSSAO     = nullptr;
    HWND chkIBL      = nullptr;
    HWND chkFog      = nullptr;

    HWND txtFPS  = nullptr;
    HWND txtVRAM = nullptr;
    HWND btnSafePreset = nullptr;
};

QualityState g_q;

float SliderToFloat(HWND slider, float minValue, float maxValue) {
    if (!slider) return minValue;
    int pos = static_cast<int>(SendMessage(slider, TBM_GETPOS, 0, 0));
    float t = static_cast<float>(pos) / 100.0f;
    return minValue + t * (maxValue - minValue);
}

void SetSliderFromFloat(HWND slider, float value, float minValue, float maxValue) {
    if (!slider) return;
    float t = 0.0f;
    if (maxValue > minValue) {
        t = (value - minValue) / (maxValue - minValue);
    }
    int pos = static_cast<int>(t * 100.0f + 0.5f);
    if (pos < 0) pos = 0;
    if (pos > 100) pos = 100;
    SendMessage(slider, TBM_SETPOS, TRUE, pos);
}

void SetCheckbox(HWND hwnd, bool enabled) {
    if (!hwnd) return;
    SendMessage(hwnd, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool GetCheckbox(HWND hwnd) {
    if (!hwnd) return false;
    return SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

const wchar_t* kQualitySettingsClassName = L"CortexQualitySettingsWindow";

void RefreshControlsFromState() {
    if (!g_q.hwnd) {
        return;
    }

    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    // Render scale slider
    SetSliderFromFloat(g_q.sliderRenderScale,
                       renderer->GetRenderScale(),
                       0.5f,
                       1.0f);

    // Feature toggles
    SetCheckbox(g_q.chkRTMaster,
                renderer->IsRayTracingSupported() && renderer->IsRayTracingEnabled());
    SetCheckbox(g_q.chkRTRefl, renderer->GetRTReflectionsEnabled());
    SetCheckbox(g_q.chkRTGI,   renderer->GetRTGIEnabled());
    SetCheckbox(g_q.chkTAA,    renderer->IsTAAEnabled());
    SetCheckbox(g_q.chkSSR,    renderer->GetSSREnabled());
    SetCheckbox(g_q.chkSSAO,   renderer->GetSSAOEnabled());
    SetCheckbox(g_q.chkIBL,    renderer->GetIBLEnabled());
    SetCheckbox(g_q.chkFog,    renderer->IsFogEnabled());
}

void RefreshStatsLabels() {
    auto* engine   = Cortex::ServiceLocator::GetEngine();
    auto* renderer = Cortex::ServiceLocator::GetRenderer();

    float fps = 0.0f;
    if (engine) {
        float dt = engine->GetLastFrameTimeSeconds();
        if (dt > 0.0f) {
            fps = 1.0f / dt;
        }
    }

    float vramMB = renderer ? renderer->GetEstimatedVRAMMB() : 0.0f;

    wchar_t buffer[128];
    if (g_q.txtFPS) {
        swprintf_s(buffer, L"FPS: %.1f", fps);
        SetWindowTextW(g_q.txtFPS, buffer);
    }
    if (g_q.txtVRAM) {
        swprintf_s(buffer, L"VRAM (est): %.0f MB", vramMB);
        SetWindowTextW(g_q.txtVRAM, buffer);
    }
}

void RegisterQualitySettingsClass() {
    static bool registered = false;
    if (registered) {
        return;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        switch (msg) {
        case WM_CREATE: {
            g_q.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;

            const int margin = 8;
            const int labelHeight = 18;
            const int sliderHeight = 24;
            const int checkHeight = 18;
            const int rowGap = 4;

            int x = margin;
            int y = margin;
            int colLabelWidth = 140;
            int colSliderWidth = width - colLabelWidth - margin * 2;

            auto makeLabel = [&](const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, colLabelWidth - 4, labelHeight,
                    hwnd, nullptr, nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_q.font), TRUE);
                return h;
            };

            auto makeSlider = [&](int id, int yy) {
                HWND h = CreateWindowExW(
                    0, TRACKBAR_CLASSW, L"",
                    WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                    x + colLabelWidth, yy, colSliderWidth, sliderHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                return h;
            };

            auto makeCheckbox = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    x, yy, width - margin * 2, checkHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_q.font), TRUE);
                return h;
            };

            auto makeStatic = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, width - margin * 2, labelHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_q.font), TRUE);
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, yy, width - margin * 2, 24,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_q.font), TRUE);
                return h;
            };

            // Render scale
            makeLabel(L"Render Scale", y);
            g_q.sliderRenderScale = makeSlider(IDC_QL_RENDER_SCALE, y);
            y += sliderHeight + rowGap * 2;

            // Quality toggles
            g_q.chkRTMaster = makeCheckbox(IDC_QL_RT_MASTER, L"RTX (global)", y);
            y += checkHeight + rowGap;
            g_q.chkRTRefl   = makeCheckbox(IDC_QL_RT_REFLECT, L"RT Reflections", y);
            y += checkHeight + rowGap;
            g_q.chkRTGI     = makeCheckbox(IDC_QL_RT_GI, L"RT GI / Ambient", y);
            y += checkHeight + rowGap;

            g_q.chkTAA      = makeCheckbox(IDC_QL_TAA, L"TAA (temporal AA)", y);
            y += checkHeight + rowGap;
            g_q.chkSSR      = makeCheckbox(IDC_QL_SSR, L"SSR (screen-space reflections)", y);
            y += checkHeight + rowGap;
            g_q.chkSSAO     = makeCheckbox(IDC_QL_SSAO, L"SSAO (ambient occlusion)", y);
            y += checkHeight + rowGap;
            g_q.chkIBL      = makeCheckbox(IDC_QL_IBL, L"IBL (environment lighting)", y);
            y += checkHeight + rowGap;
            g_q.chkFog      = makeCheckbox(IDC_QL_FOG, L"Fog / Atmosphere", y);
            y += checkHeight + rowGap * 2;

            // Stats
            g_q.txtFPS  = makeStatic(IDC_QL_STATS_FPS,  L"FPS: --", y);
            y += labelHeight + rowGap;
            g_q.txtVRAM = makeStatic(IDC_QL_STATS_VRAM, L"VRAM (est): -- MB", y);
            y += labelHeight + rowGap * 2;

            // Safe preset button
            g_q.btnSafePreset = makeButton(IDC_QL_SAFE_PRESET, L"Apply Safe Low Preset", y);

            // Periodic refresh for stats.
            SetTimer(hwnd, 1, 500, nullptr);

            RefreshControlsFromState();
            RefreshStatsLabels();
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                RefreshStatsLabels();
            }
            return 0;
        case WM_HSCROLL: {
            auto* renderer = Cortex::ServiceLocator::GetRenderer();
            if (!renderer || renderer->IsDeviceRemoved()) {
                // Once the device has been removed, avoid touching render
                // scale; the renderer has already entered a degraded state.
                return 0;
            }

            const int scrollCode = LOWORD(wParam);
            HWND slider = reinterpret_cast<HWND>(lParam);
            if (slider == g_q.sliderRenderScale) {
                // Only apply the new render scale when the user releases the
                // thumb (end of drag) or clicks to a new position, instead of
                // reallocating depth/HDR targets continuously while dragging.
                if (scrollCode == TB_ENDTRACK || scrollCode == TB_THUMBPOSITION) {
                    float scale = SliderToFloat(slider, 0.5f, 1.0f);
                    if (scale < 0.5f) scale = 0.5f;
                    if (scale > 1.0f) scale = 1.0f;

                    float current = renderer->GetRenderScale();
                    if (std::fabs(scale - current) > 0.01f) {
                        renderer->SetRenderScale(scale);
                    }
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED) {
                int id = LOWORD(wParam);

                auto* renderer = Cortex::ServiceLocator::GetRenderer();
                if (!renderer) {
                    return 0;
                }

                switch (id) {
                case IDC_QL_SAFE_PRESET: {
                    renderer->ApplySafeQualityPreset();
                    RefreshControlsFromState();
                    RefreshStatsLabels();
                    break;
                }
                case IDC_QL_RT_MASTER: {
                    bool enabled = GetCheckbox(g_q.chkRTMaster);
                    renderer->SetRayTracingEnabled(enabled);
                    break;
                }
                case IDC_QL_RT_REFLECT: {
                    bool enabled = GetCheckbox(g_q.chkRTRefl);
                    renderer->SetRTReflectionsEnabled(enabled);
                    break;
                }
                case IDC_QL_RT_GI: {
                    bool enabled = GetCheckbox(g_q.chkRTGI);
                    renderer->SetRTGIEnabled(enabled);
                    break;
                }
                case IDC_QL_TAA: {
                    bool enabled = GetCheckbox(g_q.chkTAA);
                    renderer->SetTAAEnabled(enabled);
                    break;
                }
                case IDC_QL_SSR: {
                    bool enabled = GetCheckbox(g_q.chkSSR);
                    renderer->SetSSREnabled(enabled);
                    break;
                }
                case IDC_QL_SSAO: {
                    bool enabled = GetCheckbox(g_q.chkSSAO);
                    renderer->SetSSAOEnabled(enabled);
                    break;
                }
                case IDC_QL_IBL: {
                    bool enabled = GetCheckbox(g_q.chkIBL);
                    renderer->SetIBLEnabled(enabled);
                    break;
                }
                case IDC_QL_FOG: {
                    bool enabled = GetCheckbox(g_q.chkFog);
                    renderer->SetFogEnabled(enabled);
                    break;
                }
                default:
                    break;
                }

                RefreshControlsFromState();
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            QualitySettingsWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_q.hwnd = nullptr;
            g_q.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kQualitySettingsClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_q.initialized || g_q.hwnd) {
        return;
    }

    RegisterQualitySettingsClass();

    int width = 440;
    int height = 360;

    RECT rc{0, 0, width, height};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_q.parent) {
        RECT pr{};
        GetWindowRect(g_q.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_q.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kQualitySettingsClassName,
        L"Cortex Quality / Performance",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y,
        width, height,
        g_q.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_q.hwnd) {
        ShowWindow(g_q.hwnd, SW_HIDE);
        UpdateWindow(g_q.hwnd);
    }
}

} // namespace

void QualitySettingsWindow::Initialize(HWND parent) {
    g_q.parent = parent;
    g_q.initialized = true;
}

void QualitySettingsWindow::Shutdown() {
    if (g_q.hwnd) {
        DestroyWindow(g_q.hwnd);
        g_q.hwnd = nullptr;
    }
    g_q = QualityState{};
}

void QualitySettingsWindow::SetVisible(bool visible) {
    if (!g_q.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_q.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromState();
        RefreshStatsLabels();
        ShowWindow(g_q.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_q.hwnd);
        g_q.visible = true;
    } else {
        ShowWindow(g_q.hwnd, SW_HIDE);
        g_q.visible = false;
    }
}

void QualitySettingsWindow::Toggle() {
    if (!g_q.initialized) {
        return;
    }
    SetVisible(!g_q.visible);
}

bool QualitySettingsWindow::IsVisible() {
    return g_q.visible;
}

} // namespace Cortex::UI
