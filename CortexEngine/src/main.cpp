#include "Core/Engine.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <exception>
#include <filesystem>
#include <windows.h>
#include <vector>
#include <dbghelp.h>

using namespace Cortex;

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

int main(int argc, char* argv[]) {
    // Set up logging
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    // Default to info-level to avoid per-frame spam; raise if diagnosing issues
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
    spdlog::flush_on(spdlog::level::info);

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
        // Disable DX debug layer to avoid runtime breaks/crashes on some systems
        config.device.enableDebugLayer = false;
        config.device.enableGPUValidation = false;  // Too slow for development

        // Phase 2: Configure The Architect (LLM)
        config.enableLLM = true;
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

        // Preferred models in order (largest first)
        const char* kPreferredModels[] = {
            "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            "Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
        };

        fs::path modelPath;
        for (const char* name : kPreferredModels) {
            fs::path candidate = exeDir / "models" / name;
            if (fs::exists(candidate)) {
                modelPath = candidate;
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
        config.llmConfig.contextSize = 8192;  // Larger context for richer scene summaries
        config.llmConfig.threads = 4;
        config.llmConfig.temperature = 0.1f; // deterministic JSON commands
        config.llmConfig.maxTokens = 128;    // short, avoids runaway loops
        config.llmConfig.gpuLayers = 999;    // offload all layers to GPU when available

        // Initialize engine
        Engine engine;
        auto initResult = engine.Initialize(config);

        if (initResult.IsErr()) {
            spdlog::critical("Failed to initialize engine: {}", initResult.Error());
            return 1;
        }

        // Run main loop
        engine.Run();

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
