#include "DebugMenu.h"
#include <commctrl.h>
#include <cstdio>
#include <algorithm>
#include "Core/ServiceLocator.h"
#include "Graphics/Renderer.h"

namespace Cortex::UI {

namespace {

constexpr int ID_SLIDER_EXPOSURE        = 2001;
constexpr int ID_SLIDER_BIAS            = 2002;
constexpr int ID_SLIDER_PCF             = 2003;
constexpr int ID_SLIDER_LAMBDA          = 2004;
constexpr int ID_SLIDER_CASCADE0        = 2005;
constexpr int ID_SLIDER_CAM_SPEED       = 2006;
constexpr int ID_SLIDER_BLOOM           = 2007;
constexpr int ID_SLIDER_FRACTAL_AMP     = 2008;
constexpr int ID_SLIDER_FRACTAL_FREQ    = 2009;
constexpr int ID_SLIDER_FRACTAL_OCT     = 2010;
constexpr int ID_SLIDER_FRACTAL_MODE    = 2011;
constexpr int ID_SLIDER_FRACTAL_SCALEX  = 2012;
constexpr int ID_SLIDER_FRACTAL_SCALEZ  = 2013;
constexpr int ID_SLIDER_FRACTAL_LACUN   = 2014;
constexpr int ID_SLIDER_FRACTAL_GAIN    = 2015;
constexpr int ID_SLIDER_FRACTAL_WARP    = 2016;
constexpr int ID_SLIDER_FRACTAL_TYPE    = 2017;
constexpr int ID_BUTTON_RESET_ALL       = 2101;
constexpr int ID_BUTTON_RESET_EXPOSURE  = 2102;
constexpr int ID_BUTTON_RESET_SHADOWS   = 2103;
constexpr int ID_BUTTON_RESET_CAMERA    = 2104;

struct DebugMenuInternalState {
    HWND parent = nullptr;
    HWND hwnd = nullptr;
    HWND content = nullptr;
    int contentHeight = 0;
    int scrollPos = 0;
    WNDPROC contentOrigProc = nullptr;

    // Section headers
    HWND headerExposurePost = nullptr;
    HWND headerShadows = nullptr;
    HWND headerCamera = nullptr;
    HWND headerFractal = nullptr;

    // Labels
    HWND labelExposure = nullptr;
    HWND labelBloom = nullptr;
    HWND labelBias = nullptr;
    HWND labelPCF = nullptr;
    HWND labelLambda = nullptr;
    HWND labelCascade0 = nullptr;
    HWND labelCamSpeed = nullptr;
    HWND labelFractalAmp = nullptr;
    HWND labelFractalFreq = nullptr;
    HWND labelFractalOct = nullptr;
    HWND labelFractalMode = nullptr;
    HWND labelFractalScaleX = nullptr;
    HWND labelFractalScaleZ = nullptr;
    HWND labelFractalLacun = nullptr;
    HWND labelFractalGain = nullptr;
    HWND labelFractalWarp = nullptr;
    HWND labelFractalType = nullptr;

    // Sliders
    HWND sliderExposure = nullptr;
    HWND sliderBias = nullptr;
    HWND sliderPCF = nullptr;
    HWND sliderLambda = nullptr;
    HWND sliderCascade0 = nullptr;
    HWND sliderCamSpeed = nullptr;
    HWND sliderBloom = nullptr;
    HWND sliderFractalAmp = nullptr;
    HWND sliderFractalFreq = nullptr;
    HWND sliderFractalOct = nullptr;
    HWND sliderFractalMode = nullptr;
    HWND sliderFractalScaleX = nullptr;
    HWND sliderFractalScaleZ = nullptr;
    HWND sliderFractalLacun = nullptr;
    HWND sliderFractalGain = nullptr;
    HWND sliderFractalWarp = nullptr;
    HWND sliderFractalType = nullptr;

    // Numeric readouts
    HWND valueExposure = nullptr;
    HWND valueBloom = nullptr;
    HWND valueBias = nullptr;
    HWND valuePCF = nullptr;
    HWND valueLambda = nullptr;
    HWND valueCascade0 = nullptr;
    HWND valueCamSpeed = nullptr;
    HWND valueFractalAmp = nullptr;
    HWND valueFractalFreq = nullptr;
    HWND valueFractalOct = nullptr;
    HWND valueFractalMode = nullptr;
    HWND valueFractalScaleX = nullptr;
    HWND valueFractalScaleZ = nullptr;
    HWND valueFractalLacun = nullptr;
    HWND valueFractalGain = nullptr;
    HWND valueFractalWarp = nullptr;
    HWND valueFractalType = nullptr;

