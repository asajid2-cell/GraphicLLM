#include "PerformanceWindow.h"

#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Graphics/Renderer.h"
#include "Graphics/AssetRegistry.h"

#include <commctrl.h>
#include <string>
#include <vector>

namespace Cortex::UI {

namespace {

enum ControlIdPerf : int {
    IDC_PERF_STATS_FRAME    = 4001,
    IDC_PERF_STATS_MEM      = 4002,
    IDC_PERF_STATS_JOBS     = 4003,
    IDC_PERF_STATS_GOV      = 4004,
    IDC_PERF_STATS_BUDGETS  = 4005,

    IDC_PERF_STATS_FPS      = 4010,

    IDC_PERF_ASSET_TEXT     = 4020,

    IDC_PERF_RENDER_SCALE   = 4030,
    IDC_PERF_BLOOM          = 4031,

    IDC_PERF_RT_MASTER      = 4040,
    IDC_PERF_RT_REFL        = 4041,
    IDC_PERF_RT_GI          = 4042,
    IDC_PERF_TAA            = 4043,
    IDC_PERF_FXAA           = 4044,
    IDC_PERF_SSR            = 4045,
    IDC_PERF_SSAO           = 4046,
    IDC_PERF_IBL            = 4047,
    IDC_PERF_FOG            = 4048,
    IDC_PERF_PARTICLES      = 4049,
    IDC_PERF_IBL_LIMIT      = 4050,

    IDC_PERF_LOAD_ENV_ONE   = 4060,
    IDC_PERF_LOAD_ENV_ALL   = 4061,
    IDC_PERF_SAFE_PRESET    = 4062,
};

struct PerfWindowState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HFONT font = nullptr;

    HWND txtFrame    = nullptr;
    HWND txtMem      = nullptr;
    HWND txtJobs     = nullptr;
    HWND txtGov      = nullptr;
    HWND txtBudgets  = nullptr;
    HWND txtFPS      = nullptr;

    HWND txtAssets   = nullptr;

    HWND sliderRenderScale = nullptr;
    HWND sliderBloom       = nullptr;

    HWND chkRTMaster = nullptr;
    HWND chkRTRefl   = nullptr;
    HWND chkRTGI     = nullptr;
    HWND chkTAA      = nullptr;
    HWND chkFXAA     = nullptr;
    HWND chkSSR      = nullptr;
    HWND chkSSAO     = nullptr;
    HWND chkIBL      = nullptr;
    HWND chkFog      = nullptr;
    HWND chkParticles = nullptr;
    HWND chkIBLLimit = nullptr;

    HWND btnEnvOne   = nullptr;
    HWND btnEnvAll   = nullptr;
    HWND btnSafe     = nullptr;

    int contentHeight = 0;
    int scrollPos = 0;
};

PerfWindowState g_perf;

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

const wchar_t* kPerfWindowClassName = L"CortexPerformanceWindow";

void RefreshControlsFromState() {
    if (!g_perf.hwnd) {
        return;
    }

    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    // Render scale slider (0.5 .. 1.0)
    SetSliderFromFloat(g_perf.sliderRenderScale,
                       renderer->GetRenderScale(),
                       0.5f,
                       1.0f);

    // Bloom intensity (0.0 .. 5.0)
    SetSliderFromFloat(g_perf.sliderBloom,
                       renderer->GetBloomIntensity(),
                       0.0f,
                       5.0f);

    // Feature toggles
    SetCheckbox(g_perf.chkRTMaster,
                renderer->IsRayTracingSupported() && renderer->IsRayTracingEnabled());
    SetCheckbox(g_perf.chkRTRefl,   renderer->GetRTReflectionsEnabled());
    SetCheckbox(g_perf.chkRTGI,     renderer->GetRTGIEnabled());
    SetCheckbox(g_perf.chkTAA,      renderer->IsTAAEnabled());
    SetCheckbox(g_perf.chkFXAA,     renderer->IsFXAAEnabled());
    SetCheckbox(g_perf.chkSSR,      renderer->GetSSREnabled());
    SetCheckbox(g_perf.chkSSAO,     renderer->GetSSAOEnabled());
    SetCheckbox(g_perf.chkIBL,      renderer->GetIBLEnabled());
    SetCheckbox(g_perf.chkFog,      renderer->IsFogEnabled());
    SetCheckbox(g_perf.chkParticles, renderer->GetParticlesEnabled());
    SetCheckbox(g_perf.chkIBLLimit, renderer->IsIBLLimitEnabled());
}

void RefreshStats() {
    auto* engine   = Cortex::ServiceLocator::GetEngine();
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer) {
        return;
    }

