#pragma once

// ScriptEngine.h
// Lua scripting engine integration.
// Provides script execution, bindings, and hot-reload support.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <any>
#include <variant>
#include <optional>

// Forward declare Lua types
struct lua_State;

// Forward declare ECS types
namespace entt { class registry; }
using Entity = uint32_t;

namespace Cortex::Scripting {

// Forward declarations
class ScriptEngine;
class ScriptInstance;

// ============================================================================
// Script Value Types
// ============================================================================

// Variant type for script values
using ScriptValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    double,
    std::string,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    glm::quat,
    Entity,
    std::vector<ScriptValue>,
    std::unordered_map<std::string, ScriptValue>
>;

// Type enumeration
enum class ScriptValueType : uint8_t {
    Nil,
    Boolean,
    Integer,
    Number,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Entity,
    Array,
    Table,
    Function,
    UserData
};

// ============================================================================
// Script Function
// ============================================================================

class ScriptFunction {
public:
    ScriptFunction() = default;
    ScriptFunction(lua_State* L, int ref);
    ~ScriptFunction();

    // Move only
    ScriptFunction(ScriptFunction&& other) noexcept;
    ScriptFunction& operator=(ScriptFunction&& other) noexcept;
    ScriptFunction(const ScriptFunction&) = delete;
    ScriptFunction& operator=(const ScriptFunction&) = delete;

    // Check if valid
    bool IsValid() const { return m_ref != 0; }
    operator bool() const { return IsValid(); }

    // Call function
    template<typename... Args>
    std::optional<ScriptValue> Call(Args&&... args);

    // Call with vector of arguments
    std::optional<ScriptValue> CallWithArgs(const std::vector<ScriptValue>& args);

private:
    lua_State* m_lua = nullptr;
    int m_ref = 0;
};

// ============================================================================
// Script Table
// ============================================================================

class ScriptTable {
public:
    ScriptTable() = default;
    ScriptTable(lua_State* L, int ref);
    ~ScriptTable();

    // Move only
    ScriptTable(ScriptTable&& other) noexcept;
    ScriptTable& operator=(ScriptTable&& other) noexcept;
    ScriptTable(const ScriptTable&) = delete;
    ScriptTable& operator=(const ScriptTable&) = delete;

    // Check if valid
    bool IsValid() const { return m_ref != 0; }
    operator bool() const { return IsValid(); }

    // Get/Set values
    ScriptValue Get(const std::string& key) const;
    void Set(const std::string& key, const ScriptValue& value);

    // Get/Set with index
    ScriptValue GetIndex(int index) const;
    void SetIndex(int index, const ScriptValue& value);

    // Check if key exists
    bool Has(const std::string& key) const;

    // Get function
    ScriptFunction GetFunction(const std::string& key) const;

    // Iteration
    void ForEach(std::function<void(const std::string&, const ScriptValue&)> callback) const;

    // Get length (for array-like tables)
    size_t Length() const;

    // Get raw Lua reference
    int GetRef() const { return m_ref; }
    lua_State* GetLuaState() const { return m_lua; }

private:
    lua_State* m_lua = nullptr;
    int m_ref = 0;
};

// ============================================================================
// Script Instance (per-entity script state)
// ============================================================================

class ScriptInstance {
public:
    ScriptInstance(ScriptEngine* engine, const std::string& scriptPath, Entity entity);
    ~ScriptInstance();

    // Lifecycle
    bool Initialize();
    void Shutdown();

    // Update callbacks
    void OnStart();
    void OnUpdate(float deltaTime);
    void OnFixedUpdate(float fixedDeltaTime);
    void OnLateUpdate(float deltaTime);
    void OnDestroy();

    // Event callbacks
    void OnCollisionEnter(Entity other);
    void OnCollisionExit(Entity other);
    void OnTriggerEnter(Entity other);
    void OnTriggerExit(Entity other);

    // Custom event
    void SendMessage(const std::string& message, const std::vector<ScriptValue>& args = {});

    // State
    bool IsInitialized() const { return m_initialized; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    // Get script table (for inspector)
    ScriptTable& GetTable() { return m_instanceTable; }
    const ScriptTable& GetTable() const { return m_instanceTable; }

    // Get/Set properties
    ScriptValue GetProperty(const std::string& name) const;
    void SetProperty(const std::string& name, const ScriptValue& value);

    // Reload script
    bool Reload();

    // Get entity
    Entity GetEntity() const { return m_entity; }
    const std::string& GetScriptPath() const { return m_scriptPath; }

private:
    bool LoadScript();
    void CallMethod(const std::string& method, const std::vector<ScriptValue>& args = {});

    ScriptEngine* m_engine = nullptr;
    std::string m_scriptPath;
    Entity m_entity;

    ScriptTable m_instanceTable;
    bool m_initialized = false;
    bool m_enabled = true;

    // Cached method references
    ScriptFunction m_onStart;
    ScriptFunction m_onUpdate;
    ScriptFunction m_onFixedUpdate;
    ScriptFunction m_onLateUpdate;
    ScriptFunction m_onDestroy;
};

// ============================================================================
// Script Error
// ============================================================================

struct ScriptError {
    std::string message;
    std::string source;
    int line = 0;
    std::string stackTrace;

