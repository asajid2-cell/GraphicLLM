#include "GraphicsSettingsWindow.h"

#include "Core/Engine.h"
#include "Core/ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/RendererTuningState.h"

#include <commctrl.h>

#include <algorithm>
#include <string>

namespace Cortex::UI {

namespace {

enum ControlIdGraphics : int {
    IDC_GFX_HEALTH = 9001,
    IDC_GFX_MEMORY = 9002,
    IDC_GFX_WARNING = 9003,

    IDC_GFX_RENDER_SCALE = 9010,
    IDC_GFX_EXPOSURE = 9011,
    IDC_GFX_BLOOM = 9012,
    IDC_GFX_SUN = 9013,
    IDC_GFX_GOD_RAYS = 9014,
    IDC_GFX_AREA_LIGHT = 9015,
    IDC_GFX_IBL_DIFFUSE = 9016,
    IDC_GFX_IBL_SPECULAR = 9017,
    IDC_GFX_SSAO_RADIUS = 9018,
    IDC_GFX_SSAO_INTENSITY = 9019,
    IDC_GFX_FOG_DENSITY = 9020,
    IDC_GFX_WATER_WAVE = 9021,
    IDC_GFX_BLOOM_THRESHOLD = 9022,
    IDC_GFX_BLOOM_KNEE = 9023,
    IDC_GFX_VIGNETTE = 9024,
    IDC_GFX_LENS_DIRT = 9025,

    IDC_GFX_TAA = 9100,
    IDC_GFX_FXAA = 9101,
    IDC_GFX_GPU_CULLING = 9102,
    IDC_GFX_RT = 9103,
    IDC_GFX_RT_REFLECTIONS = 9104,
    IDC_GFX_RT_GI = 9105,
    IDC_GFX_SSR = 9106,
    IDC_GFX_SSAO = 9107,
    IDC_GFX_PCSS = 9108,
    IDC_GFX_IBL = 9109,
    IDC_GFX_IBL_LIMIT = 9110,
    IDC_GFX_FOG = 9111,
    IDC_GFX_PARTICLES = 9112,

    IDC_GFX_SAFE_PRESET = 9200,
    IDC_GFX_HERO_BASELINE = 9201,
    IDC_GFX_RESET_FROM_RENDERER = 9202,
    IDC_GFX_ENV_NEXT = 9203,
    IDC_GFX_ENV_ONE = 9204,
    IDC_GFX_ENV_ALL = 9205,
    IDC_GFX_SAVE = 9206,
    IDC_GFX_LOAD = 9207,
};

struct SliderBinding {
    HWND hwnd = nullptr;
    float minValue = 0.0f;
    float maxValue = 1.0f;
};

struct GraphicsSettingsState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;

    Graphics::RendererTuningState tuning;

    HWND txtHealth = nullptr;
    HWND txtMemory = nullptr;
    HWND txtWarning = nullptr;

    SliderBinding renderScale;
    SliderBinding exposure;
    SliderBinding bloom;
    SliderBinding sun;
    SliderBinding godRays;
    SliderBinding areaLight;
    SliderBinding iblDiffuse;
    SliderBinding iblSpecular;
    SliderBinding ssaoRadius;
    SliderBinding ssaoIntensity;
    SliderBinding fogDensity;
    SliderBinding waterWave;
    SliderBinding bloomThreshold;
    SliderBinding bloomKnee;
    SliderBinding vignette;
    SliderBinding lensDirt;

    HWND chkTAA = nullptr;
    HWND chkFXAA = nullptr;
    HWND chkGPUCulling = nullptr;
    HWND chkRT = nullptr;
    HWND chkRTReflections = nullptr;
    HWND chkRTGI = nullptr;
    HWND chkSSR = nullptr;
    HWND chkSSAO = nullptr;
    HWND chkPCSS = nullptr;
    HWND chkIBL = nullptr;
    HWND chkIBLLimit = nullptr;
    HWND chkFog = nullptr;
    HWND chkParticles = nullptr;

