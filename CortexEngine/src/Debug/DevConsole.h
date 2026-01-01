// DevConsole.h
// Developer console with command execution, variable inspection, and logging.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <deque>
#include <variant>
#include <any>
#include <mutex>
#include <chrono>

namespace Cortex::Debug {

// ============================================================================
// Console Log Entry
// ============================================================================

enum class LogLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
    Command,    // User input
    Response    // Command response
};

struct LogEntry {
    std::string message;
    std::string category;
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
    std::string file;
    int line;

    // Get formatted timestamp
    std::string GetTimestamp() const;

    // Get level string
    const char* GetLevelString() const;

    // Get level color (RGBA)
    uint32_t GetLevelColor() const;
};

// ============================================================================
// Console Variable
// ============================================================================

using CVarValue = std::variant<bool, int, float, std::string>;

enum class CVarFlags : uint32_t {
    None = 0,
    ReadOnly = 1 << 0,      // Cannot be changed at runtime
    Cheat = 1 << 1,         // Requires cheats enabled
    Archive = 1 << 2,       // Saved to config file
    ServerOnly = 1 << 3,    // Only server can change
    RequireRestart = 1 << 4 // Needs restart to take effect
};

inline CVarFlags operator|(CVarFlags a, CVarFlags b) {
    return static_cast<CVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(CVarFlags a, CVarFlags b) {
    return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

class CVar {
public:
    CVar(const std::string& name, const CVarValue& defaultValue,
          const std::string& description = "", CVarFlags flags = CVarFlags::None);

    // Get/Set value
    template<typename T>
    T Get() const {
        return std::get<T>(m_value);
    }

    template<typename T>
    void Set(const T& value) {
        if (m_flags & CVarFlags::ReadOnly) return;
        m_value = value;
        if (m_onChange) m_onChange(*this);
    }

    void SetFromString(const std::string& str);
    std::string GetAsString() const;

    // Info
    const std::string& GetName() const { return m_name; }
    const std::string& GetDescription() const { return m_description; }
    CVarFlags GetFlags() const { return m_flags; }

    // Type info
    bool IsBool() const { return std::holds_alternative<bool>(m_value); }
    bool IsInt() const { return std::holds_alternative<int>(m_value); }
    bool IsFloat() const { return std::holds_alternative<float>(m_value); }
    bool IsString() const { return std::holds_alternative<std::string>(m_value); }

    // Reset to default
    void Reset() { m_value = m_defaultValue; }

    // Change callback
    using ChangeCallback = std::function<void(const CVar&)>;
    void SetOnChange(ChangeCallback callback) { m_onChange = callback; }

private:
    std::string m_name;
    std::string m_description;
    CVarValue m_value;
    CVarValue m_defaultValue;
    CVarFlags m_flags;
    ChangeCallback m_onChange;
};

// ============================================================================
// Console Command
// ============================================================================

class ConsoleCommand {
public:
    using CommandFunc = std::function<std::string(const std::vector<std::string>&)>;

    ConsoleCommand(const std::string& name, CommandFunc func,
                    const std::string& description = "",
                    const std::string& usage = "");

    // Execute
    std::string Execute(const std::vector<std::string>& args);

    // Info
    const std::string& GetName() const { return m_name; }
    const std::string& GetDescription() const { return m_description; }
    const std::string& GetUsage() const { return m_usage; }

    // Autocomplete suggestions
    using AutocompleteFunc = std::function<std::vector<std::string>(const std::string&)>;
    void SetAutocomplete(AutocompleteFunc func) { m_autocomplete = func; }
    std::vector<std::string> GetAutocomplete(const std::string& partial) const;

private:
    std::string m_name;
    std::string m_description;
    std::string m_usage;
    CommandFunc m_func;
    AutocompleteFunc m_autocomplete;
};

// ============================================================================
// Developer Console
// ============================================================================

class DevConsole {
public:
    // Singleton access
    static DevConsole& Get();

    DevConsole();
    ~DevConsole();

    // Visibility
    void Show() { m_visible = true; }
    void Hide() { m_visible = false; }
    void Toggle() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }

    // Logging
    void Log(const std::string& message, LogLevel level = LogLevel::Info,
              const std::string& category = "");
    void Log(const std::string& message, const std::string& category) {
        Log(message, LogLevel::Info, category);
    }

    void Trace(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Trace, category);
    }
    void Debug(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Debug, category);
    }
    void Info(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Info, category);
    }
    void Warning(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Warning, category);
    }
    void Error(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Error, category);
    }
    void Fatal(const std::string& message, const std::string& category = "") {
        Log(message, LogLevel::Fatal, category);
    }

    // Printf-style logging
    template<typename... Args>
    void LogFormat(LogLevel level, const char* format, Args... args) {
        char buffer[4096];
        snprintf(buffer, sizeof(buffer), format, args...);
        Log(buffer, level);
    }

    // Command execution
    std::string Execute(const std::string& commandLine);

    // Command registration
    void RegisterCommand(const std::string& name, ConsoleCommand::CommandFunc func,
                          const std::string& description = "",
                          const std::string& usage = "");
    void UnregisterCommand(const std::string& name);
    ConsoleCommand* GetCommand(const std::string& name);
    std::vector<std::string> GetCommandNames() const;

    // CVar registration
    CVar* RegisterCVar(const std::string& name, const CVarValue& defaultValue,
                         const std::string& description = "",
                         CVarFlags flags = CVarFlags::None);
    void UnregisterCVar(const std::string& name);
    CVar* GetCVar(const std::string& name);
    std::vector<std::string> GetCVarNames() const;

    // Autocomplete
    std::vector<std::string> GetAutocompleteSuggestions(const std::string& input);

    // History
    const std::deque<LogEntry>& GetLog() const { return m_log; }
    const std::deque<std::string>& GetCommandHistory() const { return m_commandHistory; }
    void ClearLog();
    void ClearHistory();

    // Filtering
    void SetLogLevelFilter(LogLevel minLevel) { m_minLogLevel = minLevel; }
    LogLevel GetLogLevelFilter() const { return m_minLogLevel; }
    void SetCategoryFilter(const std::string& category) { m_categoryFilter = category; }
    const std::string& GetCategoryFilter() const { return m_categoryFilter; }

    // Configuration
    void SetMaxLogEntries(size_t count) { m_maxLogEntries = count; }
    size_t GetMaxLogEntries() const { return m_maxLogEntries; }
    void SetMaxHistoryEntries(size_t count) { m_maxHistoryEntries = count; }
    size_t GetMaxHistoryEntries() const { return m_maxHistoryEntries; }

    // Config file
    bool LoadConfig(const std::string& path);
    bool SaveConfig(const std::string& path);

    // Export log
    bool ExportLog(const std::string& path) const;

    // Callbacks
    using LogCallback = std::function<void(const LogEntry&)>;
    void SetOnLog(LogCallback callback) { m_onLog = callback; }

    // Built-in commands
    void RegisterBuiltinCommands();