    const float frameSeconds = engine ? engine->GetLastFrameTimeSeconds() : 0.0f;
    const double frameMs = static_cast<double>(frameSeconds) * 1000.0;
    const double fps = (frameSeconds > 0.0f) ? (1.0 / static_cast<double>(frameSeconds)) : 0.0;
    const double mainMs = static_cast<double>(renderer->GetLastMainPassTimeMS());
    const double rtMs   = static_cast<double>(renderer->GetLastRTTimeMS());
    const double postMs = static_cast<double>(renderer->GetLastPostTimeMS());

    wchar_t buffer[256];
    if (g_perf.txtFrame) {
        swprintf_s(buffer, L"Frame: %.2f ms   FPS: %.1f", frameMs, fps);
        SetWindowTextW(g_perf.txtFrame, buffer);
    }
    if (g_perf.txtFPS) {
        swprintf_s(buffer, L"Passes: Main=%.2f ms  RT=%.2f ms  Post=%.2f ms", mainMs, rtMs, postMs);
        SetWindowTextW(g_perf.txtFPS, buffer);
    }

    // Memory breakdown
    auto mem = renderer->GetAssetMemoryBreakdown();
    constexpr double kToMB = 1.0 / (1024.0 * 1024.0);
    const double texMB  = static_cast<double>(mem.textureBytes) * kToMB;
    const double envMB  = static_cast<double>(mem.environmentBytes) * kToMB;
    const double geomMB = static_cast<double>(mem.geometryBytes) * kToMB;
    const double rtMB   = static_cast<double>(mem.rtStructureBytes) * kToMB;

    if (g_perf.txtMem) {
        swprintf_s(buffer, L"GPU mem: tex=%.0f MB  env=%.0f MB  geom=%.0f MB  RT=%.0f MB",
                   texMB, envMB, geomMB, rtMB);
        SetWindowTextW(g_perf.txtMem, buffer);
    }

    // Jobs and governors
    uint32_t meshJobs = renderer->GetPendingMeshJobs();
    uint32_t blasJobs = renderer->GetPendingBLASJobs();
    bool rtWarmup = renderer->IsRTWarmingUp();

    if (g_perf.txtJobs) {
        swprintf_s(buffer, L"GPU jobs: meshes=%u  BLAS=%u  RT warmup=%s",
                   meshJobs,
                   blasJobs,
                   rtWarmup ? L"YES" : L"NO");
        SetWindowTextW(g_perf.txtJobs, buffer);
    }

    const bool vramGov = engine ? engine->DidVRAMGovernorReduce() : false;
    const bool perfGov = engine ? engine->DidPerfGovernorAdjust() : false;

    if (g_perf.txtGov) {
        swprintf_s(buffer, L"Governors: VRAM=%s  PERF=%s  scale=%.2f",
                   vramGov ? L"ON" : L"OFF",
                   perfGov ? L"ON" : L"OFF",
                   renderer->GetRenderScale());
        SetWindowTextW(g_perf.txtGov, buffer);
    }

    const auto& registry = renderer->GetAssetRegistry();
    const bool texOver  = registry.IsTextureBudgetExceeded();
    const bool envOver  = registry.IsEnvironmentBudgetExceeded();
    const bool geomOver = registry.IsGeometryBudgetExceeded();
    const bool rtOver   = registry.IsRTBudgetExceeded();

    if (g_perf.txtBudgets) {
        swprintf_s(buffer, L"Budgets: tex=%s env=%s geom=%s rt=%s",
                   texOver  ? L"OVER" : L"OK",
                   envOver  ? L"OVER" : L"OK",
                   geomOver ? L"OVER" : L"OK",
                   rtOver   ? L"OVER" : L"OK");
        SetWindowTextW(g_perf.txtBudgets, buffer);
    }