    int contentHeight = 0;
    int scrollPos = 0;
};

GraphicsSettingsState g_gfx;
const wchar_t* kGraphicsSettingsClassName = L"CortexGraphicsSettingsWindow";

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

float SliderToFloat(const SliderBinding& binding) {
    if (!binding.hwnd) {
        return binding.minValue;
    }
    const int pos = static_cast<int>(SendMessageW(binding.hwnd, TBM_GETPOS, 0, 0));
    const float t = static_cast<float>(pos) / 100.0f;
    return binding.minValue + t * (binding.maxValue - binding.minValue);
}

void SetSliderFromFloat(const SliderBinding& binding, float value) {
    if (!binding.hwnd) {
        return;
    }
    float t = 0.0f;
    if (binding.maxValue > binding.minValue) {
        t = (value - binding.minValue) / (binding.maxValue - binding.minValue);
    }
    int pos = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 100.0f + 0.5f);
    SendMessageW(binding.hwnd, TBM_SETPOS, TRUE, pos);
}

void SetCheckbox(HWND hwnd, bool enabled) {
    if (hwnd) {
        SendMessageW(hwnd, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool GetCheckbox(HWND hwnd) {
    return hwnd && SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SyncStateFromToggles() {
    g_gfx.tuning.quality.taaEnabled = GetCheckbox(g_gfx.chkTAA);
    g_gfx.tuning.quality.fxaaEnabled = GetCheckbox(g_gfx.chkFXAA);
    g_gfx.tuning.quality.gpuCullingEnabled = GetCheckbox(g_gfx.chkGPUCulling);

    g_gfx.tuning.rayTracing.enabled = GetCheckbox(g_gfx.chkRT);
    g_gfx.tuning.rayTracing.reflectionsEnabled = GetCheckbox(g_gfx.chkRTReflections);
    g_gfx.tuning.rayTracing.giEnabled = GetCheckbox(g_gfx.chkRTGI);

    g_gfx.tuning.screenSpace.ssrEnabled = GetCheckbox(g_gfx.chkSSR);
    g_gfx.tuning.screenSpace.ssaoEnabled = GetCheckbox(g_gfx.chkSSAO);
    g_gfx.tuning.screenSpace.pcssEnabled = GetCheckbox(g_gfx.chkPCSS);

    g_gfx.tuning.environment.iblEnabled = GetCheckbox(g_gfx.chkIBL);
    g_gfx.tuning.environment.iblLimitEnabled = GetCheckbox(g_gfx.chkIBLLimit);

    g_gfx.tuning.atmosphere.fogEnabled = GetCheckbox(g_gfx.chkFog);
    g_gfx.tuning.particles.enabled = GetCheckbox(g_gfx.chkParticles);
}

void SyncStateFromSliders() {
    g_gfx.tuning.quality.renderScale = SliderToFloat(g_gfx.renderScale);
    g_gfx.tuning.lighting.exposure = SliderToFloat(g_gfx.exposure);
    g_gfx.tuning.lighting.bloomIntensity = SliderToFloat(g_gfx.bloom);
    g_gfx.tuning.lighting.sunIntensity = SliderToFloat(g_gfx.sun);
    g_gfx.tuning.lighting.godRayIntensity = SliderToFloat(g_gfx.godRays);
    g_gfx.tuning.lighting.areaLightSizeScale = SliderToFloat(g_gfx.areaLight);
    g_gfx.tuning.environment.diffuseIntensity = SliderToFloat(g_gfx.iblDiffuse);
    g_gfx.tuning.environment.specularIntensity = SliderToFloat(g_gfx.iblSpecular);
    g_gfx.tuning.screenSpace.ssaoRadius = SliderToFloat(g_gfx.ssaoRadius);
    g_gfx.tuning.screenSpace.ssaoIntensity = SliderToFloat(g_gfx.ssaoIntensity);
    g_gfx.tuning.atmosphere.fogDensity = SliderToFloat(g_gfx.fogDensity);
    g_gfx.tuning.water.waveAmplitude = SliderToFloat(g_gfx.waterWave);
    g_gfx.tuning.cinematicPost.bloomThreshold = SliderToFloat(g_gfx.bloomThreshold);
    g_gfx.tuning.cinematicPost.bloomSoftKnee = SliderToFloat(g_gfx.bloomKnee);
    g_gfx.tuning.cinematicPost.vignette = SliderToFloat(g_gfx.vignette);
    g_gfx.tuning.cinematicPost.lensDirt = SliderToFloat(g_gfx.lensDirt);
}

void ApplyTuningState() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || renderer->IsDeviceRemoved()) {
        return;
    }
    SyncStateFromToggles();
    SyncStateFromSliders();
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
}

void RefreshControlsFromRenderer() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || !g_gfx.hwnd) {
        return;
    }

    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);

    SetSliderFromFloat(g_gfx.renderScale, g_gfx.tuning.quality.renderScale);
    SetSliderFromFloat(g_gfx.exposure, g_gfx.tuning.lighting.exposure);
    SetSliderFromFloat(g_gfx.bloom, g_gfx.tuning.lighting.bloomIntensity);
    SetSliderFromFloat(g_gfx.sun, g_gfx.tuning.lighting.sunIntensity);
    SetSliderFromFloat(g_gfx.godRays, g_gfx.tuning.lighting.godRayIntensity);
    SetSliderFromFloat(g_gfx.areaLight, g_gfx.tuning.lighting.areaLightSizeScale);
    SetSliderFromFloat(g_gfx.iblDiffuse, g_gfx.tuning.environment.diffuseIntensity);
    SetSliderFromFloat(g_gfx.iblSpecular, g_gfx.tuning.environment.specularIntensity);
    SetSliderFromFloat(g_gfx.ssaoRadius, g_gfx.tuning.screenSpace.ssaoRadius);
    SetSliderFromFloat(g_gfx.ssaoIntensity, g_gfx.tuning.screenSpace.ssaoIntensity);
    SetSliderFromFloat(g_gfx.fogDensity, g_gfx.tuning.atmosphere.fogDensity);
    SetSliderFromFloat(g_gfx.waterWave, g_gfx.tuning.water.waveAmplitude);
    SetSliderFromFloat(g_gfx.bloomThreshold, g_gfx.tuning.cinematicPost.bloomThreshold);
    SetSliderFromFloat(g_gfx.bloomKnee, g_gfx.tuning.cinematicPost.bloomSoftKnee);
    SetSliderFromFloat(g_gfx.vignette, g_gfx.tuning.cinematicPost.vignette);
    SetSliderFromFloat(g_gfx.lensDirt, g_gfx.tuning.cinematicPost.lensDirt);

    SetCheckbox(g_gfx.chkTAA, g_gfx.tuning.quality.taaEnabled);
    SetCheckbox(g_gfx.chkFXAA, g_gfx.tuning.quality.fxaaEnabled);
    SetCheckbox(g_gfx.chkGPUCulling, g_gfx.tuning.quality.gpuCullingEnabled);
    SetCheckbox(g_gfx.chkRT, g_gfx.tuning.rayTracing.enabled);
    SetCheckbox(g_gfx.chkRTReflections, g_gfx.tuning.rayTracing.reflectionsEnabled);
    SetCheckbox(g_gfx.chkRTGI, g_gfx.tuning.rayTracing.giEnabled);
    SetCheckbox(g_gfx.chkSSR, g_gfx.tuning.screenSpace.ssrEnabled);
    SetCheckbox(g_gfx.chkSSAO, g_gfx.tuning.screenSpace.ssaoEnabled);
    SetCheckbox(g_gfx.chkPCSS, g_gfx.tuning.screenSpace.pcssEnabled);
    SetCheckbox(g_gfx.chkIBL, g_gfx.tuning.environment.iblEnabled);
    SetCheckbox(g_gfx.chkIBLLimit, g_gfx.tuning.environment.iblLimitEnabled);
    SetCheckbox(g_gfx.chkFog, g_gfx.tuning.atmosphere.fogEnabled);
    SetCheckbox(g_gfx.chkParticles, g_gfx.tuning.particles.enabled);
}

