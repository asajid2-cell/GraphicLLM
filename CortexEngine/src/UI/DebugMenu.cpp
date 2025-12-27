#include "DebugMenu.h"

#include "Core/ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Core/Engine.h"

#include <commctrl.h>
#include <string>

namespace Cortex::UI {

namespace {

// -----------------------------------------------------------------------------
// Internal state and helpers
// -----------------------------------------------------------------------------

// Control identifiers for the Win32 debug window.
enum ControlId : int {
    IDC_EXPOSURE      = 1001,
    IDC_BLOOM         = 1002,
    IDC_SHADOW_BIAS   = 1003,
    IDC_SHADOW_PCF    = 1004,
    IDC_CASCADE_LAMBDA = 1005,
    IDC_CASCADE0_RES   = 1006,
    IDC_CAMERA_SPEED   = 1007,

    IDC_SHADOWS       = 1101,
    IDC_PCSS          = 1102,
    IDC_FXAA          = 1103,
    IDC_TAA           = 1104,
    IDC_SSR           = 1105,
    IDC_SSAO          = 1106,
    IDC_IBL           = 1107,
    IDC_FOG           = 1108,
    IDC_RAYTRACING    = 1109,

    IDC_RESET         = 1201,
    IDC_SCENE_TOGGLE  = 1202,
};

struct DebugMenuInternalState {
    DebugMenuState current{};
    DebugMenuState defaults{};
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    // Win32 window + controls
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HWND sliderExposure = nullptr;
    HWND sliderBloom = nullptr;
    HWND sliderShadowBias = nullptr;
    HWND sliderShadowPCF = nullptr;
    HWND sliderCascadeLambda = nullptr;
    HWND sliderCascade0Res = nullptr;
    HWND sliderCameraSpeed = nullptr;
    HWND chkShadows = nullptr;
    HWND chkPCSS = nullptr;
    HWND chkFXAA = nullptr;
    HWND chkTAA = nullptr;
    HWND chkSSR = nullptr;
    HWND chkSSAO = nullptr;
    HWND chkIBL = nullptr;
    HWND chkFog = nullptr;
    HWND chkRT = nullptr;
    HWND btnReset = nullptr;
    HWND btnSceneToggle = nullptr;

    int contentHeight = 0;
    int scrollPos = 0;
};

DebugMenuInternalState g_state;

// Simple helper to map slider position in [0,100] to a float range.
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

// Apply the current state into the renderer.
void ApplyStateToRenderer(const DebugMenuState& state) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    renderer->SetExposure(state.exposure);
    renderer->SetShadowBias(state.shadowBias);
    renderer->SetShadowPCFRadius(state.shadowPCFRadius);
    renderer->SetCascadeSplitLambda(state.cascadeLambda);

    float currentScale = renderer->GetCascadeResolutionScale(0);
    float targetScale = state.cascade0ResolutionScale;
    renderer->AdjustCascadeResolutionScale(0, targetScale - currentScale);

    renderer->SetBloomIntensity(state.bloomIntensity);

    renderer->SetFractalParams(
        state.fractalAmplitude,
        state.fractalFrequency,
        state.fractalOctaves,
        state.fractalCoordMode,
        state.fractalScaleX,
        state.fractalScaleZ,
        state.fractalLacunarity,
        state.fractalGain,
        state.fractalWarpStrength,
        state.fractalNoiseType);

    // Feature toggles
    renderer->SetShadowsEnabled(state.shadowsEnabled);
    renderer->SetPCSS(state.pcssEnabled);
    renderer->SetFXAAEnabled(state.fxaaEnabled);
    renderer->SetTAAEnabled(state.taaEnabled);
    renderer->SetSSREnabled(state.ssrEnabled);
    renderer->SetSSAOEnabled(state.ssaoEnabled);
    renderer->SetIBLEnabled(state.iblEnabled);
    renderer->SetFogEnabled(state.fogEnabled);