    std::string ToString() const;
};

// ============================================================================
// Script Engine
// ============================================================================

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // Initialization
    bool Initialize();
    void Shutdown();
    bool IsInitialized() const { return m_lua != nullptr; }

    // Set ECS registry
    void SetRegistry(entt::registry* registry) { m_registry = registry; }
    entt::registry* GetRegistry() const { return m_registry; }

    // Execute Lua code
    bool ExecuteString(const std::string& code, const std::string& chunkName = "chunk");
    bool ExecuteFile(const std::string& path);

    // Load script module
    ScriptTable LoadModule(const std::string& path);

    // Create new table
    ScriptTable CreateTable();

    // Global access
    void SetGlobal(const std::string& name, const ScriptValue& value);
    ScriptValue GetGlobal(const std::string& name) const;
    void SetGlobalFunction(const std::string& name, std::function<ScriptValue(const std::vector<ScriptValue>&)> func);

    // Script instances
    ScriptInstance* CreateInstance(const std::string& scriptPath, Entity entity);
    void DestroyInstance(Entity entity);
    ScriptInstance* GetInstance(Entity entity);

    // Update all instances
    void Update(float deltaTime);
    void FixedUpdate(float fixedDeltaTime);
    void LateUpdate(float deltaTime);

    // Error handling
    void SetErrorCallback(std::function<void(const ScriptError&)> callback);
    const ScriptError& GetLastError() const { return m_lastError; }
    bool HasError() const { return !m_lastError.message.empty(); }
    void ClearError() { m_lastError = {}; }

    // Hot reload
    void EnableHotReload(bool enable);
    bool IsHotReloadEnabled() const { return m_hotReloadEnabled; }
    void CheckForChanges();
    void ReloadScript(const std::string& path);
    void ReloadAll();

    // Script paths
    void AddSearchPath(const std::string& path);
    void SetScriptRoot(const std::string& path) { m_scriptRoot = path; }
    const std::string& GetScriptRoot() const { return m_scriptRoot; }

    // Garbage collection
    void CollectGarbage();
    size_t GetMemoryUsage() const;

    // Debug
    void EnableDebug(bool enable) { m_debugEnabled = enable; }
    bool IsDebugEnabled() const { return m_debugEnabled; }
    std::string GetStackTrace() const;

    // Get Lua state (for advanced use)
    lua_State* GetLuaState() const { return m_lua; }

    // Type registration helpers
    template<typename T>
    void RegisterType(const std::string& name);

    template<typename T, typename... Args>
    void RegisterConstructor();

    template<typename T, typename R, typename... Args>
    void RegisterMethod(const std::string& name, R(T::*method)(Args...));

    template<typename T, typename V>
    void RegisterProperty(const std::string& name, V T::*member);

private:
    // Stack helpers
    void PushValue(const ScriptValue& value);
    ScriptValue PopValue();
    ScriptValue GetValue(int index) const;

    // Error handling
    void HandleError(const std::string& context);
    static int LuaErrorHandler(lua_State* L);
    static int LuaPanicHandler(lua_State* L);

    // Module loader
    static int LuaModuleLoader(lua_State* L);
    std::string FindModule(const std::string& name) const;

    // File watching
    void StartFileWatcher();
    void StopFileWatcher();
    void FileWatcherThread();

    lua_State* m_lua = nullptr;
    entt::registry* m_registry = nullptr;

    // Script instances
    std::unordered_map<Entity, std::unique_ptr<ScriptInstance>> m_instances;

    // Paths
    std::string m_scriptRoot;
    std::vector<std::string> m_searchPaths;

    // Error handling
    ScriptError m_lastError;
    std::function<void(const ScriptError&)> m_errorCallback;

    // Hot reload
    bool m_hotReloadEnabled = false;
    std::unordered_map<std::string, int64_t> m_fileModTimes;
    std::thread m_watcherThread;
    std::atomic<bool> m_watcherRunning{false};
    std::mutex m_reloadMutex;
    std::vector<std::string> m_pendingReloads;

