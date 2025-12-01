#include "QuickSettingsWindow.h"

#include "UI/DebugMenu.h"
#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Graphics/Renderer.h"

#include <commctrl.h>

namespace Cortex::UI {

namespace {

// Control identifiers for the quick settings window.
enum ControlIdQuick : int {
    IDC_QS_EXPOSURE    = 2001,
    IDC_QS_BLOOM       = 2002,
    IDC_QS_CAM_SPEED   = 2003,

    IDC_QS_SHADOWS     = 2101,
    IDC_QS_TAA         = 2102,
    IDC_QS_SSR         = 2103,
    IDC_QS_SSAO        = 2104,
    IDC_QS_IBL         = 2105,
    IDC_QS_FOG         = 2106,
    IDC_QS_RT          = 2107,
    IDC_QS_WATER_STEEPNESS = 2108,
    IDC_QS_FOG_DENSITY     = 2109,
    IDC_QS_GODRAYS         = 2110,
    IDC_QS_AREA_SIZE       = 2111,

    IDC_QS_DEBUGVIEW   = 2201,
    IDC_QS_ENV_NEXT    = 2202,
    IDC_QS_AUTODEMO    = 2203,
    IDC_QS_SCENE_TOGGLE = 2204,
};

struct QuickSettingsState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HFONT font = nullptr;

    HWND sliderExposure = nullptr;
    HWND sliderBloom = nullptr;
    HWND sliderCameraSpeed = nullptr;
    HWND sliderWaterSteepness = nullptr;
    HWND sliderFogDensity = nullptr;
    HWND sliderGodRays = nullptr;
    HWND sliderAreaSize = nullptr;

    HWND chkShadows = nullptr;
    HWND chkTAA = nullptr;
    HWND chkSSR = nullptr;
    HWND chkSSAO = nullptr;
    HWND chkIBL = nullptr;
    HWND chkFog = nullptr;
    HWND chkRT = nullptr;