    // Top assets
    if (g_perf.txtAssets) {
        std::wstring text;
        text.reserve(1024);

        text += L"Top textures by estimated GPU size:\r\n";
        auto topTex = registry.GetHeaviestTextures(5);
        if (topTex.empty()) {
            text += L"  (none)\r\n";
        } else {
            for (const auto& t : topTex) {
                const double mb = static_cast<double>(t.bytes) * kToMB;
                std::wstring name(t.key.begin(), t.key.end());
                if (name.size() > 48) {
                    name = L"..." + name.substr(name.size() - 48);
                }
                wchar_t line[256];
                swprintf_s(line, L"  %.1f MB  %s\r\n", mb, name.c_str());
                text += line;
            }
        }

        text += L"\r\nTop meshes by estimated GPU size:\r\n";
        auto topMeshes = registry.GetHeaviestMeshes(5);
        if (topMeshes.empty()) {
            text += L"  (none)\r\n";
        } else {
            for (const auto& m : topMeshes) {
                const double mb = static_cast<double>(m.bytes) * kToMB;
                std::wstring name(m.key.begin(), m.key.end());
                if (name.size() > 48) {
                    name = L"..." + name.substr(name.size() - 48);
                }
                wchar_t line[256];
                swprintf_s(line, L"  %.1f MB  %s\r\n", mb, name.c_str());
                text += line;
            }
        }

        SetWindowTextW(g_perf.txtAssets, text.c_str());
    }
}