    if (renderer->IsRayTracingSupported()) {
        renderer->SetRayTracingEnabled(state.rayTracingEnabled);
    }
}

// Refresh the Win32 controls from g_state.current so that both keyboard
// shortcuts and the F2 window stay in sync.
void RefreshControlsFromState();

// -----------------------------------------------------------------------------
// Win32 window implementation
// -----------------------------------------------------------------------------

const wchar_t* kDebugMenuClassName = L"CortexDebugMenuWindow";

void RegisterDebugMenuClass() {
    static bool registered = false;
    if (registered) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        auto* state = &g_state;

        switch (msg) {
        case WM_NCCREATE: {
            // Nothing special; state is global.
            break;
        }
        case WM_CREATE: {
            state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

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

            auto makeLabel = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, colLabelWidth - 4, labelHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
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
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                return h;
            };

            // Sliders
            makeLabel(0, L"Exposure", y);
            state->sliderExposure = makeSlider(IDC_EXPOSURE, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Bloom Intensity", y);
            state->sliderBloom = makeSlider(IDC_BLOOM, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Shadow Bias", y);
            state->sliderShadowBias = makeSlider(IDC_SHADOW_BIAS, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Shadow PCF Radius", y);
            state->sliderShadowPCF = makeSlider(IDC_SHADOW_PCF, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Cascade Lambda", y);
            state->sliderCascadeLambda = makeSlider(IDC_CASCADE_LAMBDA, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Near Cascade Scale", y);
            state->sliderCascade0Res = makeSlider(IDC_CASCADE0_RES, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Camera Speed", y);
            state->sliderCameraSpeed = makeSlider(IDC_CAMERA_SPEED, y);
            y += sliderHeight + rowGap * 2;

            // Checkboxes
            state->chkShadows = makeCheckbox(IDC_SHADOWS, L"Shadows", y);
            y += checkHeight + rowGap;
            state->chkPCSS = makeCheckbox(IDC_PCSS, L"PCSS Soft Shadows", y);
            y += checkHeight + rowGap;
            state->chkFXAA = makeCheckbox(IDC_FXAA, L"FXAA", y);
            y += checkHeight + rowGap;
            state->chkTAA = makeCheckbox(IDC_TAA, L"TAA", y);
            y += checkHeight + rowGap;
            state->chkSSR = makeCheckbox(IDC_SSR, L"Screen-Space Reflections", y);
            y += checkHeight + rowGap;
            state->chkSSAO = makeCheckbox(IDC_SSAO, L"SSAO", y);
            y += checkHeight + rowGap;
            state->chkIBL = makeCheckbox(IDC_IBL, L"Image-Based Lighting (IBL)", y);
            y += checkHeight + rowGap;
            state->chkFog = makeCheckbox(IDC_FOG, L"Height Fog", y);
            y += checkHeight + rowGap;
            state->chkRT = makeCheckbox(IDC_RAYTRACING, L"Ray Tracing (DXR)", y);
            y += checkHeight + rowGap * 2;

            // Reset button
            state->btnReset = CreateWindowExW(
                0, L"BUTTON", L"Reset to Defaults",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, width - margin * 2, 26,
                hwnd, reinterpret_cast<HMENU>(IDC_RESET), nullptr, nullptr);
            SendMessageW(state->btnReset, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            y += 26 + rowGap;

            // Scene toggle button
            state->btnSceneToggle = CreateWindowExW(
                0, L"BUTTON", L"Toggle Scene (Cornell / Dragon)",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, y, width - margin * 2, 26,
                hwnd, reinterpret_cast<HMENU>(IDC_SCENE_TOGGLE), nullptr, nullptr);
            SendMessageW(state->btnSceneToggle, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            y += 26 + rowGap;

            state->contentHeight = y + margin;
            state->scrollPos = 0;

            int clientHeight = rc.bottom - rc.top;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = (state->contentHeight > 0 ? state->contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = 0;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            // Populate initial positions
            RefreshControlsFromState();
            return 0;
        }
        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int clientHeight = rc.bottom - rc.top;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = (state->contentHeight > 0 ? state->contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = state->scrollPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int yPos = si.nPos;

            switch (LOWORD(wParam)) {
            case SB_LINEUP:   yPos -= 20; break;
            case SB_LINEDOWN: yPos += 20; break;
            case SB_PAGEUP:   yPos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: yPos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                yPos = si.nTrackPos;
                break;
            default:
                break;
            }

            if (yPos < si.nMin) yPos = si.nMin;
            if (yPos > static_cast<int>(si.nMax - si.nPage + 1)) {
                yPos = static_cast<int>(si.nMax - si.nPage + 1);
            }
            if (yPos < si.nMin) yPos = si.nMin;

            si.fMask = SIF_POS;
            si.nPos = yPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            int dy = state->scrollPos - yPos;
            if (dy != 0) {
                ScrollWindowEx(hwnd,
                               0,
                               dy,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               SW_INVALIDATE | SW_SCROLLCHILDREN);
                state->scrollPos = yPos;
            }
            return 0;
        }
        case WM_HSCROLL: {
            // Slider changed
            HWND slider = reinterpret_cast<HWND>(lParam);
            if (!slider) break;

            DebugMenuState& s = state->current;

            if (slider == state->sliderExposure) {
                s.exposure = SliderToFloat(slider, 0.0f, 10.0f);
            } else if (slider == state->sliderBloom) {
                s.bloomIntensity = SliderToFloat(slider, 0.0f, 5.0f);
            } else if (slider == state->sliderShadowBias) {
                s.shadowBias = SliderToFloat(slider, 0.00005f, 0.01f);
            } else if (slider == state->sliderShadowPCF) {
                s.shadowPCFRadius = SliderToFloat(slider, 0.0f, 5.0f);
            } else if (slider == state->sliderCascadeLambda) {
                s.cascadeLambda = SliderToFloat(slider, 0.0f, 1.0f);
            } else if (slider == state->sliderCascade0Res) {
                s.cascade0ResolutionScale = SliderToFloat(slider, 0.25f, 2.0f);
            } else if (slider == state->sliderCameraSpeed) {
                s.cameraBaseSpeed = SliderToFloat(slider, 0.5f, 25.0f);
            }

            ApplyStateToRenderer(s);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (HIWORD(wParam) == BN_CLICKED) {
                DebugMenuState& s = state->current;

                switch (id) {
                case IDC_SHADOWS:
                    s.shadowsEnabled = GetCheckbox(state->chkShadows);
                    break;
                case IDC_PCSS:
                    s.pcssEnabled = GetCheckbox(state->chkPCSS);
                    break;
                case IDC_FXAA:
                    s.fxaaEnabled = GetCheckbox(state->chkFXAA);
                    break;
                case IDC_TAA:
                    s.taaEnabled = GetCheckbox(state->chkTAA);
                    break;
                case IDC_SSR:
                    s.ssrEnabled = GetCheckbox(state->chkSSR);
                    break;
                case IDC_SSAO:
                    s.ssaoEnabled = GetCheckbox(state->chkSSAO);
                    break;
                case IDC_IBL:
                    s.iblEnabled = GetCheckbox(state->chkIBL);
                    break;
                case IDC_FOG:
                    s.fogEnabled = GetCheckbox(state->chkFog);
                    break;
                case IDC_RAYTRACING:
                    s.rayTracingEnabled = GetCheckbox(state->chkRT);
                    break;
                case IDC_SCENE_TOGGLE: {
                    // Toggle scene preset via the global engine pointer.
                    if (auto* engine = Cortex::ServiceLocator::GetEngine()) {
                        engine->ToggleScenePreset();
                    }
                    return 0;
                }
                case IDC_RESET:
                    DebugMenu::ResetToDefaults();
                    RefreshControlsFromState();
                    return 0;
                default:
                    break;
                }

                ApplyStateToRenderer(s);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DebugMenu::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            // The engine will recreate the window on demand.
            state->hwnd = nullptr;
            state->visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kDebugMenuClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_state.initialized || g_state.hwnd) {
        return;
    }

    RegisterDebugMenuClass();

    // Create a modest-sized tool window centered on the parent (if available).
    int width = 420;
    int height = 520;

    RECT rc{ 0, 0, width, height };
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_state.parent) {
        RECT pr{};
        GetWindowRect(g_state.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    OffsetRect(&rc, x, y);

    g_state.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kDebugMenuClassName,
        L"Cortex Renderer Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
        rc.left, rc.top,
        rc.right - rc.left, rc.bottom - rc.top,
        g_state.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_state.hwnd) {
        ShowWindow(g_state.hwnd, SW_HIDE);
        UpdateWindow(g_state.hwnd);
    }
}

void RefreshControlsFromState() {
    if (!g_state.hwnd) return;

    const DebugMenuState& s = g_state.current;

    SetSliderFromFloat(g_state.sliderExposure,      s.exposure,              0.0f,    10.0f);
    SetSliderFromFloat(g_state.sliderBloom,         s.bloomIntensity,        0.0f,    5.0f);
    SetSliderFromFloat(g_state.sliderShadowBias,    s.shadowBias,            0.00005f, 0.01f);
    SetSliderFromFloat(g_state.sliderShadowPCF,     s.shadowPCFRadius,       0.0f,    5.0f);
    SetSliderFromFloat(g_state.sliderCascadeLambda, s.cascadeLambda,         0.0f,    1.0f);
    SetSliderFromFloat(g_state.sliderCascade0Res,   s.cascade0ResolutionScale, 0.25f, 2.0f);
    SetSliderFromFloat(g_state.sliderCameraSpeed,   s.cameraBaseSpeed,       0.5f,    25.0f);

    SetCheckbox(g_state.chkShadows,   s.shadowsEnabled);
    SetCheckbox(g_state.chkPCSS,      s.pcssEnabled);
    SetCheckbox(g_state.chkFXAA,      s.fxaaEnabled);
    SetCheckbox(g_state.chkTAA,       s.taaEnabled);
    SetCheckbox(g_state.chkSSR,       s.ssrEnabled);
    SetCheckbox(g_state.chkSSAO,      s.ssaoEnabled);
    SetCheckbox(g_state.chkIBL,       s.iblEnabled);
    SetCheckbox(g_state.chkFog,       s.fogEnabled);
    SetCheckbox(g_state.chkRT,        s.rayTracingEnabled);
}

} // namespace

// -----------------------------------------------------------------------------
// Public DebugMenu API
// -----------------------------------------------------------------------------

void DebugMenu::Initialize(HWND parent, const DebugMenuState& initialState) {
    g_state.parent = parent;
    g_state.current = initialState;
    g_state.defaults = initialState;
    g_state.initialized = true;
    g_state.visible = false;

    ApplyStateToRenderer(g_state.current);
}

void DebugMenu::Shutdown() {
    if (g_state.hwnd) {
        DestroyWindow(g_state.hwnd);
        g_state.hwnd = nullptr;
    }
    g_state = DebugMenuInternalState{};
}

void DebugMenu::Toggle() {
    if (!g_state.initialized) {
        return;
    }
    SetVisible(!g_state.visible);
}

void DebugMenu::SetVisible(bool visible) {
    if (!g_state.initialized) {
        return;
    }

    if (visible) {
        EnsureWindowCreated();
        if (g_state.hwnd) {
            RefreshControlsFromState();
            ShowWindow(g_state.hwnd, SW_SHOWNORMAL);
            SetForegroundWindow(g_state.hwnd);
            g_state.visible = true;
        }
    } else {
        if (g_state.hwnd) {
            ShowWindow(g_state.hwnd, SW_HIDE);
        }
        g_state.visible = false;
    }
}

bool DebugMenu::IsVisible() {
    return g_state.visible;
}

void DebugMenu::SyncFromState(const DebugMenuState& state) {
    if (!g_state.initialized) {
        return;
    }
    g_state.current = state;
    ApplyStateToRenderer(g_state.current);
    RefreshControlsFromState();
}

DebugMenuState DebugMenu::GetState() {
    return g_state.current;
}

void DebugMenu::ResetToDefaults() {
    if (!g_state.initialized) {
        return;
    }

    if (auto* renderer = Cortex::ServiceLocator::GetRenderer()) {
        renderer->SetShadowsEnabled(true);
        renderer->SetDebugViewMode(0);
        renderer->SetPCSS(false);
        renderer->SetFXAAEnabled(true);
        renderer->SetTAAEnabled(false);
        renderer->SetSSREnabled(true);
        renderer->SetSSAOEnabled(true);
        renderer->SetIBLEnabled(true);
        renderer->SetFogEnabled(false);
    }

    g_state.current = g_state.defaults;
    g_state.current.shadowsEnabled = true;
    g_state.current.pcssEnabled = false;
    g_state.current.fxaaEnabled = true;
    g_state.current.taaEnabled = false;
    g_state.current.ssrEnabled = true;
    g_state.current.ssaoEnabled = true;
    g_state.current.iblEnabled = true;
    g_state.current.fogEnabled = false;

    ApplyStateToRenderer(g_state.current);
    RefreshControlsFromState();
}

} // namespace Cortex::UI
