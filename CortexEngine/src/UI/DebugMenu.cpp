#include "DebugMenu.h"
#include <commctrl.h>

namespace Cortex::UI {

namespace {

constexpr int ID_SLIDER_EXPOSURE   = 2001;
constexpr int ID_SLIDER_BIAS       = 2002;
constexpr int ID_SLIDER_PCF        = 2003;
constexpr int ID_SLIDER_LAMBDA     = 2004;
constexpr int ID_SLIDER_CASCADE0   = 2005;
constexpr int ID_SLIDER_CAM_SPEED  = 2006;
constexpr int ID_SLIDER_BLOOM      = 2007;

struct DebugMenuInternalState {
    HWND parent = nullptr;
    HWND hwnd = nullptr;
    HWND sliderExposure = nullptr;
    HWND sliderBias = nullptr;
    HWND sliderPCF = nullptr;
    HWND sliderLambda = nullptr;
    HWND sliderCascade0 = nullptr;
    HWND sliderCamSpeed = nullptr;
    HWND sliderBloom = nullptr;
    DebugMenuState state{};
    bool visible = false;
    bool initialized = false;
};

DebugMenuInternalState g_state;

float SliderToExposure(int pos) {
    // 10..500 -> 0.1 .. 5.0
    return static_cast<float>(pos) / 100.0f;
}

int ExposureToSlider(float value) {
    value = (value < 0.1f) ? 0.1f : (value > 5.0f ? 5.0f : value);
    return static_cast<int>(value * 100.0f + 0.5f);
}

float SliderToBias(int pos) {
    // 1..1000 -> 0.00001 .. 0.01 (linear)
    const float minB = 0.00001f;
    const float maxB = 0.01f;
    float t = static_cast<float>(pos - 1) / 999.0f;
    return minB + t * (maxB - minB);
}

int BiasToSlider(float bias) {
    const float minB = 0.00001f;
    const float maxB = 0.01f;
    if (bias < minB) bias = minB;
    if (bias > maxB) bias = maxB;
    float t = (bias - minB) / (maxB - minB);
    return 1 + static_cast<int>(t * 999.0f + 0.5f);
}

float SliderToPCF(int pos) {
    // 5..80 -> 0.5 .. 8.0
    return static_cast<float>(pos) / 10.0f;
}

int PCFToSlider(float radius) {
    if (radius < 0.5f) radius = 0.5f;
    if (radius > 8.0f) radius = 8.0f;
    return static_cast<int>(radius * 10.0f + 0.5f);
}

float SliderToLambda(int pos) {
    // 0..100 -> 0..1
    return static_cast<float>(pos) / 100.0f;
}

int LambdaToSlider(float lambda) {
    if (lambda < 0.0f) lambda = 0.0f;
    if (lambda > 1.0f) lambda = 1.0f;
    return static_cast<int>(lambda * 100.0f + 0.5f);
}

float SliderToCascadeScale(int pos) {
    // 25..200 -> 0.25 .. 2.0
    return static_cast<float>(pos) / 100.0f;
}

int CascadeScaleToSlider(float scale) {
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 2.0f) scale = 2.0f;
    return static_cast<int>(scale * 100.0f + 0.5f);
}

float SliderToCamSpeed(int pos) {
    // 1..30
    return static_cast<float>(pos);
}

int CamSpeedToSlider(float speed) {
    if (speed < 1.0f) speed = 1.0f;
    if (speed > 30.0f) speed = 30.0f;
    return static_cast<int>(speed + 0.5f);
}

float SliderToBloom(int pos) {
    // 0..200 -> 0.0 .. 2.0
    return static_cast<float>(pos) / 100.0f;
}

int BloomToSlider(float intensity) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 2.0f) intensity = 2.0f;
    return static_cast<int>(intensity * 100.0f + 0.5f);
}