    // Settings
    bool m_debugEnabled = false;

    // Registered native functions
    std::unordered_map<std::string, std::function<ScriptValue(const std::vector<ScriptValue>&)>> m_nativeFunctions;
};

// ============================================================================
// Script Component
// ============================================================================

struct ScriptComponent {
    std::string scriptPath;
    bool enabled = true;
    bool autoStart = true;

    // Runtime state (not serialized)
    ScriptInstance* instance = nullptr;

    // Exposed properties (serialized)
    std::unordered_map<std::string, ScriptValue> properties;
};

// ============================================================================
// Script Utilities
// ============================================================================

namespace ScriptUtils {

// Convert ScriptValue to specific type
template<typename T>
T As(const ScriptValue& value);

template<> inline bool As<bool>(const ScriptValue& value) {
    if (auto* v = std::get_if<bool>(&value)) return *v;
    return false;
}

template<> inline int As<int>(const ScriptValue& value) {
    if (auto* v = std::get_if<int64_t>(&value)) return static_cast<int>(*v);
    if (auto* v = std::get_if<double>(&value)) return static_cast<int>(*v);
    return 0;
}

template<> inline float As<float>(const ScriptValue& value) {
    if (auto* v = std::get_if<double>(&value)) return static_cast<float>(*v);
    if (auto* v = std::get_if<int64_t>(&value)) return static_cast<float>(*v);
    return 0.0f;
}

template<> inline double As<double>(const ScriptValue& value) {
    if (auto* v = std::get_if<double>(&value)) return *v;
    if (auto* v = std::get_if<int64_t>(&value)) return static_cast<double>(*v);
    return 0.0;
}

template<> inline std::string As<std::string>(const ScriptValue& value) {
    if (auto* v = std::get_if<std::string>(&value)) return *v;
    return "";
}

template<> inline glm::vec3 As<glm::vec3>(const ScriptValue& value) {
    if (auto* v = std::get_if<glm::vec3>(&value)) return *v;
    return glm::vec3(0.0f);
}

template<> inline glm::quat As<glm::quat>(const ScriptValue& value) {
    if (auto* v = std::get_if<glm::quat>(&value)) return *v;
    return glm::quat(1, 0, 0, 0);
}

template<> inline Entity As<Entity>(const ScriptValue& value) {
    if (auto* v = std::get_if<Entity>(&value)) return *v;
    return 0;
}

// Get type of ScriptValue
ScriptValueType GetType(const ScriptValue& value);

// Convert ScriptValue to string representation
std::string ToString(const ScriptValue& value);

// Check if value is nil
bool IsNil(const ScriptValue& value);

// Create table from initializer list
ScriptValue MakeTable(std::initializer_list<std::pair<std::string, ScriptValue>> values);

// Create array from vector
ScriptValue MakeArray(const std::vector<ScriptValue>& values);

} // namespace ScriptUtils

// ============================================================================
// Coroutine Support
// ============================================================================

class ScriptCoroutine {
public:
    ScriptCoroutine(lua_State* L, int ref);
    ~ScriptCoroutine();

    // Move only
    ScriptCoroutine(ScriptCoroutine&& other) noexcept;
    ScriptCoroutine& operator=(ScriptCoroutine&& other) noexcept;

    // Resume coroutine
    enum class Status {
        Running,
        Suspended,
        Finished,
        Error
    };

    Status Resume(const std::vector<ScriptValue>& args = {});
    Status GetStatus() const { return m_status; }

    // Get yield values
    std::vector<ScriptValue> GetYieldValues() const { return m_yieldValues; }

    // Check if finished
    bool IsFinished() const { return m_status == Status::Finished || m_status == Status::Error; }

private:
    lua_State* m_lua = nullptr;
    lua_State* m_thread = nullptr;
    int m_ref = 0;
    Status m_status = Status::Suspended;
    std::vector<ScriptValue> m_yieldValues;
};

// ============================================================================
// Timer System (for script delays)
// ============================================================================

class ScriptTimerManager {
public:
    struct Timer {
        uint32_t id;
        float delay;
        float elapsed;
        bool repeat;
        ScriptFunction callback;
    };

    ScriptTimerManager();
    ~ScriptTimerManager() = default;

    // Create timers
    uint32_t SetTimeout(ScriptFunction callback, float delay);
    uint32_t SetInterval(ScriptFunction callback, float interval);

    // Cancel timer
    void ClearTimer(uint32_t id);
    void ClearAll();

    // Update
    void Update(float deltaTime);

private:
    std::vector<Timer> m_timers;
    uint32_t m_nextId = 1;
};

} // namespace Cortex::Scripting
