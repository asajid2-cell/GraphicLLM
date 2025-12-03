#include "LightingWindow.h"

#include "UI/DebugMenu.h"
#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Graphics/Renderer.h"
#include "LLM/SceneCommands.h"

#include <commctrl.h>
#include <string>
#include <vector>

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

namespace Cortex::UI {

namespace {

enum ControlIdLighting : int {
    IDC_LG_LIGHT_TYPE      = 5001,
    IDC_LG_COLOR_R         = 5002,
    IDC_LG_COLOR_G         = 5003,
    IDC_LG_COLOR_B         = 5004,
    IDC_LG_INTENSITY       = 5005,
    IDC_LG_RANGE           = 5006,
    IDC_LG_INNER_CONE      = 5007,
    IDC_LG_OUTER_CONE      = 5008,
    IDC_LG_AUTOPLACE       = 5009,
    IDC_LG_ANCHOR_MODE     = 5010,
    IDC_LG_FORWARD_DIST    = 5011,
    IDC_LG_SHADOWS         = 5012,
    IDC_LG_NAME_EDIT       = 5013,
    IDC_LG_ADD_LIGHT       = 5014,

    IDC_LG_RIG_COMBO       = 5020,
    IDC_LG_APPLY_RIG       = 5021,

    IDC_LG_SUN_INTENSITY   = 5030,
    IDC_LG_IBL_DIFFUSE     = 5031,
    IDC_LG_IBL_SPECULAR    = 5032,
    IDC_LG_GODRAYS         = 5033,

    IDC_LG_CURRENT_LIGHT   = 5040,
    IDC_LG_REFRESH_LIGHTS  = 5041,
    IDC_LG_SAFE_RIG        = 5042,
};

struct LightingState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HFONT font = nullptr;

    // Per-light creation controls
    HWND comboLightType = nullptr;
    HWND sliderColorR   = nullptr;
    HWND sliderColorG   = nullptr;
    HWND sliderColorB   = nullptr;
    HWND sliderIntensity = nullptr;
    HWND sliderRange     = nullptr;
    HWND sliderInnerCone = nullptr;
    HWND sliderOuterCone = nullptr;
    HWND chkAutoPlace    = nullptr;
    HWND comboAnchor     = nullptr;
    HWND sliderForward   = nullptr;
    HWND chkShadows      = nullptr;
    HWND editName        = nullptr;
    HWND btnAddLight     = nullptr;

    // Lighting rig + global controls
    HWND comboRig        = nullptr;
    HWND btnApplyRig     = nullptr;
    HWND sliderSunIntensity = nullptr;
    HWND sliderIBLDiffuse   = nullptr;
    HWND sliderIBLSpecular  = nullptr;
    HWND sliderGodRays      = nullptr;
    HWND comboCurrentLight  = nullptr;
    HWND btnRefreshLights   = nullptr;
    HWND chkSafeRig         = nullptr;

    std::vector<std::string> lightNames;
    int selectedLightIndex = -1;