    HWND btnDebugView = nullptr;
    HWND btnEnvNext = nullptr;
    HWND btnAutoDemo = nullptr;
    HWND btnSceneToggle = nullptr;
};

QuickSettingsState g_qs;

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
    pos = (pos < 0) ? 0 : (pos > 100 ? 100 : pos);
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

const wchar_t* kQuickSettingsClassName = L"CortexQuickSettingsWindow";

void RefreshControlsFromState() {
    if (!g_qs.hwnd) {
        return;
    }

    DebugMenuState s = DebugMenu::GetState();

    SetSliderFromFloat(g_qs.sliderExposure,    s.exposure,        0.0f, 10.0f);
    SetSliderFromFloat(g_qs.sliderBloom,       s.bloomIntensity,  0.0f, 5.0f);
    SetSliderFromFloat(g_qs.sliderCameraSpeed, s.cameraBaseSpeed, 0.5f, 25.0f);

    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (renderer) {
        SetSliderFromFloat(g_qs.sliderWaterSteepness, renderer->GetWaterSteepness(), 0.0f, 1.0f);
        SetSliderFromFloat(g_qs.sliderFogDensity,     renderer->GetFogDensity(),      0.0f, 0.1f);
        SetSliderFromFloat(g_qs.sliderGodRays,        renderer->GetGodRayIntensity(), 0.0f, 3.0f);
        SetSliderFromFloat(g_qs.sliderAreaSize,       renderer->GetAreaLightSizeScale(), 0.25f, 2.0f);
    }

    SetCheckbox(g_qs.chkShadows, s.shadowsEnabled);
    SetCheckbox(g_qs.chkTAA,     s.taaEnabled);
    SetCheckbox(g_qs.chkSSR,     s.ssrEnabled);
    SetCheckbox(g_qs.chkSSAO,    s.ssaoEnabled);
    SetCheckbox(g_qs.chkIBL,     s.iblEnabled);
    SetCheckbox(g_qs.chkFog,     s.fogEnabled);
    SetCheckbox(g_qs.chkRT,      s.rayTracingEnabled);

    // Button labels that depend on current engine state.
    if (renderer && g_qs.btnDebugView) {
        int mode = renderer->GetDebugViewMode();
        const wchar_t* label = L"Debug View";
        switch (mode) {
            case 0:  label = L"Debug View: Shaded"; break;
            case 6:  label = L"Debug View: DebugScreen"; break;
            case 13: label = L"Debug View: SSAO"; break;
            case 15: label = L"Debug View: SSR"; break;
            case 25: label = L"Debug View: TAA"; break;
            default: label = L"Debug View: Other"; break;
        }
        SetWindowTextW(g_qs.btnDebugView, label);
    }

    if (renderer && g_qs.btnEnvNext) {
        std::string envNameUtf8 = renderer->GetCurrentEnvironmentName();
        std::wstring envName;
        envName.assign(envNameUtf8.begin(), envNameUtf8.end());
        std::wstring btnLabel = L"Environment: ";
        btnLabel += envName.empty() ? L"<none>" : envName;
        SetWindowTextW(g_qs.btnEnvNext, btnLabel.c_str());
    }

    if (g_qs.btnAutoDemo) {
        auto* engine = Cortex::ServiceLocator::GetEngine();
        bool autoDemo = false;
        if (engine) {
            // Engine does not expose a public getter; infer from button text.
            // Keep label simple and stateless here.
            autoDemo = false;
        }
        SetWindowTextW(g_qs.btnAutoDemo, autoDemo ? L"Auto Demo: On" : L"Auto Demo: Toggle");
    }
}

void RegisterQuickSettingsClass() {
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
        case WM_NCCREATE:
            break;
        case WM_CREATE: {
            g_qs.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

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
            int colLabelWidth = 120;
            int colSliderWidth = width - colLabelWidth - margin * 2;

            auto makeLabel = [&](const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, colLabelWidth - 4, labelHeight,
                    hwnd, nullptr, nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_qs.font), TRUE);
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
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_qs.font), TRUE);
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, yy, width - margin * 2, 24,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_qs.font), TRUE);
                return h;
            };

            // Sliders
            makeLabel(L"Exposure", y);
            g_qs.sliderExposure = makeSlider(IDC_QS_EXPOSURE, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Bloom Intensity", y);
            g_qs.sliderBloom = makeSlider(IDC_QS_BLOOM, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Camera Speed", y);
            g_qs.sliderCameraSpeed = makeSlider(IDC_QS_CAM_SPEED, y);
            y += sliderHeight + rowGap * 2;

            makeLabel(L"Water Steepness", y);
            g_qs.sliderWaterSteepness = makeSlider(IDC_QS_WATER_STEEPNESS, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Fog Density", y);
            g_qs.sliderFogDensity = makeSlider(IDC_QS_FOG_DENSITY, y);
            y += sliderHeight + rowGap;

            makeLabel(L"God-Ray Intensity", y);
            g_qs.sliderGodRays = makeSlider(IDC_QS_GODRAYS, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Area Light Size", y);
            g_qs.sliderAreaSize = makeSlider(IDC_QS_AREA_SIZE, y);
            y += sliderHeight + rowGap * 2;

            // Checkboxes
            g_qs.chkShadows = makeCheckbox(IDC_QS_SHADOWS, L"Shadows", y);
            y += checkHeight + rowGap;
            g_qs.chkTAA = makeCheckbox(IDC_QS_TAA, L"TAA", y);
            y += checkHeight + rowGap;
            g_qs.chkSSR = makeCheckbox(IDC_QS_SSR, L"Screen-Space Reflections", y);
            y += checkHeight + rowGap;
            g_qs.chkSSAO = makeCheckbox(IDC_QS_SSAO, L"SSAO", y);
            y += checkHeight + rowGap;
            g_qs.chkIBL = makeCheckbox(IDC_QS_IBL, L"Image-Based Lighting (IBL)", y);
            y += checkHeight + rowGap;
            g_qs.chkFog = makeCheckbox(IDC_QS_FOG, L"Height Fog", y);
            y += checkHeight + rowGap;
            g_qs.chkRT = makeCheckbox(IDC_QS_RT, L"Ray Tracing (DXR)", y);
            y += checkHeight + rowGap * 2;

            // Action buttons
            g_qs.btnDebugView = makeButton(IDC_QS_DEBUGVIEW, L"Debug View", y);
            y += 24 + rowGap;
            g_qs.btnEnvNext = makeButton(IDC_QS_ENV_NEXT, L"Environment: <cycle>", y);
            y += 24 + rowGap;
            g_qs.btnAutoDemo = makeButton(IDC_QS_AUTODEMO, L"Auto Demo: Toggle", y);
            y += 24 + rowGap;
            g_qs.btnSceneToggle = makeButton(IDC_QS_SCENE_TOGGLE, L"Toggle Scene (Cornell / Dragon)", y);

            RefreshControlsFromState();
            return 0;
        }
        case WM_HSCROLL: {
            HWND slider = reinterpret_cast<HWND>(lParam);
            if (!slider) break;

            DebugMenuState s = DebugMenu::GetState();

            if (slider == g_qs.sliderExposure) {
                s.exposure = SliderToFloat(slider, 0.0f, 10.0f);
            } else if (slider == g_qs.sliderBloom) {
                s.bloomIntensity = SliderToFloat(slider, 0.0f, 5.0f);
            } else if (slider == g_qs.sliderCameraSpeed) {
                s.cameraBaseSpeed = SliderToFloat(slider, 0.5f, 25.0f);
            } else if (slider == g_qs.sliderWaterSteepness ||
                       slider == g_qs.sliderFogDensity     ||
                       slider == g_qs.sliderGodRays        ||
                       slider == g_qs.sliderAreaSize) {
                if (auto* renderer = Cortex::ServiceLocator::GetRenderer()) {
                    if (slider == g_qs.sliderWaterSteepness) {
                        float steep = SliderToFloat(slider, 0.0f, 1.0f);
                        renderer->SetWaterParams(
                            renderer->GetWaterLevel(),
                            renderer->GetWaterWaveAmplitude(),
                            renderer->GetWaterWaveLength(),
                            renderer->GetWaterWaveSpeed(),
                            renderer->GetWaterPrimaryDir().x,
                            renderer->GetWaterPrimaryDir().y,
                            renderer->GetWaterSecondaryAmplitude(),
                            steep);
                    } else if (slider == g_qs.sliderFogDensity) {
                        float density = SliderToFloat(slider, 0.0f, 0.1f);
                        renderer->SetFogParams(
                            density,
                            renderer->GetFogHeight(),
                            renderer->GetFogFalloff());
                    } else if (slider == g_qs.sliderGodRays) {
                        float g = SliderToFloat(slider, 0.0f, 3.0f);
                        renderer->SetGodRayIntensity(g);
                    } else if (slider == g_qs.sliderAreaSize) {
                        float sArea = SliderToFloat(slider, 0.25f, 2.0f);
                        renderer->SetAreaLightSizeScale(sArea);
                    }
                }

                // Renderer-only sliders do not change DebugMenuState; keep it
                // in sync only for the core exposure/bloom/speed values above.
                DebugMenu::SyncFromState(s);
                return 0;
            }

            DebugMenu::SyncFromState(s);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (HIWORD(wParam) == BN_CLICKED) {
                if (id == IDC_QS_DEBUGVIEW) {
                    if (auto* renderer = Cortex::ServiceLocator::GetRenderer()) {
                        renderer->CycleDebugViewMode();
                    }
                    RefreshControlsFromState();
                    return 0;
                }
                if (id == IDC_QS_ENV_NEXT) {
                    if (auto* renderer = Cortex::ServiceLocator::GetRenderer()) {
                        renderer->CycleEnvironmentPreset();
                    }
                    RefreshControlsFromState();
                    return 0;
                }
                if (id == IDC_QS_AUTODEMO) {
                    if (auto* engine = Cortex::ServiceLocator::GetEngine()) {
                        engine->ToggleScenePreset(); // simple attention grabber; reuse preset toggle
                    }
                    RefreshControlsFromState();
                    return 0;
                }
                if (id == IDC_QS_SCENE_TOGGLE) {
                    if (auto* engine = Cortex::ServiceLocator::GetEngine()) {
                        engine->ToggleScenePreset();
                    }
                    RefreshControlsFromState();
                    return 0;
                }

                // Checkboxes -> DebugMenuState
                DebugMenuState s = DebugMenu::GetState();
                switch (id) {
                case IDC_QS_SHADOWS:
                    s.shadowsEnabled = GetCheckbox(g_qs.chkShadows);
                    break;
                case IDC_QS_TAA:
                    s.taaEnabled = GetCheckbox(g_qs.chkTAA);
                    break;
                case IDC_QS_SSR:
                    s.ssrEnabled = GetCheckbox(g_qs.chkSSR);
                    break;
                case IDC_QS_SSAO:
                    s.ssaoEnabled = GetCheckbox(g_qs.chkSSAO);
                    break;
                case IDC_QS_IBL:
                    s.iblEnabled = GetCheckbox(g_qs.chkIBL);
                    break;
                case IDC_QS_FOG:
                    s.fogEnabled = GetCheckbox(g_qs.chkFog);
                    break;
                case IDC_QS_RT:
                    s.rayTracingEnabled = GetCheckbox(g_qs.chkRT);
                    break;
                default:
                    break;
                }

                DebugMenu::SyncFromState(s);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            QuickSettingsWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_qs.hwnd = nullptr;
            g_qs.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kQuickSettingsClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_qs.initialized || g_qs.hwnd) {
        return;
    }

    RegisterQuickSettingsClass();

    int width = 420;
    int height = 420;

    RECT rc{0, 0, width, height};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_qs.parent) {
        RECT pr{};
        GetWindowRect(g_qs.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_qs.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kQuickSettingsClassName,
        L"Cortex Quick Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y,
        width, height,
        g_qs.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_qs.hwnd) {
        ShowWindow(g_qs.hwnd, SW_HIDE);
        UpdateWindow(g_qs.hwnd);
    }
}

} // namespace

void QuickSettingsWindow::Initialize(HWND parent) {
    g_qs.parent = parent;
    g_qs.initialized = true;
}

void QuickSettingsWindow::Shutdown() {
    if (g_qs.hwnd) {
        DestroyWindow(g_qs.hwnd);
        g_qs.hwnd = nullptr;
    }
    g_qs = QuickSettingsState{};
}

void QuickSettingsWindow::SetVisible(bool visible) {
    if (!g_qs.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_qs.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromState();
        ShowWindow(g_qs.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_qs.hwnd);
        g_qs.visible = true;
    } else {
        ShowWindow(g_qs.hwnd, SW_HIDE);
        g_qs.visible = false;
    }
}

void QuickSettingsWindow::Toggle() {
    if (!g_qs.initialized) {
        return;
    }
    SetVisible(!g_qs.visible);
}

bool QuickSettingsWindow::IsVisible() {
    return g_qs.visible;
}

} // namespace Cortex::UI