void RefreshHealthLabels() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!renderer || !g_gfx.hwnd) {
        return;
    }

    const auto health = renderer->BuildHealthState();
    const float frameSeconds = engine ? engine->GetLastFrameTimeSeconds() : 0.0f;
    const double fps = frameSeconds > 0.0f ? (1.0 / static_cast<double>(frameSeconds)) : 0.0;
    constexpr double kToMB = 1.0 / (1024.0 * 1024.0);

    wchar_t buffer[512];
    if (g_gfx.txtHealth) {
        const std::wstring adapter = ToWide(health.adapterName);
        const std::wstring preset = ToWide(health.qualityPreset);
        const std::wstring env = ToWide(health.activeEnvironment);
        swprintf_s(buffer,
                   L"Health: %.1f FPS | %ls | preset=%ls | env=%ls | RT=%ls/%ls",
                   fps,
                   adapter.empty() ? L"adapter unknown" : adapter.c_str(),
                   preset.empty() ? L"unknown" : preset.c_str(),
                   env.empty() ? L"unknown" : env.c_str(),
                   health.rayTracingRequested ? L"requested" : L"off",
                   health.rayTracingEffective ? L"ready" : L"inactive");
        SetWindowTextW(g_gfx.txtHealth, buffer);
    }
    if (g_gfx.txtMemory) {
        swprintf_s(buffer,
                   L"Budgets: VRAM=%.0f MB | descriptors persistent=%u/%u transient=%u/%u | env resident=%u pending=%u",
                   static_cast<double>(health.estimatedVRAMBytes) * kToMB,
                   health.descriptorPersistentUsed,
                   health.descriptorPersistentBudget,
                   health.descriptorTransientUsed,
                   health.descriptorTransientBudget,
                   health.residentEnvironments,
                   health.pendingEnvironments);
        SetWindowTextW(g_gfx.txtMemory, buffer);
    }
    if (g_gfx.txtWarning) {
        const std::wstring code = ToWide(health.lastWarningCode);
        const std::wstring message = ToWide(health.lastWarningMessage);
        swprintf_s(buffer,
                   L"Warnings: frame=%u assets=%u | %ls%ls%ls",
                   health.frameWarnings,
                   health.assetFallbacks,
                   code.empty() ? L"none" : code.c_str(),
                   (!code.empty() && !message.empty()) ? L": " : L"",
                   message.empty() ? L"" : message.c_str());
        SetWindowTextW(g_gfx.txtWarning, buffer);
    }
}