    // Scrolling
    int contentHeight = 0;
    int scrollPos = 0;
};

LightingState g_light;

const wchar_t* kLightingWindowClassName = L"CortexLightingWindow";

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

std::string WStringToUtf8(const std::wstring& s) {
    return std::string(s.begin(), s.end());
}

static int GetSelectedLightIndex() {
    if (!g_light.comboCurrentLight) {
        return -1;
    }
    int sel = static_cast<int>(SendMessageW(g_light.comboCurrentLight, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_light.lightNames.size())) {
        return -1;
    }
    return sel;
}

void LoadSelectedLightIntoControls();
void ApplyCurrentLightEdits(bool setColor, bool setIntensity, bool setRange, bool setCone, bool setShadows, bool setType);
void RefreshLightListFromScene();

void RefreshControlsFromState() {
    if (!g_light.hwnd) {
        return;
    }

    // Default light creation sliders: white point light, moderate range.
    SetSliderFromFloat(g_light.sliderColorR, 1.0f, 0.0f, 1.0f);
    SetSliderFromFloat(g_light.sliderColorG, 1.0f, 0.0f, 1.0f);
    SetSliderFromFloat(g_light.sliderColorB, 1.0f, 0.0f, 1.0f);
    SetSliderFromFloat(g_light.sliderIntensity, 10.0f, 0.0f, 20.0f);
    SetSliderFromFloat(g_light.sliderRange,     10.0f, 1.0f, 30.0f);
    SetSliderFromFloat(g_light.sliderInnerCone, 20.0f, 5.0f, 60.0f);
    SetSliderFromFloat(g_light.sliderOuterCone, 30.0f, 10.0f, 80.0f);
    SetSliderFromFloat(g_light.sliderForward,    5.0f, 1.0f, 20.0f);

    SetCheckbox(g_light.chkAutoPlace, true);
    SetCheckbox(g_light.chkShadows,   true);

    if (g_light.comboLightType) {
        SendMessageW(g_light.comboLightType, CB_SETCURSEL, 1, 0); // default: Point
    }
    if (g_light.comboAnchor) {
        // Default to spawning lights at the camera's current position so
        // "auto-place near camera" behaves intuitively as the camera moves.
        SendMessageW(g_light.comboAnchor, CB_SETCURSEL, 1, 0); // default: Camera origin
    }

    // Global lighting state from renderer/debug menu
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (renderer) {
        SetSliderFromFloat(g_light.sliderSunIntensity,
                           renderer->GetSunIntensity(),
                           0.0f, 20.0f);
        SetSliderFromFloat(g_light.sliderIBLDiffuse,
                           renderer->GetIBLDiffuseIntensity(),
                           0.0f, 3.0f);
        SetSliderFromFloat(g_light.sliderIBLSpecular,
                           renderer->GetIBLSpecularIntensity(),
                           0.0f, 3.0f);
        SetSliderFromFloat(g_light.sliderGodRays,
                           renderer->GetGodRayIntensity(),
                           0.0f, 3.0f);
        SetCheckbox(g_light.chkSafeRig, renderer->GetUseSafeLightingRigOnLowVRAM());
    }

    // Lighting rig selection mirrors DebugMenuState.lightingRig
    DebugMenuState dbg = DebugMenu::GetState();
    if (g_light.comboRig) {
        int rigIndex = dbg.lightingRig;
        if (rigIndex < 0) rigIndex = 0;
        if (rigIndex > 4) rigIndex = 4;
        SendMessageW(g_light.comboRig, CB_SETCURSEL, rigIndex, 0);
    }

    // Refresh light list and try to sync selection with current focus.
    RefreshLightListFromScene();
}

void SpawnLightFromUI() {
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        return;
    }

    auto cmd = std::make_shared<LLM::AddLightCommand>();

    // Light type
    int typeIndex = 1; // default Point
    if (g_light.comboLightType) {
        typeIndex = static_cast<int>(SendMessageW(g_light.comboLightType, CB_GETCURSEL, 0, 0));
    }
    if (typeIndex < 0 || typeIndex > 2) {
        typeIndex = 1;
    }
    using LType = LLM::AddLightCommand::LightType;
    switch (typeIndex) {
    case 0: cmd->lightType = LType::Directional; break;
    case 1: cmd->lightType = LType::Point;       break;
    case 2: cmd->lightType = LType::Spot;        break;
    default: cmd->lightType = LType::Point;      break;
    }

    // Color
    float r = SliderToFloat(g_light.sliderColorR, 0.0f, 1.0f);
    float g = SliderToFloat(g_light.sliderColorG, 0.0f, 1.0f);
    float b = SliderToFloat(g_light.sliderColorB, 0.0f, 1.0f);
    if (r <= 0.0f && g <= 0.0f && b <= 0.0f) {
        r = g = b = 1.0f;
    }
    cmd->color = glm::vec3(r, g, b);