private:
    // Parse command line into command and args
    void ParseCommandLine(const std::string& line, std::string& command,
                           std::vector<std::string>& args);

    bool m_visible = false;

    // Log entries
    std::deque<LogEntry> m_log;
    size_t m_maxLogEntries = 1000;
    LogLevel m_minLogLevel = LogLevel::Trace;
    std::string m_categoryFilter;

    // Command history
    std::deque<std::string> m_commandHistory;
    size_t m_maxHistoryEntries = 100;

    // Registered commands
    std::unordered_map<std::string, std::unique_ptr<ConsoleCommand>> m_commands;

    // Console variables
    std::unordered_map<std::string, std::unique_ptr<CVar>> m_cvars;

    // Thread safety
    std::mutex m_logMutex;
    std::mutex m_commandMutex;

    // Callbacks
    LogCallback m_onLog;

    // Cheats enabled
    bool m_cheatsEnabled = false;
};

// ============================================================================
// Global Logging Macros
// ============================================================================

#define CONSOLE_LOG(msg) \
    Cortex::Debug::DevConsole::Get().Info(msg)

#define CONSOLE_LOG_CATEGORY(cat, msg) \
    Cortex::Debug::DevConsole::Get().Info(msg, cat)

#define CONSOLE_TRACE(msg) \
    Cortex::Debug::DevConsole::Get().Trace(msg)

#define CONSOLE_DEBUG(msg) \
    Cortex::Debug::DevConsole::Get().Debug(msg)

#define CONSOLE_INFO(msg) \
    Cortex::Debug::DevConsole::Get().Info(msg)

#define CONSOLE_WARNING(msg) \
    Cortex::Debug::DevConsole::Get().Warning(msg)

#define CONSOLE_ERROR(msg) \
    Cortex::Debug::DevConsole::Get().Error(msg)

#define CONSOLE_FATAL(msg) \
    Cortex::Debug::DevConsole::Get().Fatal(msg)

