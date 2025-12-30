#include "Core/Engine.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <exception>
#include <windows.h>
#include <vector>
#include <dbghelp.h>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <fstream>

using namespace Cortex;

namespace {

// Forward declarations (helpers defined later in this TU).
std::string GetExecutableDirectory();

struct RunLogState {
    std::filesystem::path logFilePath;
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ringSink;
};

RunLogState& GetRunLogState() {
    static RunLogState s{};
    return s;
}

std::filesystem::path GetLogDirectory() {
    std::filesystem::path exeDir = GetExecutableDirectory();
    std::filesystem::path logDir = exeDir / "logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);
    if (ec) {
        // Fall back to CWD if exe-relative logs can't be created.
        logDir = std::filesystem::current_path() / "logs";
        std::filesystem::create_directories(logDir, ec);
    }
    return logDir;
}

void ConfigureLoggingToFile() {
    auto& state = GetRunLogState();
    state.logFilePath = GetLogDirectory() / "cortex_last_run.txt";

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(state.logFilePath.string(), true);
    state.ringSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(4096);

    auto logger = std::make_shared<spdlog::logger>(
        "cortex",
        spdlog::sinks_init_list{consoleSink, fileSink, state.ringSink}
    );
    spdlog::set_default_logger(logger);

    // Default to info-level to avoid per-frame spam; raise if diagnosing issues.
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::flush_on(spdlog::level::info);
}

void DumpCurrentStackToLog(const char* header) {
    if (header && *header) {
        spdlog::info("{}", header);
    }

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, nullptr, TRUE);

    HMODULE exeModule = GetModuleHandle(nullptr);
    const auto base = reinterpret_cast<uintptr_t>(exeModule);

    void* stack[32]{};
    USHORT frames = RtlCaptureStackBackTrace(0, 32, stack, nullptr);
    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr64 = reinterpret_cast<DWORD64>(stack[i]);
        char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, addr64, &displacement, symbol)) {
            spdlog::info("  frame {}: {} + 0x{:X}", i, symbol->Name, static_cast<unsigned int>(displacement));
        } else {
            spdlog::info("  frame {}: {} (offset 0x{:X})", i, stack[i],
                         static_cast<unsigned int>(reinterpret_cast<uintptr_t>(stack[i]) - base));
        }
    }
}

void AppendEndOfRunDump(Engine& engine) {
    spdlog::info("===================================");
    spdlog::info("End-of-run diagnostics dump");
    spdlog::info("Log file: {}", GetRunLogState().logFilePath.string());
    spdlog::info("===================================");

    if (auto* renderer = engine.GetRenderer()) {
        renderer->LogDiagnostics();
    } else {
        spdlog::warn("Renderer diagnostics unavailable (renderer is null)");
    }

    DumpCurrentStackToLog("Stack trace at clean shutdown:");

    // Ensure file sinks flush before process exit.
    if (auto logger = spdlog::default_logger()) {
        logger->flush();
    }
}

// Global SEH handler to log crashes instead of silent termination
LONG WINAPI CortexCrashHandler(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord) {
        auto code = info->ExceptionRecord->ExceptionCode;
        auto addr = info->ExceptionRecord->ExceptionAddress;
        HMODULE exeModule = GetModuleHandle(nullptr);
        auto base = reinterpret_cast<uintptr_t>(exeModule);
        auto fault = reinterpret_cast<uintptr_t>(addr);
        spdlog::critical("Unhandled exception: code=0x{:08X} at address {} (offset 0x{:X})",
            static_cast<unsigned int>(code), addr, static_cast<unsigned int>(fault - base));

        // Capture a short stack trace with symbols if available
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, nullptr, TRUE);

        void* stack[16]{};
        USHORT frames = RtlCaptureStackBackTrace(0, 16, stack, nullptr);
        for (USHORT i = 0; i < frames; ++i) {
            DWORD64 addr64 = reinterpret_cast<DWORD64>(stack[i]);
            char buffer[sizeof(SYMBOL_INFO) + 256] = {};
            PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;

            DWORD64 displacement = 0;
            if (SymFromAddr(process, addr64, &displacement, symbol)) {
                spdlog::critical("  frame {}: {} + 0x{:X}", i, symbol->Name, static_cast<unsigned int>(displacement));
            } else {
                spdlog::critical("  frame {}: {} (offset 0x{:X})", i, stack[i],
                    static_cast<unsigned int>(reinterpret_cast<uintptr_t>(stack[i]) - base));
            }
        }
    } else {
        spdlog::critical("Unhandled exception: unknown");
    }
    if (auto logger = spdlog::default_logger()) {
        logger->flush();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Lightweight helpers to avoid <filesystem> and keep the main translation
// unit small enough for constrained build environments.
std::string GetExecutableDirectory() {
    char exePathA[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exePathA, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        char cwd[MAX_PATH] = {};
        DWORD cwdLen = GetCurrentDirectoryA(MAX_PATH, cwd);
        if (cwdLen > 0 && cwdLen < MAX_PATH) {
            return std::string(cwd, cwdLen);
        }
        return ".";
    }
    std::string path(exePathA, len);
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        path.resize(slash);
    }
    return path;
}