LRESULT CALLBACK DebugMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&icc);

        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        auto makeLabel = [&](int x, int y, const wchar_t* text) {
            HWND lbl = CreateWindowExW(
                0, L"STATIC", text,
                WS_CHILD | WS_VISIBLE,
                x, y, 220, 18,
                hwnd, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        };

        auto makeSlider = [&](int x, int y, int id, int min, int max, int pos) -> HWND {
            HWND tb = CreateWindowExW(
                0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                x, y, 220, 30,
                hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(tb, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(tb, TBM_SETRANGE, TRUE, MAKELPARAM(min, max));
            SendMessageW(tb, TBM_SETPOS, TRUE, pos);
            return tb;
        };

        int y = 10;
        makeLabel(10, y, L"Exposure"); y += 16;
        g_state.sliderExposure = makeSlider(10, y, ID_SLIDER_EXPOSURE, 10, 500, ExposureToSlider(g_state.state.exposure)); y += 34;

        makeLabel(10, y, L"Shadow Bias"); y += 16;
        g_state.sliderBias = makeSlider(10, y, ID_SLIDER_BIAS, 1, 1000, BiasToSlider(g_state.state.shadowBias)); y += 34;

        makeLabel(10, y, L"Shadow PCF Radius"); y += 16;
        g_state.sliderPCF = makeSlider(10, y, ID_SLIDER_PCF, 5, 80, PCFToSlider(g_state.state.shadowPCFRadius)); y += 34;

        makeLabel(10, y, L"Cascade Split Lambda"); y += 16;
        g_state.sliderLambda = makeSlider(10, y, ID_SLIDER_LAMBDA, 0, 100, LambdaToSlider(g_state.state.cascadeLambda)); y += 34;

        makeLabel(10, y, L"Cascade 0 Resolution Scale"); y += 16;
        g_state.sliderCascade0 = makeSlider(10, y, ID_SLIDER_CASCADE0, 25, 200, CascadeScaleToSlider(g_state.state.cascade0ResolutionScale)); y += 34;

        makeLabel(10, y, L"Camera Base Speed"); y += 16;
        g_state.sliderCamSpeed = makeSlider(10, y, ID_SLIDER_CAM_SPEED, 1, 30, CamSpeedToSlider(g_state.state.cameraBaseSpeed)); y += 34;

        makeLabel(10, y, L"Bloom Intensity"); y += 16;
        g_state.sliderBloom = makeSlider(10, y, ID_SLIDER_BLOOM, 0, 200, BloomToSlider(g_state.state.bloomIntensity)); y += 40;

        return 0;
    }
    case WM_HSCROLL: {
        HWND src = reinterpret_cast<HWND>(lParam);
        if (!src) break;
        int pos = static_cast<int>(SendMessageW(src, TBM_GETPOS, 0, 0));

        if (src == g_state.sliderExposure) {
            g_state.state.exposure = SliderToExposure(pos);
        } else if (src == g_state.sliderBias) {
            g_state.state.shadowBias = SliderToBias(pos);
        } else if (src == g_state.sliderPCF) {
            g_state.state.shadowPCFRadius = SliderToPCF(pos);
        } else if (src == g_state.sliderLambda) {
            g_state.state.cascadeLambda = SliderToLambda(pos);
        } else if (src == g_state.sliderCascade0) {
            g_state.state.cascade0ResolutionScale = SliderToCascadeScale(pos);
        } else if (src == g_state.sliderCamSpeed) {
            g_state.state.cameraBaseSpeed = SliderToCamSpeed(pos);
        } else if (src == g_state.sliderBloom) {
            g_state.state.bloomIntensity = SliderToBloom(pos);
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        g_state.visible = false;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureClass() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wc{};
    wc.lpfnWndProc = DebugMenuWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CortexDebugMenuWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindow(HWND parent, const DebugMenuState& initialState) {
    if (g_state.initialized) {
        return;
    }

    g_state.parent = parent;
    g_state.state = initialState;

    EnsureClass();

    RECT rc{0, 0, 260, 280};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - (rc.right - rc.left)) / 2;
    int y = (screenH - (rc.bottom - rc.top)) / 2;
    OffsetRect(&rc, x, y);

    g_state.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"CortexDebugMenuWindow",
        L"Debug Controls",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        rc.left, rc.top,
        rc.right - rc.left,
        rc.bottom - rc.top,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_state.hwnd) {
        g_state.initialized = true;
    }
}

} // namespace

void DebugMenu::Initialize(HWND parent, const DebugMenuState& initialState) {
    EnsureWindow(parent, initialState);
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
    if (!g_state.hwnd) {
        return;
    }
    g_state.visible = !g_state.visible;
    ShowWindow(g_state.hwnd, g_state.visible ? SW_SHOW : SW_HIDE);
}

void DebugMenu::SetVisible(bool visible) {
    if (!g_state.initialized || !g_state.hwnd) {
        return;
    }
    g_state.visible = visible;
    ShowWindow(g_state.hwnd, visible ? SW_SHOW : SW_HIDE);
}

bool DebugMenu::IsVisible() {
    return g_state.visible;
}

void DebugMenu::SyncFromState(const DebugMenuState& state) {
    g_state.state = state;
    if (!g_state.hwnd) {
        return;
    }
    if (g_state.sliderExposure) {
        SendMessageW(g_state.sliderExposure, TBM_SETPOS, TRUE, ExposureToSlider(state.exposure));
    }
    if (g_state.sliderBias) {
        SendMessageW(g_state.sliderBias, TBM_SETPOS, TRUE, BiasToSlider(state.shadowBias));
    }
    if (g_state.sliderPCF) {
        SendMessageW(g_state.sliderPCF, TBM_SETPOS, TRUE, PCFToSlider(state.shadowPCFRadius));
    }
    if (g_state.sliderLambda) {
        SendMessageW(g_state.sliderLambda, TBM_SETPOS, TRUE, LambdaToSlider(state.cascadeLambda));
    }
    if (g_state.sliderCascade0) {
        SendMessageW(g_state.sliderCascade0, TBM_SETPOS, TRUE, CascadeScaleToSlider(state.cascade0ResolutionScale));
    }
    if (g_state.sliderCamSpeed) {
        SendMessageW(g_state.sliderCamSpeed, TBM_SETPOS, TRUE, CamSpeedToSlider(state.cameraBaseSpeed));
    }
    if (g_state.sliderBloom) {
        SendMessageW(g_state.sliderBloom, TBM_SETPOS, TRUE, BloomToSlider(state.bloomIntensity));
    }
}

DebugMenuState DebugMenu::GetState() {
    return g_state.state;
}

} // namespace Cortex::UI
