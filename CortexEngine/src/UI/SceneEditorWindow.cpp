#include "SceneEditorWindow.h"

#include "Core/ServiceLocator.h"
#include "Core/Engine.h"
#include "Graphics/Renderer.h"
#include "LLM/SceneCommands.h"
#include "Utils/GLTFLoader.h"

#include <commctrl.h>
#include <string>
#include <vector>

namespace Cortex::UI {

namespace {

// Control identifiers for the editor window.
enum ControlIdEditor : int {
    IDC_SE_PRIMITIVE_LABEL     = 3001,
    IDC_SE_PRIMITIVE_TYPE      = 3002,
    IDC_SE_MATERIAL_LABEL      = 3003,
    IDC_SE_MATERIAL_PRESET     = 3004,
    IDC_SE_METALLIC_LABEL      = 3005,
    IDC_SE_METALLIC_SLIDER     = 3006,
    IDC_SE_ROUGHNESS_LABEL     = 3007,
    IDC_SE_ROUGHNESS_SLIDER    = 3008,
    IDC_SE_AUTOPLACE           = 3009,
    IDC_SE_NAME_LABEL          = 3010,
    IDC_SE_NAME_EDIT           = 3011,
    IDC_SE_ADD_PRIMITIVE       = 3012,

    IDC_SE_MODELS_LABEL        = 3101,
    IDC_SE_MODEL_LIST          = 3102,
    IDC_SE_ADD_MODEL           = 3103,

    IDC_SE_FOCUSED_LABEL       = 3201,
    IDC_SE_FOCUSED_NAME        = 3202,
    IDC_SE_FOCUSED_MAT_LABEL   = 3203,
    IDC_SE_FOCUSED_MAT_PRESET  = 3204,
    IDC_SE_FOCUSED_MET_LABEL   = 3205,
    IDC_SE_FOCUSED_MET_SLIDER  = 3206,
    IDC_SE_FOCUSED_ROUGH_LABEL = 3207,
    IDC_SE_FOCUSED_ROUGH_SLIDER= 3208,
    IDC_SE_FOCUSED_SCALE_LABEL = 3209,
    IDC_SE_FOCUSED_SCALE_SLIDER= 3210,
    IDC_SE_APPLY_MATERIAL      = 3211,
    IDC_SE_APPLY_SCALE         = 3212,
};

struct SceneEditorState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;

    HWND hwnd = nullptr;
    HFONT font = nullptr;

    HWND comboPrimitive = nullptr;
    HWND comboMaterial = nullptr;
    HWND sliderMetallic = nullptr;
    HWND sliderRoughness = nullptr;
    HWND chkAutoPlace = nullptr;
    HWND editName = nullptr;
    HWND btnAddPrimitive = nullptr;

    HWND listModels = nullptr;
    HWND btnAddModel = nullptr;

    HWND lblFocusedName = nullptr;
    HWND comboFocusedMaterial = nullptr;
    HWND sliderFocusedMetallic = nullptr;
    HWND sliderFocusedRoughness = nullptr;
    HWND sliderFocusedScale = nullptr;
    HWND btnApplyMaterial = nullptr;
    HWND btnApplyScale = nullptr;