bool FileExists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryHasEngine(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::string pattern = dir;
    char last = pattern.back();
    if (last != '\\' && last != '/') {
        pattern += '\\';
    }
    pattern += "*.engine";

    WIN32_FIND_DATAA data{};
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return false;
    }
    FindClose(hFind);
    return true;
}

struct LauncherState {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HWND comboScene = nullptr;
    HWND comboQuality = nullptr;
    HWND chkRT = nullptr;
    HWND chkLLM = nullptr;
    HWND chkDreamer = nullptr;
    HWND radioRaster = nullptr;
    HWND radioVoxel = nullptr;
    HWND btnLaunch = nullptr;
    HWND btnEditor = nullptr;
    HWND btnCancel = nullptr;
    EngineConfig* config = nullptr;
    bool accepted = false;
};

enum LauncherControlId : int {
    IDC_LAUNCH_SCENE    = 2001,
    IDC_LAUNCH_QUALITY  = 2002,
    IDC_LAUNCH_RT       = 2003,
    IDC_LAUNCH_LLM      = 2004,
    IDC_LAUNCH_DREAMER  = 2005,
    IDC_LAUNCH_RASTER   = 2006,
    IDC_LAUNCH_VOXEL    = 2007,
    IDC_LAUNCH_OK       = 2010,
    IDC_LAUNCH_CANCEL   = 2011,
    IDC_LAUNCH_EDITOR   = 2012,
};

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<LauncherState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* s  = reinterpret_cast<LauncherState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<LauncherState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!state) return -1;

        state->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        RECT rc{};
        GetClientRect(hwnd, &rc);
        int width  = rc.right - rc.left;
        int margin = 12;
        int labelH = 18;
        int ctrlH  = 22;
        int rowGap = 6;

        int xLabel = margin;
        int xCtrl  = margin + 140;
        int ctrlW  = width - xCtrl - margin;
        int y      = margin;

        auto makeLabel = [&](const wchar_t* text, int yy) {
            HWND h = CreateWindowExW(
                0, L"STATIC", text,
                WS_CHILD | WS_VISIBLE,
                xLabel, yy, xCtrl - xLabel - 4, labelH,
                hwnd, nullptr, nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return h;
        };

        auto makeCombo = [&](int id, int yy) {
            HWND h = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                xCtrl, yy, ctrlW, 120,
                hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return h;
        };

        auto makeCheckbox = [&](int id, const wchar_t* text, int yy) {
            HWND h = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                xCtrl, yy, ctrlW, ctrlH,
                hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return h;
        };

        auto makeRadio = [&](int id, const wchar_t* text, int yy) {
            HWND h = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                xCtrl, yy, ctrlW, ctrlH,
                hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return h;
        };

        auto makeButton = [&](int id, const wchar_t* text, int xx, int yy, int w) {
            HWND h = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                xx, yy, w, ctrlH + 4,
                hwnd, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            return h;
        };

        // Scene selection
        makeLabel(L"Scene", y);
        state->comboScene = makeCombo(IDC_LAUNCH_SCENE, y);
        SendMessageW(state->comboScene, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"RT Showcase Gallery"));
        SendMessageW(state->comboScene, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cornell Box"));
        SendMessageW(state->comboScene, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dragon Over Water"));
        SendMessageW(state->comboScene, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"God Rays Atrium"));
        SendMessageW(state->comboScene, CB_SETCURSEL, 0, 0);
        y += labelH + rowGap * 2;

        // Quality mode
        makeLabel(L"Quality mode", y);
        state->comboQuality = makeCombo(IDC_LAUNCH_QUALITY, y);
        SendMessageW(state->comboQuality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default (high)"));
        SendMessageW(state->comboQuality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Conservative (8 GB safe)"));
        SendMessageW(state->comboQuality, CB_SETCURSEL,
                     state->config && state->config->qualityMode == EngineConfig::QualityMode::Conservative ? 1 : 0,
                     0);
        y += labelH + rowGap * 2;

        // Feature toggles
        state->chkRT = makeCheckbox(IDC_LAUNCH_RT, L"Enable ray tracing (DXR)", y);
        y += ctrlH + rowGap;
        state->chkLLM = makeCheckbox(IDC_LAUNCH_LLM, L"Enable Architect LLM", y);
        y += ctrlH + rowGap;
        state->chkDreamer = makeCheckbox(IDC_LAUNCH_DREAMER, L"Enable Dreamer textures", y);
        y += ctrlH + rowGap * 2;

        // Backend selection
        makeLabel(L"Render backend", y);
        state->radioRaster = makeRadio(IDC_LAUNCH_RASTER, L"DX12 rasterization (current)", y);
        y += ctrlH + rowGap;
        state->radioVoxel = makeRadio(IDC_LAUNCH_VOXEL, L"Voxel renderer (experimental)", y);
        y += ctrlH + rowGap * 2;

        // Defaults from config
        if (state->config) {
            SendMessageW(state->chkRT, BM_SETCHECK, state->config->enableRayTracing ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(state->chkLLM, BM_SETCHECK, state->config->enableLLM ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(state->chkDreamer, BM_SETCHECK, state->config->enableDreamer ? BST_CHECKED : BST_UNCHECKED, 0);
            bool voxel = state->config->renderBackend == EngineConfig::RenderBackend::VoxelExperimental;
            SendMessageW(state->radioRaster, BM_SETCHECK, voxel ? BST_UNCHECKED : BST_CHECKED, 0);
            SendMessageW(state->radioVoxel, BM_SETCHECK, voxel ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            SendMessageW(state->chkRT, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(state->chkLLM, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(state->chkDreamer, BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(state->radioRaster, BM_SETCHECK, BST_CHECKED, 0);
        }

        // Buttons: place directly below the render backend radios so the
        // layout reads top-to-bottom (scene, quality, features, backend,
        // then actions) instead of anchoring them to the bottom edge.
        int btnW = 100;
        int btnY = y;
        // Three buttons: Launch Demo | Engine Editor | Exit
        state->btnLaunch = makeButton(IDC_LAUNCH_OK, L"Launch Demo", margin, btnY, btnW);
        state->btnEditor = makeButton(IDC_LAUNCH_EDITOR, L"Engine Editor", margin + btnW + rowGap, btnY, btnW);
        state->btnCancel = makeButton(IDC_LAUNCH_CANCEL, L"Exit", width - margin - 60, btnY, 60);

        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (code == BN_CLICKED) {
            if (id == IDC_LAUNCH_OK && state && state->config) {
                // Scene
                int selScene = static_cast<int>(SendMessageW(state->comboScene, CB_GETCURSEL, 0, 0));
                switch (selScene) {
                default:
                case 0: state->config->initialScenePreset = "rt_showcase"; break;
                case 1: state->config->initialScenePreset = "cornell";     break;
                case 2: state->config->initialScenePreset = "dragon";      break;
                case 3: state->config->initialScenePreset = "god_rays";    break;
                }
                // Quality
                int selQuality = static_cast<int>(SendMessageW(state->comboQuality, CB_GETCURSEL, 0, 0));
                state->config->qualityMode =
                    (selQuality == 1) ? EngineConfig::QualityMode::Conservative
                                      : EngineConfig::QualityMode::Default;
                // Toggles
                auto isChecked = [](HWND h) {
                    return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
                };
                state->config->enableRayTracing = isChecked(state->chkRT);
                state->config->enableLLM        = isChecked(state->chkLLM);
                state->config->enableDreamer    = isChecked(state->chkDreamer);

                // Backend
                bool voxel = (SendMessageW(state->radioVoxel, BM_GETCHECK, 0, 0) == BST_CHECKED);
                state->config->renderBackend =
                    voxel ? EngineConfig::RenderBackend::VoxelExperimental
                          : EngineConfig::RenderBackend::RasterDX12;

                state->accepted = true;
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_LAUNCH_EDITOR && state && state->config) {
                // Engine Editor mode - launch directly into terrain world
                state->config->initialScenePreset = "engine_editor";
                state->config->qualityMode = EngineConfig::QualityMode::Default;
                // Keep other settings as-is (RT, LLM, etc.)
                auto isChecked = [](HWND h) {
                    return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
                };
                state->config->enableRayTracing = isChecked(state->chkRT);
                state->config->enableLLM        = isChecked(state->chkLLM);
                state->config->enableDreamer    = isChecked(state->chkDreamer);
                bool voxel = (SendMessageW(state->radioVoxel, BM_GETCHECK, 0, 0) == BST_CHECKED);
                state->config->renderBackend =
                    voxel ? EngineConfig::RenderBackend::VoxelExperimental
                          : EngineConfig::RenderBackend::RasterDX12;
                state->accepted = true;
                PostQuitMessage(0);
                return 0;
            }
            if (id == IDC_LAUNCH_CANCEL) {
                if (state) state->accepted = false;
                PostQuitMessage(0);
                return 0;
            }
        }
        break;
    }
    case WM_CLOSE:
        if (state) state->accepted = false;
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowLauncher(EngineConfig& config) {
    WNDCLASSW wc{};
    wc.lpfnWndProc   = LauncherWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CortexLauncherWindow";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    static bool registered = false;
    if (!registered) {
        RegisterClassW(&wc);
        registered = true;
    }

    LauncherState state{};
    state.config = &config;

    int width  = 520;
    int height = 340;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    state.hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Cortex Engine Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, width, height,
        nullptr,
        nullptr,
        wc.hInstance,
        &state);

    if (!state.hwnd) {
        return true; // fall back to direct launch
    }

    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(state.hwnd);
    return state.accepted;
}

} // namespace

int main(int argc, char* argv[]) {
    // Set up logging (console + per-run log file).
    ConfigureLoggingToFile();
    spdlog::info("Last-run log: {}", GetRunLogState().logFilePath.string());

    // Install crash handler to capture SEH faults
    SetUnhandledExceptionFilter(CortexCrashHandler);

    spdlog::info("===================================");
    spdlog::info("  Project Cortex: Neural Engine");
    spdlog::info("  Phase 2: The Architect");
    spdlog::info("===================================");

    try {
        // Create engine configuration
        EngineConfig config;
        config.window.title = "Project Cortex - Phase 2: The Architect";
        config.window.width = 1280;
        config.window.height = 720;
        config.window.vsync = true;
        // Enable the DX12 debug layer by default so we get validation + DRED breadcrumbs.
        // Force GPU-based validation OFF (it is CPU-write-only descriptor copy-incompatible and
        // can crash on some drivers). You can opt out of the debug layer entirely via
        // CORTEX_DISABLE_DEBUG_LAYER=1 if your driver/SDK layers are unstable.
        config.device.enableDebugLayer = true;
        config.device.enableGPUValidation = false;
        if (const char* envDisableDebug = std::getenv("CORTEX_DISABLE_DEBUG_LAYER")) {
            std::string v = envDisableDebug;
            if (!v.empty() && v != "0" && v != "false" && v != "FALSE") {
                config.device.enableDebugLayer = false;
                config.device.enableGPUValidation = false;
                spdlog::warn("DX12 debug layer disabled via CORTEX_DISABLE_DEBUG_LAYER");
            }
        }

        // Decide whether to show the launcher UI. Power users can skip it
        // via CLI or by specifying a scene/mode explicitly.
        bool hasSceneFlag = false;
        bool hasModeFlag  = false;
        bool noLauncher   = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--scene" || arg.rfind("--scene=", 0) == 0) {
                hasSceneFlag = true;
            } else if (arg == "--mode" || arg.rfind("--mode=", 0) == 0) {
                hasModeFlag = true;
            } else if (arg == "--no-launcher") {
                noLauncher = true;
            }
        }

        const bool useLauncher = !noLauncher && !hasSceneFlag && !hasModeFlag;
        if (useLauncher) {
            if (!ShowLauncher(config)) {
                spdlog::info("Launcher cancelled; exiting.");
                return 0;
            }
        }

        // Optional: parse simple command-line flags
        //   --scene <dragon|rt_showcase|cornell>
        //   --mode  <default|conservative>
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--scene" && i + 1 < argc) {
                config.initialScenePreset = argv[++i];
            } else if (arg.rfind("--scene=", 0) == 0) {
                config.initialScenePreset = arg.substr(std::string("--scene=").size());
            } else if (arg == "--mode" && i + 1 < argc) {
                std::string mode = argv[++i];
                if (mode == "conservative") {
                    config.qualityMode = EngineConfig::QualityMode::Conservative;
                }
            } else if (arg.rfind("--mode=", 0) == 0) {
                std::string mode = arg.substr(std::string("--mode=").size());
                if (mode == "conservative") {
                    config.qualityMode = EngineConfig::QualityMode::Conservative;
                }
            }
        }

        // Phase 2: Configure The Architect (LLM)
        config.enableLLM = true;

        // Phase 3: Configure The Dreamer (async texture generator)
        config.enableDreamer = true;

        // Lightweight CLI / environment toggles so you can speed up startup:
        //   --no-llm                 : disable Architect entirely
        //   CORTEX_DISABLE_LLM=1     : same as --no-llm
        //   --llm-model=<path.gguf>  : force a specific model file
        //   --no-dreamer             : disable Dreamer texture pipeline
        //   CORTEX_DISABLE_DREAMER=1 : same as --no-dreamer
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            spdlog::info("Command line arg[{}]: '{}'", i, arg);
            if (arg == "--no-llm") {
                config.enableLLM = false;
                spdlog::info("  -> LLM disabled via --no-llm");
            } else if (arg.rfind("--llm-model=", 0) == 0) {
                config.enableLLM = true;
                config.llmConfig.modelPath = arg.substr(std::string("--llm-model=").size());
            } else if (arg == "--no-dreamer") {
                config.enableDreamer = false;
                spdlog::info("  -> Dreamer disabled via --no-dreamer");
            }
        }
        spdlog::info("After parsing args: enableDreamer={}, enableLLM={}", config.enableDreamer, config.enableLLM);

        if (const char* envDisable = std::getenv("CORTEX_DISABLE_LLM")) {
            std::string value = envDisable;
            if (!value.empty() && value != "0" && value != "false" && value != "FALSE") {
                config.enableLLM = false;
            }
        }
        if (const char* envDisableDreamer = std::getenv("CORTEX_DISABLE_DREAMER")) {
            std::string value = envDisableDreamer;
            if (!value.empty() && value != "0" && value != "false" && value != "FALSE") {
                config.enableDreamer = false;
            }
        }
        // Resolve model path relative to the executable location (robust to working directory)
        namespace fs = std::filesystem;

        wchar_t exePathW[MAX_PATH];
        DWORD exeLen = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
        fs::path exeDir;
        if (exeLen > 0 && exeLen < MAX_PATH) {
            exeDir = fs::path(exePathW).parent_path();
        } else {
            exeDir = fs::current_path();
        }
        // Common model locations:
        //   - next to the executable: <exeDir>/models
        //   - project root (two levels up): <exeDir>/../.. /models
        fs::path modelsDirExe = exeDir / "models";
        fs::path modelsDirRoot = exeDir.parent_path().parent_path() / "models";

        // Preferred models in order (largest first)
        const char* kPreferredModels[] = {
            "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            "Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
        };

        if (config.enableLLM && config.llmConfig.modelPath.empty()) {
            fs::path modelPath;
            for (const char* name : kPreferredModels) {
                fs::path candidateExe  = modelsDirExe  / name;
                fs::path candidateRoot = modelsDirRoot / name;
                if (fs::exists(candidateExe)) {
                    modelPath = candidateExe;
                    break;
                }
                if (fs::exists(candidateRoot)) {
                    modelPath = candidateRoot;
                    break;
                }
            }

            if (!modelPath.empty()) {
                spdlog::info("LLM model path resolved to: {}", modelPath.string());
                config.llmConfig.modelPath = modelPath.string();
            } else {
                // No model found on disk; run the LLM service in mock mode so Architect input still works.
                spdlog::warn("No GGUF model found for The Architect; running in MOCK MODE (no real LLM).");
                config.llmConfig.modelPath.clear();
            }
        }
        config.llmConfig.contextSize = 8192;  // Larger context for richer scene summaries
        config.llmConfig.threads = 4;
        config.llmConfig.temperature = 0.1f; // deterministic JSON commands
        config.llmConfig.maxTokens = 128;    // short, avoids runaway loops
        // Request GPU offload for a substantial part of the model while
        // keeping headroom for DX12 resources on 8 GB-class GPUs. The LLM
        // service will clamp this to a safe maximum for the current machine.
        //
        // 32 layers keeps inference clearly on-GPU but typically uses
        // noticeably less VRAM than the previous 64-layer request, which
        // reduces the risk of DXGI device-removed errors when large RT
        // scenes and HDR/RT buffers are active.
        config.llmConfig.gpuLayers = 32;

        // Phase 3: Autoconfigure Dreamer diffusion engines if present either
        // next to the executable or at the project root.
        if (config.enableDreamer) {
            std::string dreamerDirExe  = (modelsDirExe  / "dreamer").string();
            std::string dreamerDirRoot = (modelsDirRoot / "dreamer").string();
            std::string chosenDreamerDir;

            if (DirectoryHasEngine(dreamerDirExe)) {
                chosenDreamerDir = dreamerDirExe;
            } else if (DirectoryHasEngine(dreamerDirRoot)) {
                chosenDreamerDir = dreamerDirRoot;
            }

            if (!chosenDreamerDir.empty()) {
                // SDXL-Turbo export script defaults to 768x768; clamp Dreamer to that.
                config.dreamerConfig.defaultWidth = 768;
                config.dreamerConfig.defaultHeight = 768;
                config.dreamerConfig.maxWidth = 768;
                config.dreamerConfig.maxHeight = 768;
                config.dreamerConfig.useGPU = true;
                config.dreamerConfig.enginePath = chosenDreamerDir;
                spdlog::info("Dreamer diffusion engines detected at '{}'; GPU diffusion enabled (CORTEX_ENABLE_TENSORRT build required for runtime).",
                             config.dreamerConfig.enginePath);
            } else {
                spdlog::info("Dreamer: no TensorRT .engine files found under 'models/dreamer'; using CPU procedural fallback.");
            }
        }

        // Initialize engine
        Engine engine;
        auto initResult = engine.Initialize(config);

        if (initResult.IsErr()) {
            spdlog::critical("Failed to initialize engine: {}", initResult.Error());
            return 1;
        }

        // Run main loop
        engine.Run();

        // Always dump a useful snapshot before tearing down renderer/device.
        AppendEndOfRunDump(engine);

        // Shutdown
        engine.Shutdown();

        spdlog::info("===================================");
        spdlog::info("  Cortex Engine exited cleanly");
        spdlog::info("===================================");

        return 0;
    }
    catch (const std::exception& e) {
        spdlog::critical("Fatal exception: {}", e.what());
        return 1;
    }
    catch (...) {
        spdlog::critical("Unknown fatal exception");
        return 1;
    }
}