// ============================================================================
// CVar Registration Helper
// ============================================================================

#define CVAR_DEFINE(type, name, defaultVal, desc) \
    static Cortex::Debug::CVar* g_cvar_##name = \
        Cortex::Debug::DevConsole::Get().RegisterCVar(#name, static_cast<type>(defaultVal), desc)

#define CVAR_DEFINE_FLAGS(type, name, defaultVal, desc, flags) \
    static Cortex::Debug::CVar* g_cvar_##name = \
        Cortex::Debug::DevConsole::Get().RegisterCVar(#name, static_cast<type>(defaultVal), desc, flags)

#define CVAR_GET(name) \
    Cortex::Debug::DevConsole::Get().GetCVar(#name)

// ============================================================================
// Console UI State
// ============================================================================

struct ConsoleUIState {
    // Input
    char inputBuffer[4096] = {0};
    int inputCursorPos = 0;
    int historyIndex = -1;

    // Scrolling
    float scrollY = 0.0f;
    bool scrollToBottom = true;

    // Autocomplete
    bool showAutocomplete = false;
    std::vector<std::string> autocompleteSuggestions;
    int autocompleteIndex = 0;

    // Appearance
    float alpha = 0.9f;
    float height = 0.4f;  // Fraction of screen height

    // Filter toggles
    bool showTrace = true;
    bool showDebug = true;
    bool showInfo = true;
    bool showWarning = true;
    bool showError = true;
};

// ============================================================================
// Debug Watch (variable watcher)
// ============================================================================

class DebugWatch {
public:
    struct WatchEntry {
        std::string name;
        std::string expression;
        std::function<std::string()> valueGetter;
        std::string lastValue;
        bool changed = false;
    };

    static DebugWatch& Get();

    // Add watch
    void AddWatch(const std::string& name, const std::string& expression,
                   std::function<std::string()> getter);
    void RemoveWatch(const std::string& name);
    void ClearWatches();

    // Update all watches
    void Update();

    // Get watches
    const std::vector<WatchEntry>& GetWatches() const { return m_watches; }

private:
    std::vector<WatchEntry> m_watches;
};

// ============================================================================
// Common CVars
// ============================================================================

namespace CommonCVars {
    // Rendering
    extern CVar* r_vsync;
    extern CVar* r_fpsLimit;
    extern CVar* r_resolution;
    extern CVar* r_fullscreen;
    extern CVar* r_shadowQuality;
    extern CVar* r_ssaoQuality;
    extern CVar* r_bloomEnabled;
    extern CVar* r_raytracing;

    // Debug
    extern CVar* debug_wireframe;
    extern CVar* debug_showFPS;
    extern CVar* debug_showStats;
    extern CVar* debug_drawColliders;
    extern CVar* debug_drawNavmesh;
    extern CVar* debug_pauseOnError;

    // Game
    extern CVar* g_cheats;
    extern CVar* g_timescale;
    extern CVar* g_gravity;

    // Audio
    extern CVar* snd_masterVolume;
    extern CVar* snd_sfxVolume;
    extern CVar* snd_musicVolume;

    // Network
    extern CVar* net_tickrate;
    extern CVar* net_maxPlayers;
    extern CVar* net_timeout;

    // Register all common cvars
    void RegisterAll();
}

// ============================================================================
// Built-in Console Commands
// ============================================================================

namespace BuiltinCommands {
    std::string Help(const std::vector<std::string>& args);
    std::string Clear(const std::vector<std::string>& args);
    std::string Echo(const std::vector<std::string>& args);
    std::string Exec(const std::vector<std::string>& args);
    std::string ListCVars(const std::vector<std::string>& args);
    std::string ListCommands(const std::vector<std::string>& args);
    std::string Set(const std::vector<std::string>& args);
    std::string Get(const std::vector<std::string>& args);
    std::string Reset(const std::vector<std::string>& args);
    std::string Quit(const std::vector<std::string>& args);
    std::string Screenshot(const std::vector<std::string>& args);
    std::string Bind(const std::vector<std::string>& args);
    std::string Unbind(const std::vector<std::string>& args);
    std::string Alias(const std::vector<std::string>& args);
    std::string Find(const std::vector<std::string>& args);
    std::string ToggleCVar(const std::vector<std::string>& args);
    std::string IncrementCVar(const std::vector<std::string>& args);
    std::string Version(const std::vector<std::string>& args);
    std::string Stats(const std::vector<std::string>& args);
}

} // namespace Cortex::Debug