void RegisterPerfWindowClass() {
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
            g_perf.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

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

            auto makeLabel = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, width - margin * 2, labelHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_perf.font), TRUE);
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
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_perf.font), TRUE);
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, yy, width - margin * 2, 24,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_perf.font), TRUE);
                return h;
            };

            auto makeMultiline = [&](int id, int xx, int yy, int w, int h) {
                HWND e = CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    L"",
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                    xx, yy, w, h,
                    hwnd,
                    reinterpret_cast<HMENU>(id),
                    nullptr,
                    nullptr);
                SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(g_perf.font), TRUE);
                return e;
            };

            // Stats block
            g_perf.txtFrame   = makeLabel(IDC_PERF_STATS_FRAME,   L"Frame: --", y);
            y += labelHeight + rowGap;
            g_perf.txtFPS     = makeLabel(IDC_PERF_STATS_FPS,     L"Passes: --", y);
            y += labelHeight + rowGap;
            g_perf.txtMem     = makeLabel(IDC_PERF_STATS_MEM,     L"GPU mem: --", y);
            y += labelHeight + rowGap;
            g_perf.txtJobs    = makeLabel(IDC_PERF_STATS_JOBS,    L"GPU jobs: --", y);
            y += labelHeight + rowGap;
            g_perf.txtGov     = makeLabel(IDC_PERF_STATS_GOV,     L"Governors: --", y);
            y += labelHeight + rowGap;
            g_perf.txtBudgets = makeLabel(IDC_PERF_STATS_BUDGETS, L"Budgets: --", y);
            y += labelHeight + rowGap * 2;

            // Render scale + bloom
            makeLabel(0, L"Render Scale", y);
            g_perf.sliderRenderScale = makeSlider(IDC_PERF_RENDER_SCALE, y);
            y += sliderHeight + rowGap;

            makeLabel(0, L"Bloom Intensity", y);
            g_perf.sliderBloom = makeSlider(IDC_PERF_BLOOM, y);
            y += sliderHeight + rowGap * 2;

            g_perf.chkRTMaster   = makeCheckbox(IDC_PERF_RT_MASTER,   L"RTX (global)", y);
            y += checkHeight + rowGap;
            g_perf.chkRTRefl     = makeCheckbox(IDC_PERF_RT_REFL,     L"RT Reflections", y);
            y += checkHeight + rowGap;
            g_perf.chkRTGI       = makeCheckbox(IDC_PERF_RT_GI,       L"RT GI / Ambient", y);
            y += checkHeight + rowGap;
            g_perf.chkTAA        = makeCheckbox(IDC_PERF_TAA,         L"TAA (temporal AA)", y);
            y += checkHeight + rowGap;
            g_perf.chkFXAA       = makeCheckbox(IDC_PERF_FXAA,        L"FXAA", y);
            y += checkHeight + rowGap;
            g_perf.chkSSR        = makeCheckbox(IDC_PERF_SSR,         L"SSR (screen-space reflections)", y);
            y += checkHeight + rowGap;
            g_perf.chkSSAO       = makeCheckbox(IDC_PERF_SSAO,        L"SSAO (ambient occlusion)", y);
            y += checkHeight + rowGap;
            g_perf.chkIBL        = makeCheckbox(IDC_PERF_IBL,         L"IBL (environment lighting)", y);
            y += checkHeight + rowGap;
            g_perf.chkFog        = makeCheckbox(IDC_PERF_FOG,         L"Fog / Atmosphere", y);
            y += checkHeight + rowGap;
            g_perf.chkParticles  = makeCheckbox(IDC_PERF_PARTICLES,   L"Particles (billboard emitters)", y);
            y += checkHeight + rowGap;
            g_perf.chkIBLLimit   = makeCheckbox(IDC_PERF_IBL_LIMIT,   L"IBL limit (max 4 envs resident; FIFO eviction)", y);
            y += checkHeight + rowGap * 2;

            // Buttons
            g_perf.btnEnvOne = makeButton(IDC_PERF_LOAD_ENV_ONE, L"Load next pending environment", y);
            y += 24 + rowGap;
            g_perf.btnEnvAll = makeButton(IDC_PERF_LOAD_ENV_ALL, L"Load all pending environments", y);
            y += 24 + rowGap;
            g_perf.btnSafe   = makeButton(IDC_PERF_SAFE_PRESET,  L"Apply safe low preset", y);

            // Asset usage pane on the right half
            int assetX = margin;
            int assetWidth = width - margin * 2;
            int assetY = y + 24 + rowGap * 2;
            int assetHeight = rc.bottom - assetY - margin;
            if (assetHeight < 120) {
                assetHeight = 120;
            }

            g_perf.txtAssets = makeMultiline(IDC_PERF_ASSET_TEXT,
                                             assetX,
                                             assetY,
                                             assetWidth,
                                             assetHeight);

            // Record content height for scrolling and initialize scroll bar.
            g_perf.contentHeight = assetY + assetHeight + margin;
            g_perf.scrollPos = 0;

            int clientHeight = rc.bottom - rc.top;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = (g_perf.contentHeight > 0 ? g_perf.contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = 0;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            SetTimer(hwnd, 1, 500, nullptr);

            RefreshControlsFromState();
            RefreshStats();
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
            si.nMax = (g_perf.contentHeight > 0 ? g_perf.contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = g_perf.scrollPos;
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

            int dy = g_perf.scrollPos - yPos;
            if (dy != 0) {
                ScrollWindowEx(hwnd,
                               0,
                               dy,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               SW_INVALIDATE | SW_SCROLLCHILDREN);
                g_perf.scrollPos = yPos;
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                RefreshStats();
            }
            return 0;
        case WM_HSCROLL: {
            auto* renderer = Cortex::ServiceLocator::GetRenderer();
            if (!renderer || renderer->IsDeviceRemoved()) {
                return 0;
            }

            const int scrollCode = LOWORD(wParam);
            HWND slider = reinterpret_cast<HWND>(lParam);
            if (slider == g_perf.sliderRenderScale) {
                if (scrollCode == TB_ENDTRACK || scrollCode == TB_THUMBPOSITION) {
                    float scale = SliderToFloat(slider, 0.5f, 1.0f);
                    if (scale < 0.5f) scale = 0.5f;
                    if (scale > 1.0f) scale = 1.0f;

                    float current = renderer->GetRenderScale();
                    if (std::fabs(scale - current) > 0.01f) {
                        renderer->SetRenderScale(scale);
                    }
                }
            } else if (slider == g_perf.sliderBloom) {
                if (scrollCode == TB_ENDTRACK || scrollCode == TB_THUMBPOSITION) {
                    float value = SliderToFloat(slider, 0.0f, 5.0f);
                    if (value < 0.0f) value = 0.0f;
                    if (value > 5.0f) value = 5.0f;

                    float current = renderer->GetBloomIntensity();
                    if (std::fabs(value - current) > 0.01f) {
                        renderer->SetBloomIntensity(value);
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
                case IDC_PERF_SAFE_PRESET: {
                    renderer->ApplySafeQualityPreset();
                    RefreshControlsFromState();
                    RefreshStats();
                    break;
                }
                case IDC_PERF_LOAD_ENV_ONE: {
                    renderer->LoadAdditionalEnvironmentMaps(1);
                    RefreshStats();
                    break;
                }
                case IDC_PERF_LOAD_ENV_ALL: {
                    // Use a generous upper bound; the renderer will clamp
                    // internally to the number of pending environments.
                    renderer->LoadAdditionalEnvironmentMaps(64);
                    RefreshStats();
                    break;
                }
                case IDC_PERF_RT_MASTER: {
                    bool enabled = GetCheckbox(g_perf.chkRTMaster);
                    if (renderer->IsRayTracingSupported()) {
                        renderer->SetRayTracingEnabled(enabled);
                    }
                    break;
                }
                case IDC_PERF_RT_REFL: {
                    bool enabled = GetCheckbox(g_perf.chkRTRefl);
                    renderer->SetRTReflectionsEnabled(enabled);
                    break;
                }
                case IDC_PERF_RT_GI: {
                    bool enabled = GetCheckbox(g_perf.chkRTGI);
                    renderer->SetRTGIEnabled(enabled);
                    break;
                }
                case IDC_PERF_TAA: {
                    bool enabled = GetCheckbox(g_perf.chkTAA);
                    renderer->SetTAAEnabled(enabled);
                    break;
                }
                case IDC_PERF_FXAA: {
                    bool enabled = GetCheckbox(g_perf.chkFXAA);
                    renderer->SetFXAAEnabled(enabled);
                    break;
                }
                case IDC_PERF_SSR: {
                    bool enabled = GetCheckbox(g_perf.chkSSR);
                    renderer->SetSSREnabled(enabled);
                    break;
                }
                case IDC_PERF_SSAO: {
                    bool enabled = GetCheckbox(g_perf.chkSSAO);
                    renderer->SetSSAOEnabled(enabled);
                    break;
                }
                case IDC_PERF_IBL: {
                    bool enabled = GetCheckbox(g_perf.chkIBL);
                    renderer->SetIBLEnabled(enabled);
                    break;
                }
                case IDC_PERF_FOG: {
                    bool enabled = GetCheckbox(g_perf.chkFog);
                    renderer->SetFogEnabled(enabled);
                    break;
                }
                case IDC_PERF_PARTICLES: {
                    bool enabled = GetCheckbox(g_perf.chkParticles);
                    renderer->SetParticlesEnabled(enabled);
                    break;
                }
                case IDC_PERF_IBL_LIMIT: {
                    bool enabled = GetCheckbox(g_perf.chkIBLLimit);
                    renderer->SetIBLLimitEnabled(enabled);
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
            PerformanceWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_perf.hwnd = nullptr;
            g_perf.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPerfWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_perf.initialized || g_perf.hwnd) {
        return;
    }

    RegisterPerfWindowClass();

    int width = 520;
    int height = 560;

    RECT rc{0, 0, width, height};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_perf.parent) {
        RECT pr{};
        GetWindowRect(g_perf.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_perf.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kPerfWindowClassName,
        L"Cortex Performance & Memory Diagnostics",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
        x, y,
        width, height,
        g_perf.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_perf.hwnd) {
        ShowWindow(g_perf.hwnd, SW_HIDE);
        UpdateWindow(g_perf.hwnd);
    }
}

} // namespace

void PerformanceWindow::Initialize(HWND parent) {
    g_perf.parent = parent;
    g_perf.initialized = true;
}

void PerformanceWindow::Shutdown() {
    if (g_perf.hwnd) {
        DestroyWindow(g_perf.hwnd);
        g_perf.hwnd = nullptr;
    }
    g_perf = PerfWindowState{};
}

void PerformanceWindow::SetVisible(bool visible) {
    if (!g_perf.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_perf.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromState();
        RefreshStats();
        ShowWindow(g_perf.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_perf.hwnd);
        g_perf.visible = true;
    } else {
        ShowWindow(g_perf.hwnd, SW_HIDE);
        g_perf.visible = false;
    }
}

void PerformanceWindow::Toggle() {
    if (!g_perf.initialized) {
        return;
    }
    SetVisible(!g_perf.visible);
}

bool PerformanceWindow::IsVisible() {
    return g_perf.visible;
}

} // namespace Cortex::UI