    std::vector<std::string> modelNames;
};

SceneEditorState g_ed;

const wchar_t* kSceneEditorClassName = L"CortexSceneEditorWindow";

struct PrimitiveChoice {
    const wchar_t* label;
    LLM::AddEntityCommand::EntityType type;
};

// All primitive shapes supported by AddEntityCommand.
const PrimitiveChoice kPrimitiveChoices[] = {
    { L"Cube",     LLM::AddEntityCommand::EntityType::Cube     },
    { L"Sphere",   LLM::AddEntityCommand::EntityType::Sphere   },
    { L"Plane",    LLM::AddEntityCommand::EntityType::Plane    },
    { L"Quad",     LLM::AddEntityCommand::EntityType::Quad     },
    { L"Cylinder", LLM::AddEntityCommand::EntityType::Cylinder },
    { L"Pyramid",  LLM::AddEntityCommand::EntityType::Pyramid  },
    { L"Cone",     LLM::AddEntityCommand::EntityType::Cone     },
    { L"Torus",    LLM::AddEntityCommand::EntityType::Torus    },
    { L"Disk",     LLM::AddEntityCommand::EntityType::Disk     },
    { L"Capsule",  LLM::AddEntityCommand::EntityType::Capsule  },
    { L"Line",     LLM::AddEntityCommand::EntityType::Line     },
};

// Material presets understood by the renderer via presetName heuristics.
const wchar_t* kMaterialPresetLabels[] = {
    L"<Default>",
    L"chrome",
    L"polished_metal",
    L"brushed_metal",
    L"plastic",
    L"painted_plastic",
    L"matte",
    L"brick",
    L"concrete",
    L"wood_floor",
    L"backdrop",
    L"glass",
    L"glass_panel",
    L"mirror",
    L"water",
    L"emissive_panel",
    L"skin",
    L"skin_ish",
    L"cloth",
    L"velvet",
};

float Slider01ToFloat(HWND slider, float minValue, float maxValue) {
    if (!slider) return minValue;
    int pos = static_cast<int>(SendMessage(slider, TBM_GETPOS, 0, 0));
    float t = static_cast<float>(pos) / 100.0f;
    return minValue + t * (maxValue - minValue);
}

void SetSliderFrom01(HWND slider, float value, float minValue, float maxValue) {
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

std::string WStringToUtf8(const std::wstring& s) {
    return std::string(s.begin(), s.end());
}

void RefreshFocusedFromEngine() {
    if (!g_ed.lblFocusedName) {
        return;
    }

    auto* engine = Cortex::ServiceLocator::GetEngine();
    std::wstring wname = L"<none>";
    if (engine) {
        std::string name = engine->GetFocusTarget();
        if (!name.empty()) {
            wname.assign(name.begin(), name.end());
        }
    }

    SetWindowTextW(g_ed.lblFocusedName, wname.c_str());
}

void SpawnPrimitiveFromUI() {
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        return;
    }

    int sel = static_cast<int>(SendMessage(g_ed.comboPrimitive, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(std::size(kPrimitiveChoices))) {
        sel = 0;
    }

    LLM::AddEntityCommand::EntityType type = kPrimitiveChoices[sel].type;

    auto cmd = std::make_shared<LLM::AddEntityCommand>();
    cmd->entityType = type;
    cmd->autoPlace = true;
    cmd->scale = glm::vec3(1.0f);
    cmd->color = glm::vec4(1.0f); // let presets drive most of the look

    // Material preset from combo (index 0 = default/no preset).
    int matIndex = static_cast<int>(SendMessage(g_ed.comboMaterial, CB_GETCURSEL, 0, 0));
    if (matIndex > 0 && matIndex < static_cast<int>(std::size(kMaterialPresetLabels))) {
        std::wstring wlabel = kMaterialPresetLabels[matIndex];
        cmd->hasPreset = true;
        cmd->presetName = WStringToUtf8(wlabel);
    }

    // Basic material numeric parameters from sliders.
    cmd->metallic = Slider01ToFloat(g_ed.sliderMetallic, 0.0f, 1.0f);
    cmd->roughness = Slider01ToFloat(g_ed.sliderRoughness, 0.0f, 1.0f);
    cmd->ao = 1.0f;

    // Optional name tag.
    wchar_t nameBuf[128]{};
    if (g_ed.editName) {
        int len = GetWindowTextW(g_ed.editName, nameBuf, static_cast<int>(std::size(nameBuf)));
        if (len > 0) {
            std::wstring wname(nameBuf, nameBuf + len);
            cmd->name = WStringToUtf8(wname);
        }
    }

    // Auto-place toggle (default on).
    cmd->autoPlace = GetCheckbox(g_ed.chkAutoPlace);

    engine->EnqueueSceneCommand(std::move(cmd));
}

void SpawnModelFromUI() {
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine || !g_ed.listModels) {
        return;
    }

    int sel = static_cast<int>(SendMessage(g_ed.listModels, LB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= static_cast<int>(g_ed.modelNames.size())) {
        return;
    }

    std::string asset = g_ed.modelNames[static_cast<size_t>(sel)];
    if (asset.empty()) {
        return;
    }

    auto cmd = std::make_shared<LLM::AddEntityCommand>();
    cmd->entityType = LLM::AddEntityCommand::EntityType::Model;
    cmd->asset = asset;
    cmd->autoPlace = true;
    cmd->scale = glm::vec3(1.0f);
    cmd->color = glm::vec4(1.0f);
    cmd->metallic = 0.0f;
    cmd->roughness = 0.4f;
    cmd->ao = 1.0f;

    engine->EnqueueSceneCommand(std::move(cmd));
}

void ApplyMaterialToFocusedFromUI() {
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        return;
    }

    std::string target = engine->GetFocusTarget();
    if (target.empty()) {
        return;
    }

    auto cmd = std::make_shared<LLM::ModifyMaterialCommand>();
    cmd->targetName = target;

    // Material preset for focused entity.
    int matIndex = static_cast<int>(SendMessage(g_ed.comboFocusedMaterial, CB_GETCURSEL, 0, 0));
    if (matIndex > 0 && matIndex < static_cast<int>(std::size(kMaterialPresetLabels))) {
        std::wstring wlabel = kMaterialPresetLabels[matIndex];
        cmd->setPreset = true;
        cmd->presetName = WStringToUtf8(wlabel);
    }

    cmd->setMetallic = true;
    cmd->metallic = Slider01ToFloat(g_ed.sliderFocusedMetallic, 0.0f, 1.0f);
    cmd->setRoughness = true;
    cmd->roughness = Slider01ToFloat(g_ed.sliderFocusedRoughness, 0.0f, 1.0f);
    cmd->setAO = false;

    engine->EnqueueSceneCommand(std::move(cmd));
}

void ApplyScaleToFocusedFromUI() {
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!engine) {
        return;
    }