void RegisterGraphicsSettingsClass() {
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
            g_gfx.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int width = rc.right - rc.left;
            const int margin = 10;
            const int labelHeight = 18;
            const int sliderHeight = 26;
            const int rowGap = 5;
            const int checkHeight = 20;
            const int labelWidth = 160;
            const int sliderWidth = width - labelWidth - margin * 2;
            int y = margin;

            auto setFont = [](HWND h) {
                if (h) {
                    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_gfx.font), TRUE);
                }
            };

            auto makeStaticWithHeight = [&](int id, const wchar_t* text, int yy, int height) {
                HWND h = CreateWindowExW(0,
                                         L"STATIC",
                                         text,
                                         WS_CHILD | WS_VISIBLE,
                                         margin,
                                         yy,
                                         width - margin * 2,
                                         height,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                return h;
            };
            auto makeStatic = [&](int id, const wchar_t* text, int yy) {
                return makeStaticWithHeight(id, text, yy, labelHeight);
            };

            auto makeSection = [&](const wchar_t* text) {
                y += rowGap;
                makeStaticWithHeight(0, text, y, labelHeight);
                y += labelHeight + 2;
            };

            auto makeSlider = [&](int id, const wchar_t* text, SliderBinding& binding, float minValue, float maxValue) {
                makeStatic(0, text, y);
                HWND h = CreateWindowExW(0,
                                         TRACKBAR_CLASSW,
                                         L"",
                                         WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                         margin + labelWidth,
                                         y - 2,
                                         sliderWidth,
                                         sliderHeight,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                binding = SliderBinding{h, minValue, maxValue};
                y += sliderHeight + rowGap;
                return h;
            };

            auto makeCheckbox = [&](int id, const wchar_t* text) {
                HWND h = CreateWindowExW(0,
                                         L"BUTTON",
                                         text,
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         margin,
                                         y,
                                         (width - margin * 2) / 2,
                                         checkHeight,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                y += checkHeight + rowGap;
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int x, int yy, int buttonWidth) {
                HWND h = CreateWindowExW(0,
                                         L"BUTTON",
                                         text,
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         x,
                                         yy,
                                         buttonWidth,
                                         24,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                return h;
            };

            g_gfx.txtHealth = makeStatic(IDC_GFX_HEALTH, L"Health: --", y);
            y += labelHeight + rowGap;
            g_gfx.txtMemory = makeStatic(IDC_GFX_MEMORY, L"Budgets: --", y);
            y += labelHeight + rowGap;
            g_gfx.txtWarning = makeStatic(IDC_GFX_WARNING, L"Warnings: --", y);
            y += labelHeight + rowGap;

            makeSection(L"Quality");
            makeSlider(IDC_GFX_RENDER_SCALE, L"Render Scale", g_gfx.renderScale, 0.5f, 1.0f);
            g_gfx.chkTAA = makeCheckbox(IDC_GFX_TAA, L"TAA");
            g_gfx.chkFXAA = makeCheckbox(IDC_GFX_FXAA, L"FXAA");
            g_gfx.chkGPUCulling = makeCheckbox(IDC_GFX_GPU_CULLING, L"GPU Culling");

            makeSection(L"Ray Tracing");
            g_gfx.chkRT = makeCheckbox(IDC_GFX_RT, L"RT Master");
            g_gfx.chkRTReflections = makeCheckbox(IDC_GFX_RT_REFLECTIONS, L"RT Reflections");
            g_gfx.chkRTGI = makeCheckbox(IDC_GFX_RT_GI, L"RT GI");

            makeSection(L"Lighting");
            makeSlider(IDC_GFX_EXPOSURE, L"Exposure", g_gfx.exposure, 0.05f, 5.0f);
            makeSlider(IDC_GFX_BLOOM, L"Bloom", g_gfx.bloom, 0.0f, 2.0f);
            makeSlider(IDC_GFX_SUN, L"Sun Intensity", g_gfx.sun, 0.0f, 20.0f);
            makeSlider(IDC_GFX_GOD_RAYS, L"God Rays", g_gfx.godRays, 0.0f, 3.0f);
            makeSlider(IDC_GFX_AREA_LIGHT, L"Area Light Size", g_gfx.areaLight, 0.25f, 2.0f);

            makeSection(L"Environment / IBL");
            g_gfx.chkIBL = makeCheckbox(IDC_GFX_IBL, L"IBL Enabled");
            g_gfx.chkIBLLimit = makeCheckbox(IDC_GFX_IBL_LIMIT, L"IBL Residency Limit");
            makeSlider(IDC_GFX_IBL_DIFFUSE, L"IBL Diffuse", g_gfx.iblDiffuse, 0.0f, 3.0f);
            makeSlider(IDC_GFX_IBL_SPECULAR, L"IBL Specular", g_gfx.iblSpecular, 0.0f, 3.0f);
            {
                const int buttonWidth = (width - margin * 2 - 12) / 3;
                makeButton(IDC_GFX_ENV_NEXT, L"Next Env", margin, y, buttonWidth);
                makeButton(IDC_GFX_ENV_ONE, L"Load One", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_ENV_ALL, L"Load All", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                y += 24 + rowGap;
            }

            makeSection(L"Screen Space / Atmosphere");
            g_gfx.chkSSR = makeCheckbox(IDC_GFX_SSR, L"SSR");
            g_gfx.chkSSAO = makeCheckbox(IDC_GFX_SSAO, L"SSAO");
            g_gfx.chkPCSS = makeCheckbox(IDC_GFX_PCSS, L"PCSS Shadows");
            g_gfx.chkFog = makeCheckbox(IDC_GFX_FOG, L"Fog");
            makeSlider(IDC_GFX_SSAO_RADIUS, L"SSAO Radius", g_gfx.ssaoRadius, 0.01f, 5.0f);
            makeSlider(IDC_GFX_SSAO_INTENSITY, L"SSAO Intensity", g_gfx.ssaoIntensity, 0.0f, 5.0f);
            makeSlider(IDC_GFX_FOG_DENSITY, L"Fog Density", g_gfx.fogDensity, 0.0f, 0.1f);

            makeSection(L"Showcase Effects");
            g_gfx.chkParticles = makeCheckbox(IDC_GFX_PARTICLES, L"Particles");
            makeSlider(IDC_GFX_WATER_WAVE, L"Water Wave Amp", g_gfx.waterWave, 0.0f, 2.0f);
            makeSlider(IDC_GFX_BLOOM_THRESHOLD, L"Bloom Threshold", g_gfx.bloomThreshold, 0.1f, 5.0f);
            makeSlider(IDC_GFX_BLOOM_KNEE, L"Bloom Soft Knee", g_gfx.bloomKnee, 0.0f, 1.0f);
            makeSlider(IDC_GFX_VIGNETTE, L"Vignette", g_gfx.vignette, 0.0f, 1.0f);
            makeSlider(IDC_GFX_LENS_DIRT, L"Lens Dirt", g_gfx.lensDirt, 0.0f, 1.0f);

            makeSection(L"Actions");
            {
                const int buttonWidth = (width - margin * 2 - 12) / 3;
                makeButton(IDC_GFX_SAFE_PRESET, L"Safe Preset", margin, y, buttonWidth);
                makeButton(IDC_GFX_HERO_BASELINE, L"Hero Baseline", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_RESET_FROM_RENDERER, L"Refresh", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                y += 24 + rowGap;
            }
            {
                const int buttonWidth = (width - margin * 2 - 6) / 2;
                makeButton(IDC_GFX_SAVE, L"Save Settings", margin, y, buttonWidth);
                makeButton(IDC_GFX_LOAD, L"Load Settings", margin + buttonWidth + 6, y, buttonWidth);
                y += 24 + rowGap;
            }

            g_gfx.contentHeight = y + margin;
            g_gfx.scrollPos = 0;

            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = std::max(g_gfx.contentHeight - 1, 0);
            si.nPage = static_cast<UINT>(std::max(rc.bottom - rc.top, 1L));
            si.nPos = 0;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            SetTimer(hwnd, 1, 500, nullptr);
            RefreshControlsFromRenderer();
            RefreshHealthLabels();
            return 0;
        }
        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = std::max(g_gfx.contentHeight - 1, 0);
            si.nPage = static_cast<UINT>(std::max(rc.bottom - rc.top, 1L));
            si.nPos = g_gfx.scrollPos;
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
            case SB_LINEUP: yPos -= 20; break;
            case SB_LINEDOWN: yPos += 20; break;
            case SB_PAGEUP: yPos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: yPos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: yPos = si.nTrackPos; break;
            default: break;
            }
            yPos = std::clamp(yPos, si.nMin, std::max(si.nMin, static_cast<int>(si.nMax - si.nPage + 1)));
            si.fMask = SIF_POS;
            si.nPos = yPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            const int dy = g_gfx.scrollPos - yPos;
            if (dy != 0) {
                ScrollWindowEx(hwnd, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN);
                g_gfx.scrollPos = yPos;
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                RefreshHealthLabels();
            }
            return 0;
        case WM_HSCROLL: {
            const int scrollCode = LOWORD(wParam);
            if (scrollCode == TB_ENDTRACK || scrollCode == TB_THUMBPOSITION) {
                ApplyTuningState();
                RefreshControlsFromRenderer();
                RefreshHealthLabels();
            }
            return 0;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) != BN_CLICKED) {
                break;
            }

            auto* renderer = Cortex::ServiceLocator::GetRenderer();
            if (!renderer) {
                return 0;
            }

            switch (LOWORD(wParam)) {
            case IDC_GFX_SAFE_PRESET:
                Graphics::ApplySafeQualityPresetControl(*renderer);
                break;
            case IDC_GFX_HERO_BASELINE:
                Graphics::ApplyHeroVisualBaselineControls(*renderer);
                break;
            case IDC_GFX_RESET_FROM_RENDERER:
                break;
            case IDC_GFX_ENV_NEXT:
                Graphics::CycleEnvironmentPresetControl(*renderer);
                break;
            case IDC_GFX_ENV_ONE:
                Graphics::ApplyEnvironmentResidencyLoadControl(*renderer, 1);
                break;
            case IDC_GFX_ENV_ALL:
                Graphics::ApplyEnvironmentResidencyLoadControl(*renderer, 64);
                break;
            case IDC_GFX_SAVE: {
                SyncStateFromToggles();
                SyncStateFromSliders();
                std::string error;
                const auto path = Graphics::GetDefaultRendererTuningStatePath();
                if (Graphics::SaveRendererTuningStateFile(path, g_gfx.tuning, &error)) {
                    SetWindowTextW(g_gfx.txtWarning, L"Settings saved to user/graphics_settings.json");
                } else {
                    const std::wstring status = L"Settings save failed: " + ToWide(error);
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                }
                break;
            }
            case IDC_GFX_LOAD: {
                std::string error;
                const auto loaded = Graphics::LoadRendererTuningStateFile(
                    Graphics::GetDefaultRendererTuningStatePath(),
                    &error);
                if (loaded) {
                    Graphics::ApplyRendererTuningState(*renderer, *loaded);
                    SetWindowTextW(g_gfx.txtWarning, L"Settings loaded from user/graphics_settings.json");
                } else {
                    const std::wstring status = error.empty()
                        ? L"No saved graphics settings found"
                        : (L"Settings load failed: " + ToWide(error));
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                }
                break;
            }
            default:
                ApplyTuningState();
                break;
            }

            RefreshControlsFromRenderer();
            RefreshHealthLabels();
            return 0;
        }
        case WM_CLOSE:
            GraphicsSettingsWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_gfx.hwnd = nullptr;
            g_gfx.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kGraphicsSettingsClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_gfx.initialized || g_gfx.hwnd) {
        return;
    }

    RegisterGraphicsSettingsClass();

    const int width = 620;
    const int height = 700;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    if (g_gfx.parent) {
        RECT pr{};
        GetWindowRect(g_gfx.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_gfx.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                                 kGraphicsSettingsClassName,
                                 L"Cortex Graphics Settings",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
                                 x,
                                 y,
                                 width,
                                 height,
                                 g_gfx.parent,
                                 nullptr,
                                 GetModuleHandleW(nullptr),
                                 nullptr);

    if (g_gfx.hwnd) {
        ShowWindow(g_gfx.hwnd, SW_HIDE);
        UpdateWindow(g_gfx.hwnd);
    }
}

} // namespace

void GraphicsSettingsWindow::Initialize(HWND parent) {
    g_gfx.parent = parent;
    g_gfx.initialized = true;
}

void GraphicsSettingsWindow::Shutdown() {
    if (g_gfx.hwnd) {
        DestroyWindow(g_gfx.hwnd);
        g_gfx.hwnd = nullptr;
    }
    g_gfx = GraphicsSettingsState{};
}

void GraphicsSettingsWindow::SetVisible(bool visible) {
    if (!g_gfx.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_gfx.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromRenderer();
        RefreshHealthLabels();
        ShowWindow(g_gfx.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_gfx.hwnd);
        g_gfx.visible = true;
    } else {
        ShowWindow(g_gfx.hwnd, SW_HIDE);
        g_gfx.visible = false;
    }
}

void GraphicsSettingsWindow::Toggle() {
    if (!g_gfx.initialized) {
        return;
    }
    SetVisible(!g_gfx.visible);
}

bool GraphicsSettingsWindow::IsVisible() {
    return g_gfx.visible;
}

} // namespace Cortex::UI