    // Intensity / range
    cmd->intensity = SliderToFloat(g_light.sliderIntensity, 0.0f, 20.0f);
    cmd->range     = SliderToFloat(g_light.sliderRange,     1.0f, 30.0f);

    // Spot cones (used only for spot lights, but harmless otherwise)
    cmd->innerConeDegrees = SliderToFloat(g_light.sliderInnerCone, 5.0f, 60.0f);
    cmd->outerConeDegrees = SliderToFloat(g_light.sliderOuterCone,
                                          cmd->innerConeDegrees,
                                          80.0f);

    cmd->castsShadows = GetCheckbox(g_light.chkShadows);

    // Placement / anchoring
    cmd->autoPlace = GetCheckbox(g_light.chkAutoPlace);
    int anchorIndex = 0;
    if (g_light.comboAnchor) {
        anchorIndex = static_cast<int>(SendMessageW(g_light.comboAnchor, CB_GETCURSEL, 0, 0));
    }
    using AMode = LLM::AddLightCommand::AnchorMode;
    switch (anchorIndex) {
    case 1: cmd->anchorMode = AMode::Camera;        break;
    case 2: cmd->anchorMode = AMode::CameraForward; break;
    default: cmd->anchorMode = AMode::None;         break;
    }
    cmd->forwardDistance = SliderToFloat(g_light.sliderForward, 1.0f, 20.0f);

    // Optional name
    wchar_t nameBuf[128]{};
    if (g_light.editName) {
        int len = GetWindowTextW(g_light.editName, nameBuf, static_cast<int>(std::size(nameBuf)));
        if (len > 0) {
            std::wstring wname(nameBuf, nameBuf + len);
            cmd->name = WStringToUtf8(wname);
        }
    }

    engine->EnqueueSceneCommand(std::move(cmd));
}

void ApplyRigFromUI() {
    DebugMenuState dbg = DebugMenu::GetState();
    if (!g_light.comboRig) {
        return;
    }
    int rigIndex = static_cast<int>(SendMessageW(g_light.comboRig, CB_GETCURSEL, 0, 0));
    if (rigIndex < 0) rigIndex = 0;
    if (rigIndex > 4) rigIndex = 4;
    dbg.lightingRig = rigIndex;
    DebugMenu::SyncFromState(dbg);
}

void RefreshLightListFromScene() {
    if (!g_light.comboCurrentLight) {
        return;
    }

    SendMessageW(g_light.comboCurrentLight, CB_RESETCONTENT, 0, 0);
    g_light.lightNames.clear();
    g_light.selectedLightIndex = -1;

    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        SendMessageW(g_light.comboCurrentLight, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"<no engine>"));
        SendMessageW(g_light.comboCurrentLight, CB_SETCURSEL, 0, 0);
        return;
    }

    auto* registry = engine->GetRegistry();
    if (!registry) {
        SendMessageW(g_light.comboCurrentLight, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"<no scene>"));
        SendMessageW(g_light.comboCurrentLight, CB_SETCURSEL, 0, 0);
        return;
    }

    auto view = registry->View<Scene::TagComponent, Scene::LightComponent>();
    std::string focusName = engine->GetFocusTarget();
    int focusIndex = -1;
    int idx = 0;
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        const std::string& nameUtf8 = tag.tag;
        g_light.lightNames.push_back(nameUtf8);

        std::wstring wname;
        wname.assign(nameUtf8.begin(), nameUtf8.end());
        if (wname.empty()) {
            wname = L"<unnamed>";
        }
        SendMessageW(g_light.comboCurrentLight, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wname.c_str()));

        if (!focusName.empty() && nameUtf8 == focusName && focusIndex < 0) {
            focusIndex = idx;
        }
        ++idx;
    }

    if (idx == 0) {
        SendMessageW(g_light.comboCurrentLight, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"<no tagged lights>"));
        SendMessageW(g_light.comboCurrentLight, CB_SETCURSEL, 0, 0);
        g_light.selectedLightIndex = -1;
        return;
    }

    int sel = (focusIndex >= 0) ? focusIndex : 0;
    SendMessageW(g_light.comboCurrentLight, CB_SETCURSEL, sel, 0);
    g_light.selectedLightIndex = sel;

    LoadSelectedLightIntoControls();
}