    std::string target = engine->GetFocusTarget();
    if (target.empty()) {
        return;
    }

    auto cmd = std::make_shared<LLM::ModifyTransformCommand>();
    cmd->targetName = target;
    cmd->setScale = true;
    cmd->isRelative = false;

    // Map slider 0..1 to uniform scale 0.1..3.0
    float s = Slider01ToFloat(g_ed.sliderFocusedScale, 0.1f, 3.0f);
    cmd->scale = glm::vec3(s);

    engine->EnqueueSceneCommand(std::move(cmd));
}

void RefreshModelList() {
    if (!g_ed.listModels) {
        return;
    }
    SendMessageW(g_ed.listModels, LB_RESETCONTENT, 0, 0);
    g_ed.modelNames.clear();

    auto names = Utils::GetSampleModelNames();
    g_ed.modelNames = names;

    for (const auto& n : g_ed.modelNames) {
        std::wstring w;
        w.assign(n.begin(), n.end());
        SendMessageW(g_ed.listModels, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
}

void RefreshControlsFromDefaults() {
    if (!g_ed.hwnd) {
        return;
    }

    SetSliderFrom01(g_ed.sliderMetallic, 0.0f, 0.0f, 1.0f);
    SetSliderFrom01(g_ed.sliderRoughness, 0.5f, 0.0f, 1.0f);
    SetCheckbox(g_ed.chkAutoPlace, true);
}

void RegisterSceneEditorClass() {
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
            g_ed.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;

            const int margin = 8;
            const int labelHeight = 18;
            const int rowGap = 4;
            const int comboHeight = 24;
            const int sliderHeight = 26;

            int x = margin;
            int y = margin;
            int colLabelWidth = 120;
            int colFieldWidth = width - colLabelWidth - margin * 2;

            auto makeLabel = [&](const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"STATIC", text,
                    WS_CHILD | WS_VISIBLE,
                    x, yy, colLabelWidth - 4, labelHeight,
                    hwnd, nullptr, nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
                return h;
            };

            auto makeCombo = [&](int id, int yy) {
                HWND h = CreateWindowExW(
                    0, L"COMBOBOX", L"",
                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                    x + colLabelWidth, yy, colFieldWidth, comboHeight * 6,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
                return h;
            };

            auto makeSlider = [&](int id, int yy) {
                HWND h = CreateWindowExW(
                    0, TRACKBAR_CLASSW, L"",
                    WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                    x + colLabelWidth, yy, colFieldWidth, sliderHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                return h;
            };

            auto makeCheckbox = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                    x, yy, width - margin * 2, labelHeight,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int yy) {
                HWND h = CreateWindowExW(
                    0, L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, yy, width - margin * 2, 24,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
                return h;
            };

            auto makeEdit = [&](int id, int yy) {
                HWND h = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    x + colLabelWidth, yy, colFieldWidth, 20,
                    hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
                return h;
            };

            // Primitive section ------------------------------------------------
            makeLabel(L"Primitive Type", y);
            g_ed.comboPrimitive = makeCombo(IDC_SE_PRIMITIVE_TYPE, y);
            for (const auto& choice : kPrimitiveChoices) {
                SendMessageW(g_ed.comboPrimitive, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
            }
            SendMessageW(g_ed.comboPrimitive, CB_SETCURSEL, 0, 0);
            y += comboHeight + rowGap;

            makeLabel(L"Material Preset", y);
            g_ed.comboMaterial = makeCombo(IDC_SE_MATERIAL_PRESET, y);
            for (const auto* label : kMaterialPresetLabels) {
                SendMessageW(g_ed.comboMaterial, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            }
            SendMessageW(g_ed.comboMaterial, CB_SETCURSEL, 0, 0);
            y += comboHeight + rowGap;

            makeLabel(L"Metallic", y);
            g_ed.sliderMetallic = makeSlider(IDC_SE_METALLIC_SLIDER, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Roughness", y);
            g_ed.sliderRoughness = makeSlider(IDC_SE_ROUGHNESS_SLIDER, y);
            y += sliderHeight + rowGap;

            g_ed.chkAutoPlace = makeCheckbox(IDC_SE_AUTOPLACE, L"Auto-place near camera", y);
            y += labelHeight + rowGap;

            makeLabel(L"Name (optional)", y);
            g_ed.editName = makeEdit(IDC_SE_NAME_EDIT, y);
            y += 24 + rowGap * 2;

            g_ed.btnAddPrimitive = makeButton(IDC_SE_ADD_PRIMITIVE, L"Add Primitive", y);
            y += 28 + rowGap * 2;

            // Sample models section -------------------------------------------
            makeLabel(L"Sample Models (glTF)", y);
            y += labelHeight + rowGap;

            int listHeight = 120;
            g_ed.listModels = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_VSCROLL,
                x, y, width - margin * 2, listHeight,
                hwnd, reinterpret_cast<HMENU>(IDC_SE_MODEL_LIST), nullptr, nullptr);
            SendMessageW(g_ed.listModels, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
            y += listHeight + rowGap;

            g_ed.btnAddModel = makeButton(IDC_SE_ADD_MODEL, L"Add Selected Model", y);
            y += 28 + rowGap * 2;

            // Focused-entity material / transform section --------------------
            makeLabel(L"Focused Entity", y);
            g_ed.lblFocusedName = CreateWindowExW(
                0, L"STATIC", L"<none>",
                WS_CHILD | WS_VISIBLE,
                x + colLabelWidth, y, colFieldWidth, labelHeight,
                hwnd, reinterpret_cast<HMENU>(IDC_SE_FOCUSED_NAME), nullptr, nullptr);
            SendMessageW(g_ed.lblFocusedName, WM_SETFONT, reinterpret_cast<WPARAM>(g_ed.font), TRUE);
            y += labelHeight + rowGap;

            makeLabel(L"Preset", y);
            g_ed.comboFocusedMaterial = makeCombo(IDC_SE_FOCUSED_MAT_PRESET, y);
            for (const auto* label : kMaterialPresetLabels) {
                SendMessageW(g_ed.comboFocusedMaterial, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            }
            SendMessageW(g_ed.comboFocusedMaterial, CB_SETCURSEL, 0, 0);
            y += comboHeight + rowGap;

            makeLabel(L"Metallic", y);
            g_ed.sliderFocusedMetallic = makeSlider(IDC_SE_FOCUSED_MET_SLIDER, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Roughness", y);
            g_ed.sliderFocusedRoughness = makeSlider(IDC_SE_FOCUSED_ROUGH_SLIDER, y);
            y += sliderHeight + rowGap;

            makeLabel(L"Uniform Scale", y);
            g_ed.sliderFocusedScale = makeSlider(IDC_SE_FOCUSED_SCALE_SLIDER, y);
            y += sliderHeight + rowGap * 2;

            g_ed.btnApplyMaterial = makeButton(IDC_SE_APPLY_MATERIAL, L"Apply Material to Focused", y);
            y += 28 + rowGap;

            g_ed.btnApplyScale = makeButton(IDC_SE_APPLY_SCALE, L"Apply Scale to Focused", y);

            RefreshControlsFromDefaults();
            RefreshModelList();
            RefreshFocusedFromEngine();
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                if (id == IDC_SE_ADD_PRIMITIVE) {
                    SpawnPrimitiveFromUI();
                    return 0;
                } else if (id == IDC_SE_ADD_MODEL) {
                    SpawnModelFromUI();
                    return 0;
                } else if (id == IDC_SE_APPLY_MATERIAL) {
                    ApplyMaterialToFocusedFromUI();
                    RefreshFocusedFromEngine();
                    return 0;
                } else if (id == IDC_SE_APPLY_SCALE) {
                    ApplyScaleToFocusedFromUI();
                    RefreshFocusedFromEngine();
                    return 0;
                }
            }
            break;
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            g_ed.visible = false;
            return 0;
        case WM_DESTROY:
            g_ed.hwnd = nullptr;
            break;
        default:
            break;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kSceneEditorClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (g_ed.hwnd || !g_ed.parent) {
        return;
    }

    RegisterSceneEditorClass();

    int width = 420;
    int height = 520;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    g_ed.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kSceneEditorClassName,
        L"Cortex Scene Editor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y,
        width, height,
        g_ed.parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (g_ed.hwnd) {
        ShowWindow(g_ed.hwnd, SW_HIDE);
        UpdateWindow(g_ed.hwnd);
    }
}

} // namespace

void SceneEditorWindow::Initialize(HWND parent) {
    g_ed.parent = parent;
    g_ed.initialized = true;
}

void SceneEditorWindow::Shutdown() {
    if (g_ed.hwnd) {
        DestroyWindow(g_ed.hwnd);
        g_ed.hwnd = nullptr;
    }
    g_ed = SceneEditorState{};
}

void SceneEditorWindow::SetVisible(bool visible) {
    if (!g_ed.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_ed.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromDefaults();
        RefreshModelList();
        RefreshFocusedFromEngine();
        ShowWindow(g_ed.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_ed.hwnd);
        g_ed.visible = true;
    } else {
        ShowWindow(g_ed.hwnd, SW_HIDE);
        g_ed.visible = false;
    }
}

void SceneEditorWindow::Toggle() {
    if (!g_ed.initialized) {
        return;
    }
    SetVisible(!g_ed.visible);
}

bool SceneEditorWindow::IsVisible() {
    return g_ed.visible;
}

} // namespace Cortex::UI