    HWND buttonResetAll = nullptr;
    HWND buttonResetExposure = nullptr;
    HWND buttonResetShadows = nullptr;
    HWND buttonResetCamera = nullptr;

    DebugMenuState state{};
    DebugMenuState defaultState{};
    bool visible = false;
    bool initialized = false;
};

DebugMenuInternalState g_state;

LRESULT CALLBACK ContentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Forward scroll messages to the main debug window so sliders behave as before.
    if (msg == WM_HSCROLL || msg == WM_VSCROLL) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            return SendMessageW(parent, msg, wParam, lParam);
        }
    }

    if (g_state.contentOrigProc) {
        return CallWindowProcW(g_state.contentOrigProc, hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

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

float SliderToFractalAmplitude(int pos) {
    // 0..500 -> 0.0 .. 0.5
    return static_cast<float>(pos) / 1000.0f;
}

int FractalAmplitudeToSlider(float amplitude) {
    if (amplitude < 0.0f) amplitude = 0.0f;
    if (amplitude > 0.5f) amplitude = 0.5f;
    return static_cast<int>(amplitude * 1000.0f + 0.5f);
}

float SliderToFractalFrequency(int pos) {
    // 1..40 -> 0.1 .. 4.0
    return static_cast<float>(pos) / 10.0f;
}

int FractalFrequencyToSlider(float frequency) {
    if (frequency < 0.1f) frequency = 0.1f;
    if (frequency > 4.0f) frequency = 4.0f;
    return static_cast<int>(frequency * 10.0f + 0.5f);
}

int SliderToFractalOctaves(int pos) {
    if (pos < 1) pos = 1;
    if (pos > 6) pos = 6;
    return pos;
}

int FractalOctavesToSlider(float octaves) {
    int o = static_cast<int>(octaves + 0.5f);
    if (o < 1) o = 1;
    if (o > 6) o = 6;
    return o;
}

float SliderToFractalScale(int pos) {
    // 1..40 -> 0.1 .. 4.0
    return static_cast<float>(pos) / 10.0f;
}

int FractalScaleToSlider(float scale) {
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 4.0f) scale = 4.0f;
    return static_cast<int>(scale * 10.0f + 0.5f);
}

void ApplyStateToControls(const DebugMenuState& state) {
    g_state.state = state;
    if (!g_state.hwnd) {
        return;
    }

    // Update slider positions
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
    if (g_state.sliderFractalAmp) {
        SendMessageW(g_state.sliderFractalAmp, TBM_SETPOS, TRUE, FractalAmplitudeToSlider(state.fractalAmplitude));
    }
    if (g_state.sliderFractalFreq) {
        SendMessageW(g_state.sliderFractalFreq, TBM_SETPOS, TRUE, FractalFrequencyToSlider(state.fractalFrequency));
    }
    if (g_state.sliderFractalOct) {
        SendMessageW(g_state.sliderFractalOct, TBM_SETPOS, TRUE, FractalOctavesToSlider(state.fractalOctaves));
    }
    if (g_state.sliderFractalMode) {
        int modePos = (state.fractalCoordMode >= 0.5f) ? 1 : 0;
        SendMessageW(g_state.sliderFractalMode, TBM_SETPOS, TRUE, modePos);
    }
    if (g_state.sliderFractalScaleX) {
        SendMessageW(g_state.sliderFractalScaleX, TBM_SETPOS, TRUE, FractalScaleToSlider(state.fractalScaleX));
    }
    if (g_state.sliderFractalScaleZ) {
        SendMessageW(g_state.sliderFractalScaleZ, TBM_SETPOS, TRUE, FractalScaleToSlider(state.fractalScaleZ));
    }
    if (g_state.sliderFractalLacun) {
        int pos = static_cast<int>(std::clamp(state.fractalLacunarity, 1.0f, 4.0f) * 10.0f + 0.5f); // 1.0..4.0 -> 10..40
        SendMessageW(g_state.sliderFractalLacun, TBM_SETPOS, TRUE, pos);
    }
    if (g_state.sliderFractalGain) {
        int pos = static_cast<int>(std::clamp(state.fractalGain, 0.1f, 0.9f) * 100.0f + 0.5f); // 0.1..0.9 -> 10..90
        SendMessageW(g_state.sliderFractalGain, TBM_SETPOS, TRUE, pos);
    }
    if (g_state.sliderFractalWarp) {
        int pos = static_cast<int>(std::clamp(state.fractalWarpStrength, 0.0f, 1.0f) * 100.0f + 0.5f); // 0..1 -> 0..100
        SendMessageW(g_state.sliderFractalWarp, TBM_SETPOS, TRUE, pos);
    }
    if (g_state.sliderFractalType) {
        int mode = static_cast<int>(state.fractalNoiseType + 0.5f);
        if (mode < 0) mode = 0;
        if (mode > 2) mode = 2;
        SendMessageW(g_state.sliderFractalType, TBM_SETPOS, TRUE, mode);
    }

    // Update numeric labels
    wchar_t buffer[64];
    if (g_state.valueExposure) {
        swprintf(buffer, L"%.2f", state.exposure);
        SetWindowTextW(g_state.valueExposure, buffer);
    }
    if (g_state.valueBloom) {
        swprintf(buffer, L"%.2f", state.bloomIntensity);
        SetWindowTextW(g_state.valueBloom, buffer);
    }
    if (g_state.valueBias) {
        swprintf(buffer, L"%.6f", state.shadowBias);
        SetWindowTextW(g_state.valueBias, buffer);
    }
    if (g_state.valuePCF) {
        swprintf(buffer, L"%.2f", state.shadowPCFRadius);
        SetWindowTextW(g_state.valuePCF, buffer);
    }
    if (g_state.valueLambda) {
        swprintf(buffer, L"%.2f", state.cascadeLambda);
        SetWindowTextW(g_state.valueLambda, buffer);
    }
    if (g_state.valueCascade0) {
        swprintf(buffer, L"%.2f", state.cascade0ResolutionScale);
        SetWindowTextW(g_state.valueCascade0, buffer);
    }
    if (g_state.valueCamSpeed) {
        swprintf(buffer, L"%.1f", state.cameraBaseSpeed);
        SetWindowTextW(g_state.valueCamSpeed, buffer);
    }
    if (g_state.valueFractalAmp) {
        swprintf(buffer, L"%.3f", state.fractalAmplitude);
        SetWindowTextW(g_state.valueFractalAmp, buffer);
    }
    if (g_state.valueFractalFreq) {
        swprintf(buffer, L"%.2f", state.fractalFrequency);
        SetWindowTextW(g_state.valueFractalFreq, buffer);
    }
    if (g_state.valueFractalOct) {
        swprintf(buffer, L"%.0f", state.fractalOctaves);
        SetWindowTextW(g_state.valueFractalOct, buffer);
    }
    if (g_state.valueFractalMode) {
        const wchar_t* modeText = (state.fractalCoordMode >= 0.5f) ? L"WorldXZ" : L"UV";
        SetWindowTextW(g_state.valueFractalMode, modeText);
    }
    if (g_state.valueFractalScaleX) {
        swprintf(buffer, L"%.2f", state.fractalScaleX);
        SetWindowTextW(g_state.valueFractalScaleX, buffer);
    }
    if (g_state.valueFractalScaleZ) {
        swprintf(buffer, L"%.2f", state.fractalScaleZ);
        SetWindowTextW(g_state.valueFractalScaleZ, buffer);
    }
    if (g_state.valueFractalLacun) {
        swprintf(buffer, L"%.2f", state.fractalLacunarity);
        SetWindowTextW(g_state.valueFractalLacun, buffer);
    }
    if (g_state.valueFractalGain) {
        swprintf(buffer, L"%.2f", state.fractalGain);
        SetWindowTextW(g_state.valueFractalGain, buffer);
    }
    if (g_state.valueFractalWarp) {
        swprintf(buffer, L"%.2f", state.fractalWarpStrength);
        SetWindowTextW(g_state.valueFractalWarp, buffer);
    }
    if (g_state.valueFractalType) {
        const wchar_t* label = L"FBM";
        int mode = static_cast<int>(state.fractalNoiseType + 0.5f);
        if (mode == 1) label = L"Ridged";
        else if (mode == 2) label = L"Turbulence";
        SetWindowTextW(g_state.valueFractalType, label);
    }
}

void ApplyCurrentStateToRenderer() {
    auto* renderer = ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    const DebugMenuState& s = g_state.state;
    renderer->SetExposure(s.exposure);
    renderer->SetShadowBias(s.shadowBias);
    renderer->SetShadowPCFRadius(s.shadowPCFRadius);
    renderer->SetCascadeSplitLambda(s.cascadeLambda);

    float currentScale = renderer->GetCascadeResolutionScale(0);
    float targetScale = s.cascade0ResolutionScale;
    renderer->AdjustCascadeResolutionScale(0, targetScale - currentScale);

    renderer->SetBloomIntensity(s.bloomIntensity);
    renderer->SetFractalParams(
        s.fractalAmplitude,
        s.fractalFrequency,
        s.fractalOctaves,
        s.fractalCoordMode,
        s.fractalScaleX,
        s.fractalScaleZ,
        s.fractalLacunarity,
        s.fractalGain,
        s.fractalWarpStrength,
        s.fractalNoiseType);
}

LRESULT CALLBACK DebugMenuWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{};
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_BAR_CLASSES;
        InitCommonControlsEx(&icc);

        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        RECT client{};
        GetClientRect(hwnd, &client);
        int clientWidth = client.right - client.left;

        g_state.content = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            clientWidth,
            client.bottom - client.top,
            hwnd,
            nullptr,
            nullptr,
            nullptr);
        SendMessageW(g_state.content, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_state.contentOrigProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_state.content, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ContentWndProc)));

        int marginX = 10;
        int marginY = 10;
        int labelHeight = 18;
        int sliderHeight = 26;
        int valueWidth = 80;
        int gap = 8;
        int rowSpacing = 10;
        int sectionSpacing = 14;

        auto makeHeader = [&](int& y, const wchar_t* text) -> HWND {
            HWND header = CreateWindowExW(
                0, L"STATIC", text,
                WS_CHILD | WS_VISIBLE,
                marginX, y, clientWidth - 2 * marginX, labelHeight,
                g_state.content, nullptr, nullptr, nullptr);
            SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            y += labelHeight + sectionSpacing;
            return header;
        };

        auto makeSliderRow = [&](int& y,
                                 const wchar_t* labelText,
                                 int sliderId,
                                 int min,
                                 int max,
                                 int pos,
                                 HWND& labelOut,
                                 HWND& sliderOut,
                                 HWND& valueOut) {
            int sliderWidth = clientWidth - 2 * marginX - valueWidth - gap;

            labelOut = CreateWindowExW(
                0, L"STATIC", labelText,
                WS_CHILD | WS_VISIBLE,
                marginX, y, clientWidth - 2 * marginX, labelHeight,
                g_state.content, nullptr, nullptr, nullptr);
            SendMessageW(labelOut, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            y += labelHeight + 2;

            sliderOut = CreateWindowExW(
                0, TRACKBAR_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS,
                marginX, y, sliderWidth, sliderHeight,
                g_state.content, reinterpret_cast<HMENU>(sliderId), nullptr, nullptr);
            SendMessageW(sliderOut, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(sliderOut, TBM_SETRANGE, TRUE, MAKELPARAM(min, max));
            SendMessageW(sliderOut, TBM_SETPOS, TRUE, pos);

            int valueX = marginX + sliderWidth + gap;
            valueOut = CreateWindowExW(
                0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                valueX, y, valueWidth, sliderHeight,
                g_state.content, nullptr, nullptr, nullptr);
            SendMessageW(valueOut, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            y += sliderHeight + rowSpacing;
        };

        int y = marginY;

        g_state.headerExposurePost = makeHeader(y, L"Exposure & Post-process");

        int headerButtonWidth = 60;
        int headerButtonHeight = 20;
        int headerButtonX = clientWidth - marginX - headerButtonWidth;
        int headerButtonY = marginY;

        g_state.buttonResetExposure = CreateWindowExW(
            0, L"BUTTON", L"Reset",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            headerButtonX, headerButtonY, headerButtonWidth, headerButtonHeight,
            hwnd, reinterpret_cast<HMENU>(ID_BUTTON_RESET_EXPOSURE), nullptr, nullptr);
        SendMessageW(g_state.buttonResetExposure, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        makeSliderRow(
            y,
            L"Exposure (EV)",
            ID_SLIDER_EXPOSURE,
            10, 500,
            ExposureToSlider(g_state.state.exposure),
            g_state.labelExposure,
            g_state.sliderExposure,
            g_state.valueExposure);

        makeSliderRow(
            y,
            L"Bloom Intensity",
            ID_SLIDER_BLOOM,
            0, 200,
            BloomToSlider(g_state.state.bloomIntensity),
            g_state.labelBloom,
            g_state.sliderBloom,
            g_state.valueBloom);

        y += sectionSpacing / 2;
        int shadowsHeaderY = y;
        g_state.headerShadows = makeHeader(y, L"Shadows & Cascades");

        g_state.buttonResetShadows = CreateWindowExW(
            0, L"BUTTON", L"Reset",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            headerButtonX, shadowsHeaderY, headerButtonWidth, headerButtonHeight,
            hwnd, reinterpret_cast<HMENU>(ID_BUTTON_RESET_SHADOWS), nullptr, nullptr);
        SendMessageW(g_state.buttonResetShadows, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        makeSliderRow(
            y,
            L"Shadow Bias (1e-5 \u2013 1e-2)",
            ID_SLIDER_BIAS,
            1, 1000,
            BiasToSlider(g_state.state.shadowBias),
            g_state.labelBias,
            g_state.sliderBias,
            g_state.valueBias);

        makeSliderRow(
            y,
            L"Shadow PCF Radius (texels)",
            ID_SLIDER_PCF,
            5, 80,
            PCFToSlider(g_state.state.shadowPCFRadius),
            g_state.labelPCF,
            g_state.sliderPCF,
            g_state.valuePCF);

        makeSliderRow(
            y,
            L"Cascade \u03bb (0=linear, 1=log)",
            ID_SLIDER_LAMBDA,
            0, 100,
            LambdaToSlider(g_state.state.cascadeLambda),
            g_state.labelLambda,
            g_state.sliderLambda,
            g_state.valueLambda);

        makeSliderRow(
            y,
            L"Near Cascade Resolution Scale",
            ID_SLIDER_CASCADE0,
            25, 200,
            CascadeScaleToSlider(g_state.state.cascade0ResolutionScale),
            g_state.labelCascade0,
            g_state.sliderCascade0,
            g_state.valueCascade0);

        y += sectionSpacing / 2;
        g_state.headerFractal = makeHeader(y, L"Fractal Surfaces (normal-only)");

        // Amplitude (0..0.5)
        makeSliderRow(
            y,
            L"Fractal Amplitude (0..0.5)",
            ID_SLIDER_FRACTAL_AMP,
            0, 500,
            FractalAmplitudeToSlider(g_state.state.fractalAmplitude),
            g_state.labelFractalAmp,
            g_state.sliderFractalAmp,
            g_state.valueFractalAmp);

        // Frequency (0.1..4.0)
        makeSliderRow(
            y,
            L"Fractal Frequency",
            ID_SLIDER_FRACTAL_FREQ,
            1, 40,
            FractalFrequencyToSlider(g_state.state.fractalFrequency),
            g_state.labelFractalFreq,
            g_state.sliderFractalFreq,
            g_state.valueFractalFreq);

        // Octaves (1..6)
        makeSliderRow(
            y,
            L"Fractal Octaves",
            ID_SLIDER_FRACTAL_OCT,
            1, 6,
            FractalOctavesToSlider(g_state.state.fractalOctaves),
            g_state.labelFractalOct,
            g_state.sliderFractalOct,
            g_state.valueFractalOct);

        // Coord mode: 0 = UV, 1 = World XZ
        makeSliderRow(
            y,
            L"Fractal Coord Mode (0=UV,1=WorldXZ)",
            ID_SLIDER_FRACTAL_MODE,
            0, 1,
            static_cast<int>(g_state.state.fractalCoordMode + 0.5f),
            g_state.labelFractalMode,
            g_state.sliderFractalMode,
            g_state.valueFractalMode);

        // Scale X
        makeSliderRow(
            y,
            L"Fractal Scale X",
            ID_SLIDER_FRACTAL_SCALEX,
            1, 40,
            FractalScaleToSlider(g_state.state.fractalScaleX),
            g_state.labelFractalScaleX,
            g_state.sliderFractalScaleX,
            g_state.valueFractalScaleX);

        // Scale Z
        makeSliderRow(
            y,
            L"Fractal Scale Z",
            ID_SLIDER_FRACTAL_SCALEZ,
            1, 40,
            FractalScaleToSlider(g_state.state.fractalScaleZ),
            g_state.labelFractalScaleZ,
            g_state.sliderFractalScaleZ,
            g_state.valueFractalScaleZ);

        // Lacunarity
        makeSliderRow(
            y,
            L"Lacunarity (freq multiplier)",
            ID_SLIDER_FRACTAL_LACUN,
            10, 40, // 1.0 .. 4.0
            static_cast<int>(std::clamp(g_state.state.fractalLacunarity, 1.0f, 4.0f) * 10.0f + 0.5f),
            g_state.labelFractalLacun,
            g_state.sliderFractalLacun,
            g_state.valueFractalLacun);

        // Gain
        makeSliderRow(
            y,
            L"Gain (amplitude falloff)",
            ID_SLIDER_FRACTAL_GAIN,
            10, 90, // 0.1 .. 0.9
            static_cast<int>(std::clamp(g_state.state.fractalGain, 0.1f, 0.9f) * 100.0f + 0.5f),
            g_state.labelFractalGain,
            g_state.sliderFractalGain,
            g_state.valueFractalGain);

        // Warp strength
        makeSliderRow(
            y,
            L"Domain Warp Strength",
            ID_SLIDER_FRACTAL_WARP,
            0, 100, // 0.0 .. 1.0
            static_cast<int>(std::clamp(g_state.state.fractalWarpStrength, 0.0f, 1.0f) * 100.0f + 0.5f),
            g_state.labelFractalWarp,
            g_state.sliderFractalWarp,
            g_state.valueFractalWarp);

        // Noise type
        makeSliderRow(
            y,
            L"Noise Type (0=FBM,1=Ridged,2=Turb,3=Cell)",
            ID_SLIDER_FRACTAL_TYPE,
            0, 3,
            static_cast<int>(std::clamp(g_state.state.fractalNoiseType, 0.0f, 3.0f) + 0.5f),
            g_state.labelFractalType,
            g_state.sliderFractalType,
            g_state.valueFractalType);

        y += sectionSpacing / 2;
        int cameraHeaderY = y;
        g_state.headerCamera = makeHeader(y, L"Camera");

        g_state.buttonResetCamera = CreateWindowExW(
            0, L"BUTTON", L"Reset",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            headerButtonX, cameraHeaderY, headerButtonWidth, headerButtonHeight,
            hwnd, reinterpret_cast<HMENU>(ID_BUTTON_RESET_CAMERA), nullptr, nullptr);
        SendMessageW(g_state.buttonResetCamera, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        makeSliderRow(
            y,
            L"Camera Base Speed",
            ID_SLIDER_CAM_SPEED,
            1, 30,
            CamSpeedToSlider(g_state.state.cameraBaseSpeed),
            g_state.labelCamSpeed,
            g_state.sliderCamSpeed,
            g_state.valueCamSpeed);

        int buttonHeight = 26;
        int buttonWidth = 160;
        int buttonX = clientWidth - marginX - buttonWidth;
        int buttonY = y + 4;

        g_state.buttonResetAll = CreateWindowExW(
            0, L"BUTTON", L"Reset to Defaults",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            buttonX, buttonY, buttonWidth, buttonHeight,
            g_state.content, reinterpret_cast<HMENU>(ID_BUTTON_RESET_ALL), nullptr, nullptr);
        SendMessageW(g_state.buttonResetAll, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

        // Initialize numeric labels with current values
        DebugMenuState currentState = g_state.state;
        ApplyStateToControls(currentState);

        // Ensure the window is tall enough for all controls
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        int currentClientHeight = clientRect.bottom - clientRect.top;
        int desiredClientHeight = buttonY + buttonHeight + marginY;
        g_state.contentHeight = std::max(desiredClientHeight, currentClientHeight);

        // Initialize scroll bar for content larger than client area
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = std::max(g_state.contentHeight - 1, 0);
        si.nPage = currentClientHeight;
        si.nPos = 0;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        g_state.scrollPos = 0;

        MoveWindow(g_state.content, 0, -g_state.scrollPos, clientWidth, g_state.contentHeight, TRUE);

        return 0;
    }
    case WM_SIZE: {
        if (!g_state.content) {
            break;
        }
        int clientWidth = LOWORD(lParam);
        int clientHeight = HIWORD(lParam);

        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        GetScrollInfo(hwnd, SB_VERT, &si);
        si.nPage = clientHeight;
        si.nMax = std::max(g_state.contentHeight - 1, 0);
        if (si.nMax <= (int)si.nPage) {
            si.nPos = 0;
            g_state.scrollPos = 0;
        } else {
            int maxPos = si.nMax - static_cast<int>(si.nPage) + 1;
            if (g_state.scrollPos > maxPos) {
                g_state.scrollPos = maxPos;
            }
            si.nPos = g_state.scrollPos;
        }
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

        MoveWindow(g_state.content, 0, -g_state.scrollPos, clientWidth, g_state.contentHeight, TRUE);
        return 0;
    }
    case WM_VSCROLL: {
        if (!g_state.content) {
            break;
        }

        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;

        switch (LOWORD(wParam)) {
        case SB_LINEUP:       pos -= 20; break;
        case SB_LINEDOWN:     pos += 20; break;
        case SB_PAGEUP:       pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN:     pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:   pos = si.nTrackPos; break;
        default: return 0;
        }

        int maxPos = std::max(si.nMax - static_cast<int>(si.nPage) + 1, 0);
        if (pos < si.nMin) pos = si.nMin;
        if (pos > maxPos) pos = maxPos;

        if (pos != si.nPos) {
            si.fMask = SIF_POS;
            si.nPos = pos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            g_state.scrollPos = pos;

            RECT client{};
            GetClientRect(hwnd, &client);
            int clientWidth = client.right - client.left;
            MoveWindow(g_state.content, 0, -g_state.scrollPos, clientWidth, g_state.contentHeight, TRUE);
        }
        return 0;
    }
    case WM_HSCROLL: {
        HWND src = reinterpret_cast<HWND>(lParam);
        if (!src) break;
        int pos = static_cast<int>(SendMessageW(src, TBM_GETPOS, 0, 0));

        if (src == g_state.sliderExposure) {
            g_state.state.exposure = SliderToExposure(pos);
            if (g_state.valueExposure) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.exposure);
                SetWindowTextW(g_state.valueExposure, buffer);
            }
        } else if (src == g_state.sliderBias) {
            g_state.state.shadowBias = SliderToBias(pos);
            if (g_state.valueBias) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.6f", g_state.state.shadowBias);
                SetWindowTextW(g_state.valueBias, buffer);
            }
        } else if (src == g_state.sliderPCF) {
            g_state.state.shadowPCFRadius = SliderToPCF(pos);
            if (g_state.valuePCF) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.shadowPCFRadius);
                SetWindowTextW(g_state.valuePCF, buffer);
            }
        } else if (src == g_state.sliderLambda) {
            g_state.state.cascadeLambda = SliderToLambda(pos);
            if (g_state.valueLambda) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.cascadeLambda);
                SetWindowTextW(g_state.valueLambda, buffer);
            }
        } else if (src == g_state.sliderCascade0) {
            g_state.state.cascade0ResolutionScale = SliderToCascadeScale(pos);
            if (g_state.valueCascade0) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.cascade0ResolutionScale);
                SetWindowTextW(g_state.valueCascade0, buffer);
            }
        } else if (src == g_state.sliderCamSpeed) {
            g_state.state.cameraBaseSpeed = SliderToCamSpeed(pos);
            if (g_state.valueCamSpeed) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.1f", g_state.state.cameraBaseSpeed);
                SetWindowTextW(g_state.valueCamSpeed, buffer);
            }
        } else if (src == g_state.sliderBloom) {
            g_state.state.bloomIntensity = SliderToBloom(pos);
            if (g_state.valueBloom) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.bloomIntensity);
                SetWindowTextW(g_state.valueBloom, buffer);
            }
        } else if (src == g_state.sliderFractalAmp) {
            g_state.state.fractalAmplitude = SliderToFractalAmplitude(pos);
            if (g_state.valueFractalAmp) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.3f", g_state.state.fractalAmplitude);
                SetWindowTextW(g_state.valueFractalAmp, buffer);
            }
        } else if (src == g_state.sliderFractalFreq) {
            g_state.state.fractalFrequency = SliderToFractalFrequency(pos);
            if (g_state.valueFractalFreq) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalFrequency);
                SetWindowTextW(g_state.valueFractalFreq, buffer);
            }
        } else if (src == g_state.sliderFractalOct) {
            g_state.state.fractalOctaves = static_cast<float>(SliderToFractalOctaves(pos));
            if (g_state.valueFractalOct) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.0f", g_state.state.fractalOctaves);
                SetWindowTextW(g_state.valueFractalOct, buffer);
            }
        } else if (src == g_state.sliderFractalMode) {
            int modePos = (pos <= 0) ? 0 : 1;
            g_state.state.fractalCoordMode = (modePos == 0) ? 0.0f : 1.0f;
            if (g_state.valueFractalMode) {
                const wchar_t* modeText = (modePos == 0) ? L"UV" : L"WorldXZ";
                SetWindowTextW(g_state.valueFractalMode, modeText);
            }
        } else if (src == g_state.sliderFractalScaleX) {
            g_state.state.fractalScaleX = SliderToFractalScale(pos);
            if (g_state.valueFractalScaleX) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalScaleX);
                SetWindowTextW(g_state.valueFractalScaleX, buffer);
            }
        } else if (src == g_state.sliderFractalScaleZ) {
            g_state.state.fractalScaleZ = SliderToFractalScale(pos);
            if (g_state.valueFractalScaleZ) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalScaleZ);
                SetWindowTextW(g_state.valueFractalScaleZ, buffer);
            }
        } else if (src == g_state.sliderFractalLacun) {
            g_state.state.fractalLacunarity = std::clamp(static_cast<float>(pos) / 10.0f, 1.0f, 4.0f);
            if (g_state.valueFractalLacun) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalLacunarity);
                SetWindowTextW(g_state.valueFractalLacun, buffer);
            }
        } else if (src == g_state.sliderFractalGain) {
            g_state.state.fractalGain = std::clamp(static_cast<float>(pos) / 100.0f, 0.1f, 0.9f);
            if (g_state.valueFractalGain) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalGain);
                SetWindowTextW(g_state.valueFractalGain, buffer);
            }
        } else if (src == g_state.sliderFractalWarp) {
            g_state.state.fractalWarpStrength = std::clamp(static_cast<float>(pos) / 100.0f, 0.0f, 1.0f);
            if (g_state.valueFractalWarp) {
                wchar_t buffer[64];
                swprintf(buffer, L"%.2f", g_state.state.fractalWarpStrength);
                SetWindowTextW(g_state.valueFractalWarp, buffer);
            }
        } else if (src == g_state.sliderFractalType) {
            int mode = pos;
            if (mode < 0) mode = 0;
            if (mode > 3) mode = 3;
            g_state.state.fractalNoiseType = static_cast<float>(mode);
            if (g_state.valueFractalType) {
                const wchar_t* label = L"FBM";
                if (mode == 1) label = L"Ridged";
                else if (mode == 2) label = L"Turbulence";
                else if (mode == 3) label = L"Cellular";
                SetWindowTextW(g_state.valueFractalType, label);
            }
        }

        // Push changes to renderer so sliders are immediately reflected in the scene
        ApplyCurrentStateToRenderer();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BUTTON_RESET_ALL) {
            // Reset all sliders back to their initial values for this run
            ApplyStateToControls(g_state.defaultState);
            ApplyCurrentStateToRenderer();

            // Also reset non-slider debug toggles to their default modes
            if (auto* renderer = ServiceLocator::GetRenderer()) {
                renderer->SetShadowsEnabled(true);
                renderer->SetDebugViewMode(0);
                renderer->SetPCSS(false);
                renderer->SetFXAAEnabled(true);
            }
            return 0;
        }
        if (id == ID_BUTTON_RESET_EXPOSURE) {
            DebugMenuState state = g_state.state;
            state.exposure = g_state.defaultState.exposure;
            state.bloomIntensity = g_state.defaultState.bloomIntensity;
            ApplyStateToControls(state);
            ApplyCurrentStateToRenderer();
            return 0;
        }
        if (id == ID_BUTTON_RESET_SHADOWS) {
            DebugMenuState state = g_state.state;
            state.shadowBias = g_state.defaultState.shadowBias;
            state.shadowPCFRadius = g_state.defaultState.shadowPCFRadius;
            state.cascadeLambda = g_state.defaultState.cascadeLambda;
            state.cascade0ResolutionScale = g_state.defaultState.cascade0ResolutionScale;
            ApplyStateToControls(state);
            ApplyCurrentStateToRenderer();
            return 0;
        }
        if (id == ID_BUTTON_RESET_CAMERA) {
            DebugMenuState state = g_state.state;
            state.cameraBaseSpeed = g_state.defaultState.cameraBaseSpeed;
            ApplyStateToControls(state);
            // Camera base speed is applied from DebugMenuState in Engine::Update
            return 0;
        }
        break;
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
    g_state.defaultState = initialState;

    EnsureClass();

    // Start with a roomier client area so controls are never clipped
    RECT rc{0, 0, 360, 420};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_VSCROLL;
    DWORD exStyle = WS_EX_TOOLWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_state.hwnd = CreateWindowExW(
        exStyle,
        L"CortexDebugMenuWindow",
        L"Debug Controls",
        style,
        x, y,
        width,
        height,
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
    ApplyStateToControls(state);
}

DebugMenuState DebugMenu::GetState() {
    return g_state.state;
}

void DebugMenu::ResetToDefaults() {
    if (!g_state.initialized) {
        return;
    }

    ApplyStateToControls(g_state.defaultState);
    ApplyCurrentStateToRenderer();

    if (auto* renderer = ServiceLocator::GetRenderer()) {
        renderer->SetShadowsEnabled(true);
        renderer->SetDebugViewMode(0);
        renderer->SetPCSS(false);
        renderer->SetFXAAEnabled(true);
    }
}

} // namespace Cortex::UI