void LoadSelectedLightIntoControls() {
    int index = GetSelectedLightIndex();
    if (index < 0) {
        return;
    }
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) return;
    auto* registry = engine->GetRegistry();
    if (!registry) return;

    const std::string& targetName = g_light.lightNames[static_cast<size_t>(index)];
    auto view = registry->View<Scene::TagComponent, Scene::LightComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        const auto& tag = view.get<Scene::TagComponent>(entity);
        if (tag.tag != targetName) {
            continue;
        }
        const auto& light = view.get<Scene::LightComponent>(entity);

        // Light type -> combo
        if (g_light.comboLightType) {
            int typeIndex = 1;
            if (light.type == Scene::LightType::Directional) {
                typeIndex = 0;
            } else if (light.type == Scene::LightType::Spot) {
                typeIndex = 2;
            } else {
                typeIndex = 1;
            }
            SendMessageW(g_light.comboLightType, CB_SETCURSEL, typeIndex, 0);
        }

        // Color/intensity/range/cones/shadows.
        SetSliderFromFloat(g_light.sliderColorR, light.color.r, 0.0f, 1.0f);
        SetSliderFromFloat(g_light.sliderColorG, light.color.g, 0.0f, 1.0f);
        SetSliderFromFloat(g_light.sliderColorB, light.color.b, 0.0f, 1.0f);
        SetSliderFromFloat(g_light.sliderIntensity, light.intensity, 0.0f, 20.0f);
        SetSliderFromFloat(g_light.sliderRange, light.range, 1.0f, 30.0f);
        SetSliderFromFloat(g_light.sliderInnerCone, light.innerConeDegrees, 5.0f, 60.0f);
        SetSliderFromFloat(g_light.sliderOuterCone, light.outerConeDegrees, 10.0f, 80.0f);
        SetCheckbox(g_light.chkShadows, light.castsShadows);
        return;
    }
}

void ApplyCurrentLightEdits(bool setColor, bool setIntensity, bool setRange, bool setCone, bool setShadows, bool setType) {
    int index = GetSelectedLightIndex();
    if (index < 0) {
        return;
    }

    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        return;
    }

    auto cmd = std::make_shared<LLM::ModifyLightCommand>();
    cmd->targetName = g_light.lightNames[static_cast<size_t>(index)];

    if (setColor) {
        float r = SliderToFloat(g_light.sliderColorR, 0.0f, 1.0f);
        float g = SliderToFloat(g_light.sliderColorG, 0.0f, 1.0f);
        float b = SliderToFloat(g_light.sliderColorB, 0.0f, 1.0f);
        if (r <= 0.0f && g <= 0.0f && b <= 0.0f) {
            r = g = b = 1.0f;
        }
        cmd->setColor = true;
        cmd->color = glm::vec3(r, g, b);
    }
    if (setIntensity) {
        cmd->setIntensity = true;
        cmd->intensity = SliderToFloat(g_light.sliderIntensity, 0.0f, 20.0f);
    }
    if (setRange) {
        cmd->setRange = true;
        cmd->range = SliderToFloat(g_light.sliderRange, 1.0f, 30.0f);
    }
    if (setCone) {
        cmd->setInnerCone = true;
        cmd->innerConeDegrees = SliderToFloat(g_light.sliderInnerCone, 5.0f, 60.0f);
        cmd->setOuterCone = true;
        cmd->outerConeDegrees = SliderToFloat(g_light.sliderOuterCone, 10.0f, 80.0f);
    }
    if (setShadows) {
        cmd->setCastsShadows = true;
        cmd->castsShadows = GetCheckbox(g_light.chkShadows);
    }
    if (setType) {
        int typeIndex = 1;
        if (g_light.comboLightType) {
            typeIndex = static_cast<int>(SendMessageW(g_light.comboLightType, CB_GETCURSEL, 0, 0));
        }
        using LType = LLM::AddLightCommand::LightType;
        cmd->setType = true;
        switch (typeIndex) {
        case 0: cmd->lightType = LType::Directional; break;
        case 1: cmd->lightType = LType::Point;       break;
        case 2: cmd->lightType = LType::Spot;        break;
        default: cmd->lightType = LType::Point;      break;
        }
    }

    engine->EnqueueSceneCommand(std::move(cmd));
}

void RegisterLightingWindowClass() {
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
            g_light.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;

            const int margin = 8;
            const int labelHeight = 18;
            const int sliderHeight = 24;
            const int checkHeight = 18;
            const int rowGap = 4;
            const int comboHeight = 120;

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
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_light.font), TRUE);
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
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_light.font), TRUE);
                return h;
            };

            auto makeCombo = [&](int id, int yy, int dropHeight) {
                HWND h = CreateWindowExW(
                    0, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                    x + colLabelWidth, yy, colSliderWidth, dropHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_light.font), TRUE);
                return h;
            };

            auto makeEdit = [&](int id, int yy) {
                HWND h = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    x + colLabelWidth, yy, colSliderWidth, 20,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_light.font), TRUE);
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, yy, width - margin * 2, 24,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_light.font), TRUE);
                return h;
            };

            // Light creation section ------------------------------------------------
            makeLabel(L"Current Light", y);
            g_light.comboCurrentLight = makeCombo(IDC_LG_CURRENT_LIGHT, y, comboHeight);
            y += labelHeight + rowGap;
            g_light.btnRefreshLights = makeButton(IDC_LG_REFRESH_LIGHTS, L"Refresh lights from scene", y);
            y += 24 + rowGap * 2;

            makeLabel(L"Light Type", y);
            g_light.comboLightType = makeCombo(IDC_LG_LIGHT_TYPE, y, comboHeight);
            if (g_light.comboLightType) {
                SendMessageW(g_light.comboLightType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Directional"));
                SendMessageW(g_light.comboLightType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Point"));
                SendMessageW(g_light.comboLightType, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Spot"));
            }
            y += labelHeight + rowGap * 2;

            makeLabel(L"Color R", y);
            g_light.sliderColorR = makeSlider(IDC_LG_COLOR_R, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Color G", y);
            g_light.sliderColorG = makeSlider(IDC_LG_COLOR_G, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Color B", y);
            g_light.sliderColorB = makeSlider(IDC_LG_COLOR_B, y);
            y += sliderHeight + rowGap * 2;

            makeLabel(L"Intensity", y);
            g_light.sliderIntensity = makeSlider(IDC_LG_INTENSITY, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Range", y);
            g_light.sliderRange = makeSlider(IDC_LG_RANGE, y);
            y += sliderHeight + rowGap * 2;

            makeLabel(L"Inner Cone (deg)", y);
            g_light.sliderInnerCone = makeSlider(IDC_LG_INNER_CONE, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Outer Cone (deg)", y);
            g_light.sliderOuterCone = makeSlider(IDC_LG_OUTER_CONE, y);
            y += sliderHeight + rowGap * 2;

            g_light.chkAutoPlace = makeCheckbox(IDC_LG_AUTOPLACE, L"Auto-place relative to camera", y);
            y += checkHeight + rowGap;

            makeLabel(L"Anchor", y);
            g_light.comboAnchor = makeCombo(IDC_LG_ANCHOR_MODE, y, comboHeight);
            if (g_light.comboAnchor) {
                SendMessageW(g_light.comboAnchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None (world-space)"));
                SendMessageW(g_light.comboAnchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Camera origin"));
                SendMessageW(g_light.comboAnchor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Camera forward"));
            }
            y += labelHeight + rowGap;

            makeLabel(L"Forward Distance", y);
            g_light.sliderForward = makeSlider(IDC_LG_FORWARD_DIST, y);
            y += sliderHeight + rowGap;

            g_light.chkShadows = makeCheckbox(IDC_LG_SHADOWS, L"Cast Shadows", y);
            y += checkHeight + rowGap;

            makeLabel(L"Name (optional)", y);
            g_light.editName = makeEdit(IDC_LG_NAME_EDIT, y);
            y += 24 + rowGap * 2;

            g_light.btnAddLight = makeButton(IDC_LG_ADD_LIGHT, L"Add Light", y);
            y += 28 + rowGap * 2;

            // Lighting rig + global controls ---------------------------------------
            makeLabel(L"Lighting Rig", y);
            g_light.comboRig = makeCombo(IDC_LG_RIG_COMBO, y, comboHeight);
            if (g_light.comboRig) {
                SendMessageW(g_light.comboRig, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom"));
                SendMessageW(g_light.comboRig, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Studio three-point"));
                SendMessageW(g_light.comboRig, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Top-down warehouse"));
                SendMessageW(g_light.comboRig, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Horror side-light"));
                SendMessageW(g_light.comboRig, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Street lanterns"));
            }
            y += labelHeight + rowGap;

            g_light.btnApplyRig = makeButton(IDC_LG_APPLY_RIG, L"Apply Lighting Rig", y);
            y += 28 + rowGap;

            g_light.chkSafeRig = makeCheckbox(IDC_LG_SAFE_RIG, L"Use safe rig variant on 8 GB adapters", y);
            y += checkHeight + rowGap * 2;

            makeLabel(L"Sun Intensity", y);
            g_light.sliderSunIntensity = makeSlider(IDC_LG_SUN_INTENSITY, y);
            y += sliderHeight + rowGap;

            makeLabel(L"IBL Diffuse Intensity", y);
            g_light.sliderIBLDiffuse = makeSlider(IDC_LG_IBL_DIFFUSE, y);
            y += sliderHeight + rowGap;

            makeLabel(L"IBL Specular Intensity", y);
            g_light.sliderIBLSpecular = makeSlider(IDC_LG_IBL_SPECULAR, y);
            y += sliderHeight + rowGap;

            makeLabel(L"God-Ray Intensity", y);
            g_light.sliderGodRays = makeSlider(IDC_LG_GODRAYS, y);
            y += sliderHeight + rowGap;

            // Record content height for scrolling.
            g_light.contentHeight = y + margin;
            g_light.scrollPos = 0;

            // Initialize scroll bar.
            int clientHeight = rc.bottom - rc.top;
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = (g_light.contentHeight > 0 ? g_light.contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = 0;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

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
            si.nMax = (g_light.contentHeight > 0 ? g_light.contentHeight : clientHeight) - 1;
            if (si.nMax < 0) si.nMax = 0;
            si.nPage = clientHeight;
            si.nPos = g_light.scrollPos;
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

            int dy = g_light.scrollPos - yPos;
            if (dy != 0) {
                ScrollWindowEx(hwnd,
                               0,
                               dy,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               SW_INVALIDATE | SW_SCROLLCHILDREN);
                g_light.scrollPos = yPos;
            }
            return 0;
        }
        case WM_HSCROLL: {
            auto* renderer = Cortex::ServiceLocator::GetRenderer();
            if (!renderer) {
                return 0;
            }

            HWND slider = reinterpret_cast<HWND>(lParam);
            if (!slider) {
                return 0;
            }

            if (slider == g_light.sliderSunIntensity) {
                float val = SliderToFloat(slider, 0.0f, 20.0f);
                renderer->SetSunIntensity(val);
            } else if (slider == g_light.sliderIBLDiffuse ||
                       slider == g_light.sliderIBLSpecular) {
                float diff = SliderToFloat(g_light.sliderIBLDiffuse, 0.0f, 3.0f);
                float spec = SliderToFloat(g_light.sliderIBLSpecular, 0.0f, 3.0f);
                renderer->SetIBLIntensity(diff, spec);
            } else if (slider == g_light.sliderGodRays) {
                float g = SliderToFloat(slider, 0.0f, 3.0f);
                renderer->SetGodRayIntensity(g);
            } else if (slider == g_light.sliderColorR ||
                       slider == g_light.sliderColorG ||
                       slider == g_light.sliderColorB) {
                ApplyCurrentLightEdits(true, false, false, false, false, false);
            } else if (slider == g_light.sliderIntensity) {
                ApplyCurrentLightEdits(false, true, false, false, false, false);
            } else if (slider == g_light.sliderRange) {
                ApplyCurrentLightEdits(false, false, true, false, false, false);
            } else if (slider == g_light.sliderInnerCone ||
                       slider == g_light.sliderOuterCone) {
                ApplyCurrentLightEdits(false, false, false, true, false, false);
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (code == BN_CLICKED) {
                if (id == IDC_LG_ADD_LIGHT) {
                    SpawnLightFromUI();
                    return 0;
                }
                if (id == IDC_LG_APPLY_RIG) {
                    ApplyRigFromUI();
                    return 0;
                }
                if (id == IDC_LG_REFRESH_LIGHTS) {
                    RefreshLightListFromScene();
                    return 0;
                }
                if (id == IDC_LG_SHADOWS) {
                    ApplyCurrentLightEdits(false, false, false, false, true, false);
                    return 0;
                }
                if (id == IDC_LG_SAFE_RIG) {
                    auto* renderer = Cortex::ServiceLocator::GetRenderer();
                    if (renderer) {
                        bool enabled = GetCheckbox(g_light.chkSafeRig);
                        renderer->SetUseSafeLightingRigOnLowVRAM(enabled);
                    }
                    return 0;
                }
            } else if (code == CBN_SELCHANGE) {
                if (id == IDC_LG_CURRENT_LIGHT) {
                    g_light.selectedLightIndex = GetSelectedLightIndex();
                    LoadSelectedLightIntoControls();
                    return 0;
                }
                if (id == IDC_LG_LIGHT_TYPE) {
                    ApplyCurrentLightEdits(false, false, false, false, false, true);
                    return 0;
                }
            }
            break;
        }
        case WM_CLOSE:
            LightingWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_light.hwnd = nullptr;
            g_light.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kLightingWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_light.initialized || g_light.hwnd) {
        return;
    }

    RegisterLightingWindowClass();

    int width = 520;
    int height = 640;

    RECT rc{0, 0, width, height};
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    if (g_light.parent) {
        RECT pr{};
        GetWindowRect(g_light.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_light.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kLightingWindowClassName,
        L"Cortex Lighting Lab",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
        x, y,
        width, height,
        g_light.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_light.hwnd) {
        ShowWindow(g_light.hwnd, SW_HIDE);
        UpdateWindow(g_light.hwnd);
    }
}

} // namespace

void LightingWindow::Initialize(HWND parent) {
    g_light.parent = parent;
    g_light.initialized = true;
}

void LightingWindow::Shutdown() {
    if (g_light.hwnd) {
        DestroyWindow(g_light.hwnd);
        g_light.hwnd = nullptr;
    }
    g_light = LightingState{};
}

void LightingWindow::SetVisible(bool visible) {
    if (!g_light.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_light.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromState();
        ShowWindow(g_light.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_light.hwnd);
        g_light.visible = true;
    } else {
        ShowWindow(g_light.hwnd, SW_HIDE);
        g_light.visible = false;
    }
}

void LightingWindow::Toggle() {
    if (!g_light.initialized) {
        return;
    }
    SetVisible(!g_light.visible);
}

bool LightingWindow::IsVisible() {
    return g_light.visible;
}

} // namespace Cortex::UI
