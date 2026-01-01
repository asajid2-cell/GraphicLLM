// ScriptEngine.cpp
// Lua scripting engine implementation.

#include "ScriptEngine.h"
#include <lua.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace Cortex::Scripting {

// ============================================================================
// ScriptFunction Implementation
// ============================================================================

ScriptFunction::ScriptFunction(lua_State* L, int ref)
    : m_lua(L), m_ref(ref) {}

ScriptFunction::~ScriptFunction() {
    if (m_lua && m_ref != 0) {
        luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
    }
}

ScriptFunction::ScriptFunction(ScriptFunction&& other) noexcept
    : m_lua(other.m_lua), m_ref(other.m_ref) {
    other.m_lua = nullptr;
    other.m_ref = 0;
}

ScriptFunction& ScriptFunction::operator=(ScriptFunction&& other) noexcept {
    if (this != &other) {
        if (m_lua && m_ref != 0) {
            luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
        }
        m_lua = other.m_lua;
        m_ref = other.m_ref;
        other.m_lua = nullptr;
        other.m_ref = 0;
    }
    return *this;
}

std::optional<ScriptValue> ScriptFunction::CallWithArgs(const std::vector<ScriptValue>& args) {
    if (!IsValid()) return std::nullopt;

    // Get function from registry
    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 1);
        return std::nullopt;
    }

    // Push arguments
    for (const auto& arg : args) {
        PushScriptValue(m_lua, arg);
    }

    // Call function
    if (lua_pcall(m_lua, static_cast<int>(args.size()), 1, 0) != LUA_OK) {
        // Error occurred
        lua_pop(m_lua, 1);
        return std::nullopt;
    }

    // Get return value
    ScriptValue result = GetScriptValue(m_lua, -1);
    lua_pop(m_lua, 1);
    return result;
}

// Helper to push ScriptValue to Lua stack
void PushScriptValue(lua_State* L, const ScriptValue& value) {
    std::visit([L](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            lua_pushnil(L);
        } else if constexpr (std::is_same_v<T, bool>) {
            lua_pushboolean(L, arg ? 1 : 0);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            lua_pushinteger(L, static_cast<lua_Integer>(arg));
        } else if constexpr (std::is_same_v<T, double>) {
            lua_pushnumber(L, arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            lua_pushlstring(L, arg.c_str(), arg.size());
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
            lua_newtable(L);
            lua_pushnumber(L, arg.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, arg.y); lua_setfield(L, -2, "y");
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            lua_newtable(L);
            lua_pushnumber(L, arg.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, arg.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, arg.z); lua_setfield(L, -2, "z");
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
            lua_newtable(L);
            lua_pushnumber(L, arg.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, arg.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, arg.z); lua_setfield(L, -2, "z");
            lua_pushnumber(L, arg.w); lua_setfield(L, -2, "w");
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            lua_newtable(L);
            lua_pushnumber(L, arg.x); lua_setfield(L, -2, "x");
            lua_pushnumber(L, arg.y); lua_setfield(L, -2, "y");
            lua_pushnumber(L, arg.z); lua_setfield(L, -2, "z");
            lua_pushnumber(L, arg.w); lua_setfield(L, -2, "w");
        } else if constexpr (std::is_same_v<T, Entity>) {
            lua_pushinteger(L, static_cast<lua_Integer>(arg));
        } else if constexpr (std::is_same_v<T, std::vector<ScriptValue>>) {
            lua_newtable(L);
            for (size_t i = 0; i < arg.size(); ++i) {
                PushScriptValue(L, arg[i]);
                lua_rawseti(L, -2, static_cast<int>(i + 1));
            }
        } else if constexpr (std::is_same_v<T, std::unordered_map<std::string, ScriptValue>>) {
            lua_newtable(L);
            for (const auto& [key, val] : arg) {
                PushScriptValue(L, val);
                lua_setfield(L, -2, key.c_str());
            }
        }
    }, value);
}

// Helper to get ScriptValue from Lua stack
ScriptValue GetScriptValue(lua_State* L, int index) {
    int type = lua_type(L, index);
    switch (type) {
        case LUA_TNIL:
            return nullptr;
        case LUA_TBOOLEAN:
            return static_cast<bool>(lua_toboolean(L, index));
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                return static_cast<int64_t>(lua_tointeger(L, index));
            }
            return lua_tonumber(L, index);
        case LUA_TSTRING: {
            size_t len;
            const char* str = lua_tolstring(L, index, &len);
            return std::string(str, len);
        }
        case LUA_TTABLE: {
            // Check if it's a vec3
            lua_getfield(L, index, "x");
            bool hasX = !lua_isnil(L, -1);
            lua_pop(L, 1);

            if (hasX) {
                lua_getfield(L, index, "z");
                bool hasZ = !lua_isnil(L, -1);
                lua_pop(L, 1);

                lua_getfield(L, index, "w");
                bool hasW = !lua_isnil(L, -1);
                lua_pop(L, 1);

                if (hasW) {
                    // vec4 or quat
                    glm::vec4 v;
                    lua_getfield(L, index, "x"); v.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "y"); v.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "z"); v.z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "w"); v.w = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    return v;
                } else if (hasZ) {
                    // vec3
                    glm::vec3 v;
                    lua_getfield(L, index, "x"); v.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "y"); v.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "z"); v.z = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    return v;
                } else {
                    // vec2
                    glm::vec2 v;
                    lua_getfield(L, index, "x"); v.x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    lua_getfield(L, index, "y"); v.y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
                    return v;
                }
            }

            // Check if array or table
            size_t arrayLen = lua_rawlen(L, index);
            if (arrayLen > 0) {
                std::vector<ScriptValue> arr;
                arr.reserve(arrayLen);
                for (size_t i = 1; i <= arrayLen; ++i) {
                    lua_rawgeti(L, index, static_cast<int>(i));
                    arr.push_back(GetScriptValue(L, -1));
                    lua_pop(L, 1);
                }
                return arr;
            }

            // Generic table
            std::unordered_map<std::string, ScriptValue> table;
            lua_pushnil(L);
            while (lua_next(L, index < 0 ? index - 1 : index) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    std::string key = lua_tostring(L, -2);
                    table[key] = GetScriptValue(L, -1);
                }
                lua_pop(L, 1);
            }
            return table;
        }
        default:
            return nullptr;
    }
}

// ============================================================================
// ScriptTable Implementation
// ============================================================================

ScriptTable::ScriptTable(lua_State* L, int ref)
    : m_lua(L), m_ref(ref) {}

ScriptTable::~ScriptTable() {
    if (m_lua && m_ref != 0) {
        luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
    }
}

ScriptTable::ScriptTable(ScriptTable&& other) noexcept
    : m_lua(other.m_lua), m_ref(other.m_ref) {
    other.m_lua = nullptr;
    other.m_ref = 0;
}

ScriptTable& ScriptTable::operator=(ScriptTable&& other) noexcept {
    if (this != &other) {
        if (m_lua && m_ref != 0) {
            luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
        }
        m_lua = other.m_lua;
        m_ref = other.m_ref;
        other.m_lua = nullptr;
        other.m_ref = 0;
    }
    return *this;
}

ScriptValue ScriptTable::Get(const std::string& key) const {
    if (!IsValid()) return nullptr;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    lua_getfield(m_lua, -1, key.c_str());
    ScriptValue result = GetScriptValue(m_lua, -1);
    lua_pop(m_lua, 2);
    return result;
}

void ScriptTable::Set(const std::string& key, const ScriptValue& value) {
    if (!IsValid()) return;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    PushScriptValue(m_lua, value);
    lua_setfield(m_lua, -2, key.c_str());
    lua_pop(m_lua, 1);
}

ScriptValue ScriptTable::GetIndex(int index) const {
    if (!IsValid()) return nullptr;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    lua_rawgeti(m_lua, -1, index);
    ScriptValue result = GetScriptValue(m_lua, -1);
    lua_pop(m_lua, 2);
    return result;
}

void ScriptTable::SetIndex(int index, const ScriptValue& value) {
    if (!IsValid()) return;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    PushScriptValue(m_lua, value);
    lua_rawseti(m_lua, -2, index);
    lua_pop(m_lua, 1);
}

bool ScriptTable::Has(const std::string& key) const {
    if (!IsValid()) return false;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    lua_getfield(m_lua, -1, key.c_str());
    bool hasKey = !lua_isnil(m_lua, -1);
    lua_pop(m_lua, 2);
    return hasKey;
}

ScriptFunction ScriptTable::GetFunction(const std::string& key) const {
    if (!IsValid()) return ScriptFunction();

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    lua_getfield(m_lua, -1, key.c_str());

    if (!lua_isfunction(m_lua, -1)) {
        lua_pop(m_lua, 2);
        return ScriptFunction();
    }

    int ref = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    lua_pop(m_lua, 1);
    return ScriptFunction(m_lua, ref);
}

void ScriptTable::ForEach(std::function<void(const std::string&, const ScriptValue&)> callback) const {
    if (!IsValid()) return;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    lua_pushnil(m_lua);
    while (lua_next(m_lua, -2) != 0) {
        if (lua_type(m_lua, -2) == LUA_TSTRING) {
            std::string key = lua_tostring(m_lua, -2);
            ScriptValue value = GetScriptValue(m_lua, -1);
            callback(key, value);
        }
        lua_pop(m_lua, 1);
    }
    lua_pop(m_lua, 1);
}

size_t ScriptTable::Length() const {
    if (!IsValid()) return 0;

    lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_ref);
    size_t len = lua_rawlen(m_lua, -1);
    lua_pop(m_lua, 1);
    return len;
}

// ============================================================================
// ScriptInstance Implementation
// ============================================================================

ScriptInstance::ScriptInstance(ScriptEngine* engine, const std::string& scriptPath, Entity entity)
    : m_engine(engine), m_scriptPath(scriptPath), m_entity(entity) {}

ScriptInstance::~ScriptInstance() {
    Shutdown();
}

bool ScriptInstance::Initialize() {
    if (m_initialized) return true;

    if (!LoadScript()) {
        return false;
    }

    m_initialized = true;
    return true;
}

void ScriptInstance::Shutdown() {
    if (!m_initialized) return;

    OnDestroy();
    m_instanceTable = ScriptTable();
    m_onStart = ScriptFunction();
    m_onUpdate = ScriptFunction();
    m_onFixedUpdate = ScriptFunction();
    m_onLateUpdate = ScriptFunction();
    m_onDestroy = ScriptFunction();
    m_initialized = false;
}

bool ScriptInstance::LoadScript() {
    // Load the script module
    ScriptTable module = m_engine->LoadModule(m_scriptPath);
    if (!module.IsValid()) {
        return false;
    }

    // Create instance table
    lua_State* L = m_engine->GetLuaState();
    lua_newtable(L);

    // Set entity reference
    lua_pushinteger(L, static_cast<lua_Integer>(m_entity));
    lua_setfield(L, -2, "entity");

    // Copy module members to instance
    lua_rawgeti(L, LUA_REGISTRYINDEX, module.GetRef());
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        // key at -2, value at -1
        lua_pushvalue(L, -2);  // copy key
        lua_pushvalue(L, -2);  // copy value
        lua_settable(L, -6);   // instance[key] = value
        lua_pop(L, 1);  // pop value, keep key for next iteration
    }
    lua_pop(L, 1);  // pop module table

    // Store instance table reference
    int instanceRef = luaL_ref(L, LUA_REGISTRYINDEX);
    m_instanceTable = ScriptTable(L, instanceRef);

    // Cache method references
    m_onStart = m_instanceTable.GetFunction("OnStart");
    m_onUpdate = m_instanceTable.GetFunction("OnUpdate");
    m_onFixedUpdate = m_instanceTable.GetFunction("OnFixedUpdate");
    m_onLateUpdate = m_instanceTable.GetFunction("OnLateUpdate");
    m_onDestroy = m_instanceTable.GetFunction("OnDestroy");

    return true;
}

void ScriptInstance::CallMethod(const std::string& method, const std::vector<ScriptValue>& args) {
    if (!m_initialized || !m_instanceTable.IsValid()) return;

    lua_State* L = m_engine->GetLuaState();

    // Get method from instance table
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_instanceTable.GetRef());
    lua_getfield(L, -1, method.c_str());

    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }

    // Push self (instance table)
    lua_rawgeti(L, LUA_REGISTRYINDEX, m_instanceTable.GetRef());

    // Push arguments
    for (const auto& arg : args) {
        PushScriptValue(L, arg);
    }

    // Call method (self + args)
    int numArgs = static_cast<int>(args.size()) + 1;
    if (lua_pcall(L, numArgs, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        if (error) {
            // Report error through engine
        }
        lua_pop(L, 1);
    }

    lua_pop(L, 1);  // pop instance table
}

void ScriptInstance::OnStart() {
    if (m_enabled && m_onStart.IsValid()) {
        CallMethod("OnStart");
    }
}

void ScriptInstance::OnUpdate(float deltaTime) {
    if (m_enabled && m_onUpdate.IsValid()) {
        CallMethod("OnUpdate", {static_cast<double>(deltaTime)});
    }
}

void ScriptInstance::OnFixedUpdate(float fixedDeltaTime) {
    if (m_enabled && m_onFixedUpdate.IsValid()) {
        CallMethod("OnFixedUpdate", {static_cast<double>(fixedDeltaTime)});
    }
}

void ScriptInstance::OnLateUpdate(float deltaTime) {
    if (m_enabled && m_onLateUpdate.IsValid()) {
        CallMethod("OnLateUpdate", {static_cast<double>(deltaTime)});
    }
}

void ScriptInstance::OnDestroy() {
    if (m_onDestroy.IsValid()) {
        CallMethod("OnDestroy");
    }
}

void ScriptInstance::OnCollisionEnter(Entity other) {
    CallMethod("OnCollisionEnter", {other});
}

void ScriptInstance::OnCollisionExit(Entity other) {
    CallMethod("OnCollisionExit", {other});
}

void ScriptInstance::OnTriggerEnter(Entity other) {
    CallMethod("OnTriggerEnter", {other});
}

void ScriptInstance::OnTriggerExit(Entity other) {
    CallMethod("OnTriggerExit", {other});
}

void ScriptInstance::SendMessage(const std::string& message, const std::vector<ScriptValue>& args) {
    CallMethod(message, args);
}

ScriptValue ScriptInstance::GetProperty(const std::string& name) const {
    return m_instanceTable.Get(name);
}

void ScriptInstance::SetProperty(const std::string& name, const ScriptValue& value) {
    m_instanceTable.Set(name, value);
}

bool ScriptInstance::Reload() {
    Shutdown();
    return Initialize();
}

// ============================================================================
// ScriptError Implementation
// ============================================================================

std::string ScriptError::ToString() const {
    std::string result = message;
    if (!source.empty()) {
        result += " in " + source;
        if (line > 0) {
            result += ":" + std::to_string(line);
        }
    }
    if (!stackTrace.empty()) {
        result += "\n" + stackTrace;
    }
    return result;
}

// ============================================================================
// ScriptEngine Implementation
// ============================================================================

ScriptEngine::ScriptEngine() {}

ScriptEngine::~ScriptEngine() {
    Shutdown();
}

bool ScriptEngine::Initialize() {
    if (m_lua) return true;

    // Create Lua state
    m_lua = luaL_newstate();
    if (!m_lua) return false;

    // Open standard libraries
    luaL_openlibs(m_lua);

    // Set panic handler
    lua_atpanic(m_lua, LuaPanicHandler);

    // Setup custom module loader
    lua_getglobal(m_lua, "package");
    lua_getfield(m_lua, -1, "searchers");

    // Get table length
    int len = static_cast<int>(lua_rawlen(m_lua, -1));

    // Add our loader at position 2 (after preload)
    for (int i = len; i >= 2; --i) {
        lua_rawgeti(m_lua, -1, i);
        lua_rawseti(m_lua, -2, i + 1);
    }

    lua_pushlightuserdata(m_lua, this);
    lua_pushcclosure(m_lua, LuaModuleLoader, 1);
    lua_rawseti(m_lua, -2, 2);

    lua_pop(m_lua, 2);  // pop searchers and package

    // Create global 'engine' table for API
    lua_newtable(m_lua);
    lua_setglobal(m_lua, "engine");

    return true;
}

void ScriptEngine::Shutdown() {
    StopFileWatcher();

    m_instances.clear();
    m_nativeFunctions.clear();
    m_fileModTimes.clear();
    m_pendingReloads.clear();

    if (m_lua) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
}

bool ScriptEngine::ExecuteString(const std::string& code, const std::string& chunkName) {
    if (!m_lua) return false;

    ClearError();

    // Compile
    if (luaL_loadbuffer(m_lua, code.c_str(), code.size(), chunkName.c_str()) != LUA_OK) {
        HandleError("compilation");
        return false;
    }

    // Execute
    lua_pushcfunction(m_lua, LuaErrorHandler);
    lua_insert(m_lua, -2);

    if (lua_pcall(m_lua, 0, 0, -2) != LUA_OK) {
        HandleError("execution");
        lua_pop(m_lua, 1);  // pop error handler
        return false;
    }

    lua_pop(m_lua, 1);  // pop error handler
    return true;
}

bool ScriptEngine::ExecuteFile(const std::string& path) {
    if (!m_lua) return false;

    std::string fullPath = FindModule(path);
    if (fullPath.empty()) {
        m_lastError.message = "File not found: " + path;
        m_lastError.source = path;
        if (m_errorCallback) m_errorCallback(m_lastError);
        return false;
    }

    // Read file
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        m_lastError.message = "Cannot open file: " + fullPath;
        m_lastError.source = path;
        if (m_errorCallback) m_errorCallback(m_lastError);
        return false;
    }

    std::string code((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    // Track file for hot reload
    if (m_hotReloadEnabled) {
        auto modTime = std::filesystem::last_write_time(fullPath);
        m_fileModTimes[fullPath] = modTime.time_since_epoch().count();
    }

    return ExecuteString(code, "@" + path);
}

ScriptTable ScriptEngine::LoadModule(const std::string& path) {
    if (!m_lua) return ScriptTable();

    std::string fullPath = FindModule(path);
    if (fullPath.empty()) {
        m_lastError.message = "Module not found: " + path;
        m_lastError.source = path;
        if (m_errorCallback) m_errorCallback(m_lastError);
        return ScriptTable();
    }

    // Read file
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        m_lastError.message = "Cannot open module: " + fullPath;
        m_lastError.source = path;
        if (m_errorCallback) m_errorCallback(m_lastError);
        return ScriptTable();
    }

    std::string code((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    // Compile
    if (luaL_loadbuffer(m_lua, code.c_str(), code.size(), ("@" + path).c_str()) != LUA_OK) {
        HandleError("compilation");
        return ScriptTable();
    }

    // Execute (module should return a table)
    lua_pushcfunction(m_lua, LuaErrorHandler);
    lua_insert(m_lua, -2);

    if (lua_pcall(m_lua, 0, 1, -2) != LUA_OK) {
        HandleError("execution");
        lua_pop(m_lua, 1);  // pop error handler
        return ScriptTable();
    }

    lua_remove(m_lua, -2);  // remove error handler

    if (!lua_istable(m_lua, -1)) {
        lua_pop(m_lua, 1);
        m_lastError.message = "Module did not return a table: " + path;
        m_lastError.source = path;
        if (m_errorCallback) m_errorCallback(m_lastError);
        return ScriptTable();
    }

    // Track file for hot reload
    if (m_hotReloadEnabled) {
        auto modTime = std::filesystem::last_write_time(fullPath);
        m_fileModTimes[fullPath] = modTime.time_since_epoch().count();
    }

    // Store table reference
    int ref = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    return ScriptTable(m_lua, ref);
}

ScriptTable ScriptEngine::CreateTable() {
    if (!m_lua) return ScriptTable();

    lua_newtable(m_lua);
    int ref = luaL_ref(m_lua, LUA_REGISTRYINDEX);
    return ScriptTable(m_lua, ref);
}

void ScriptEngine::SetGlobal(const std::string& name, const ScriptValue& value) {
    if (!m_lua) return;

    PushScriptValue(m_lua, value);
    lua_setglobal(m_lua, name.c_str());
}

ScriptValue ScriptEngine::GetGlobal(const std::string& name) const {
    if (!m_lua) return nullptr;

    lua_getglobal(m_lua, name.c_str());
    ScriptValue result = GetScriptValue(m_lua, -1);
    lua_pop(m_lua, 1);
    return result;
}

void ScriptEngine::SetGlobalFunction(const std::string& name,
                                       std::function<ScriptValue(const std::vector<ScriptValue>&)> func) {
    if (!m_lua) return;

    m_nativeFunctions[name] = func;

    // Create C closure
    lua_pushlightuserdata(m_lua, this);
    lua_pushstring(m_lua, name.c_str());
    lua_pushcclosure(m_lua, [](lua_State* L) -> int {
        ScriptEngine* engine = static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
        const char* funcName = lua_tostring(L, lua_upvalueindex(2));

        auto it = engine->m_nativeFunctions.find(funcName);
        if (it == engine->m_nativeFunctions.end()) {
            lua_pushnil(L);
            return 1;
        }

        // Collect arguments
        std::vector<ScriptValue> args;
        int numArgs = lua_gettop(L);
        for (int i = 1; i <= numArgs; ++i) {
            args.push_back(GetScriptValue(L, i));
        }

        // Call function
        ScriptValue result = it->second(args);
        PushScriptValue(L, result);
        return 1;
    }, 2);

    lua_setglobal(m_lua, name.c_str());
}

ScriptInstance* ScriptEngine::CreateInstance(const std::string& scriptPath, Entity entity) {
    auto instance = std::make_unique<ScriptInstance>(this, scriptPath, entity);
    if (!instance->Initialize()) {
        return nullptr;
    }

    ScriptInstance* ptr = instance.get();
    m_instances[entity] = std::move(instance);
    return ptr;
}

void ScriptEngine::DestroyInstance(Entity entity) {
    auto it = m_instances.find(entity);
    if (it != m_instances.end()) {
        it->second->Shutdown();
        m_instances.erase(it);
    }
}

ScriptInstance* ScriptEngine::GetInstance(Entity entity) {
    auto it = m_instances.find(entity);
    return (it != m_instances.end()) ? it->second.get() : nullptr;
}

void ScriptEngine::Update(float deltaTime) {
    for (auto& [entity, instance] : m_instances) {
        if (instance->IsEnabled()) {
            instance->OnUpdate(deltaTime);
        }
    }
}

void ScriptEngine::FixedUpdate(float fixedDeltaTime) {
    for (auto& [entity, instance] : m_instances) {
        if (instance->IsEnabled()) {
            instance->OnFixedUpdate(fixedDeltaTime);
        }
    }
}

void ScriptEngine::LateUpdate(float deltaTime) {
    for (auto& [entity, instance] : m_instances) {
        if (instance->IsEnabled()) {
            instance->OnLateUpdate(deltaTime);
        }
    }
}

void ScriptEngine::SetErrorCallback(std::function<void(const ScriptError&)> callback) {
    m_errorCallback = callback;
}

void ScriptEngine::EnableHotReload(bool enable) {
    if (enable && !m_hotReloadEnabled) {
        m_hotReloadEnabled = true;
        StartFileWatcher();
    } else if (!enable && m_hotReloadEnabled) {
        m_hotReloadEnabled = false;
        StopFileWatcher();
    }
}

void ScriptEngine::CheckForChanges() {
    if (!m_hotReloadEnabled) return;

    std::vector<std::string> changedFiles;

    for (auto& [path, lastModTime] : m_fileModTimes) {
        if (std::filesystem::exists(path)) {
            auto currentModTime = std::filesystem::last_write_time(path);
            int64_t currentTimeValue = currentModTime.time_since_epoch().count();

            if (currentTimeValue != lastModTime) {
                changedFiles.push_back(path);
                lastModTime = currentTimeValue;
            }
        }
    }

    for (const auto& path : changedFiles) {
        ReloadScript(path);
    }
}

void ScriptEngine::ReloadScript(const std::string& path) {
    // Find instances using this script
    for (auto& [entity, instance] : m_instances) {
        std::string fullPath = FindModule(instance->GetScriptPath());
        if (fullPath == path || instance->GetScriptPath() == path) {
            instance->Reload();
        }
    }
}

void ScriptEngine::ReloadAll() {
    for (auto& [entity, instance] : m_instances) {
        instance->Reload();
    }
}

void ScriptEngine::AddSearchPath(const std::string& path) {
    m_searchPaths.push_back(path);
}

void ScriptEngine::CollectGarbage() {
    if (m_lua) {
        lua_gc(m_lua, LUA_GCCOLLECT, 0);
    }
}

size_t ScriptEngine::GetMemoryUsage() const {
    if (!m_lua) return 0;
    return static_cast<size_t>(lua_gc(m_lua, LUA_GCCOUNT, 0)) * 1024 +
           static_cast<size_t>(lua_gc(m_lua, LUA_GCCOUNTB, 0));
}

std::string ScriptEngine::GetStackTrace() const {
    if (!m_lua) return "";

    luaL_traceback(m_lua, m_lua, nullptr, 1);
    std::string trace = lua_tostring(m_lua, -1);
    lua_pop(m_lua, 1);
    return trace;
}

void ScriptEngine::PushValue(const ScriptValue& value) {
    PushScriptValue(m_lua, value);
}

ScriptValue ScriptEngine::PopValue() {
    ScriptValue result = GetScriptValue(m_lua, -1);
    lua_pop(m_lua, 1);
    return result;
}

ScriptValue ScriptEngine::GetValue(int index) const {
    return GetScriptValue(m_lua, index);
}

void ScriptEngine::HandleError(const std::string& context) {
    if (!m_lua) return;

    m_lastError.message = lua_tostring(m_lua, -1);
    lua_pop(m_lua, 1);

    // Parse source and line from error message
    // Format: "@source:line: message" or "source:line: message"
    size_t colonPos = m_lastError.message.find(':');
    if (colonPos != std::string::npos) {
        size_t start = (m_lastError.message[0] == '@') ? 1 : 0;
        m_lastError.source = m_lastError.message.substr(start, colonPos - start);

        size_t secondColon = m_lastError.message.find(':', colonPos + 1);
        if (secondColon != std::string::npos) {
            std::string lineStr = m_lastError.message.substr(colonPos + 1,
                                                               secondColon - colonPos - 1);
            try {
                m_lastError.line = std::stoi(lineStr);
            } catch (...) {
                m_lastError.line = 0;
            }
        }
    }

    m_lastError.stackTrace = GetStackTrace();

    if (m_errorCallback) {
        m_errorCallback(m_lastError);
    }
}

int ScriptEngine::LuaErrorHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

int ScriptEngine::LuaPanicHandler(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    // In a real application, log this error
    (void)msg;
    return 0;
}

int ScriptEngine::LuaModuleLoader(lua_State* L) {
    ScriptEngine* engine = static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* moduleName = lua_tostring(L, 1);

    std::string path = engine->FindModule(moduleName);
    if (path.empty()) {
        lua_pushstring(L, ("Module not found: " + std::string(moduleName)).c_str());
        return 1;
    }

    // Read file
    std::ifstream file(path);
    if (!file.is_open()) {
        lua_pushstring(L, ("Cannot open module: " + path).c_str());
        return 1;
    }

    std::string code((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    // Compile and return loader function
    if (luaL_loadbuffer(L, code.c_str(), code.size(), ("@" + path).c_str()) != LUA_OK) {
        return 1;
    }

    lua_pushstring(L, path.c_str());
    return 2;
}

std::string ScriptEngine::FindModule(const std::string& name) const {
    // Convert module name to path (replace . with /)
    std::string pathName = name;
    std::replace(pathName.begin(), pathName.end(), '.', '/');

    // Add .lua extension if not present
    if (pathName.size() < 4 || pathName.substr(pathName.size() - 4) != ".lua") {
        pathName += ".lua";
    }

    // Check script root
    if (!m_scriptRoot.empty()) {
        std::string fullPath = m_scriptRoot + "/" + pathName;
        if (std::filesystem::exists(fullPath)) {
            return fullPath;
        }
    }

    // Check search paths
    for (const auto& searchPath : m_searchPaths) {
        std::string fullPath = searchPath + "/" + pathName;
        if (std::filesystem::exists(fullPath)) {
            return fullPath;
        }
    }

    // Check if absolute path
    if (std::filesystem::exists(pathName)) {
        return pathName;
    }

    return "";
}

void ScriptEngine::StartFileWatcher() {
    if (m_watcherRunning) return;

    m_watcherRunning = true;
    m_watcherThread = std::thread(&ScriptEngine::FileWatcherThread, this);
}

void ScriptEngine::StopFileWatcher() {
    if (!m_watcherRunning) return;

    m_watcherRunning = false;
    if (m_watcherThread.joinable()) {
        m_watcherThread.join();
    }
}

void ScriptEngine::FileWatcherThread() {
    while (m_watcherRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::vector<std::string> changed;

        {
            std::lock_guard<std::mutex> lock(m_reloadMutex);
            for (auto& [path, lastModTime] : m_fileModTimes) {
                if (std::filesystem::exists(path)) {
                    auto currentModTime = std::filesystem::last_write_time(path);
                    int64_t currentTimeValue = currentModTime.time_since_epoch().count();

                    if (currentTimeValue != lastModTime) {
                        changed.push_back(path);
                        lastModTime = currentTimeValue;
                    }
                }
            }

            for (const auto& path : changed) {
                m_pendingReloads.push_back(path);
            }
        }
    }
}

// ============================================================================
// ScriptCoroutine Implementation
// ============================================================================

ScriptCoroutine::ScriptCoroutine(lua_State* L, int ref)
    : m_lua(L), m_ref(ref) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    m_thread = lua_tothread(L, -1);
    lua_pop(L, 1);
}

ScriptCoroutine::~ScriptCoroutine() {
    if (m_lua && m_ref != 0) {
        luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
    }
}

ScriptCoroutine::ScriptCoroutine(ScriptCoroutine&& other) noexcept
    : m_lua(other.m_lua), m_thread(other.m_thread), m_ref(other.m_ref),
      m_status(other.m_status), m_yieldValues(std::move(other.m_yieldValues)) {
    other.m_lua = nullptr;
    other.m_thread = nullptr;
    other.m_ref = 0;
}

ScriptCoroutine& ScriptCoroutine::operator=(ScriptCoroutine&& other) noexcept {
    if (this != &other) {
        if (m_lua && m_ref != 0) {
            luaL_unref(m_lua, LUA_REGISTRYINDEX, m_ref);
        }
        m_lua = other.m_lua;
        m_thread = other.m_thread;
        m_ref = other.m_ref;
        m_status = other.m_status;
        m_yieldValues = std::move(other.m_yieldValues);
        other.m_lua = nullptr;
        other.m_thread = nullptr;
        other.m_ref = 0;
    }
    return *this;
}

ScriptCoroutine::Status ScriptCoroutine::Resume(const std::vector<ScriptValue>& args) {
    if (!m_thread || m_status == Status::Finished || m_status == Status::Error) {
        return m_status;
    }

    // Push arguments
    for (const auto& arg : args) {
        PushScriptValue(m_thread, arg);
    }

    int nres = 0;
    int result = lua_resume(m_thread, m_lua, static_cast<int>(args.size()), &nres);

    m_yieldValues.clear();

    switch (result) {
        case LUA_OK:
            m_status = Status::Finished;
            // Collect return values
            for (int i = 0; i < nres; ++i) {
                m_yieldValues.push_back(GetScriptValue(m_thread, -nres + i));
            }
            lua_pop(m_thread, nres);
            break;

        case LUA_YIELD:
            m_status = Status::Suspended;
            // Collect yield values
            for (int i = 0; i < nres; ++i) {
                m_yieldValues.push_back(GetScriptValue(m_thread, -nres + i));
            }
            lua_pop(m_thread, nres);
            break;

        default:
            m_status = Status::Error;
            break;
    }

    return m_status;
}

// ============================================================================
// ScriptTimerManager Implementation
// ============================================================================

ScriptTimerManager::ScriptTimerManager() {}

uint32_t ScriptTimerManager::SetTimeout(ScriptFunction callback, float delay) {
    uint32_t id = m_nextId++;
    m_timers.push_back({id, delay, 0.0f, false, std::move(callback)});
    return id;
}

uint32_t ScriptTimerManager::SetInterval(ScriptFunction callback, float interval) {
    uint32_t id = m_nextId++;
    m_timers.push_back({id, interval, 0.0f, true, std::move(callback)});
    return id;
}

void ScriptTimerManager::ClearTimer(uint32_t id) {
    auto it = std::remove_if(m_timers.begin(), m_timers.end(),
                              [id](const Timer& t) { return t.id == id; });
    m_timers.erase(it, m_timers.end());
}

void ScriptTimerManager::ClearAll() {
    m_timers.clear();
}

void ScriptTimerManager::Update(float deltaTime) {
    std::vector<uint32_t> toRemove;

    for (auto& timer : m_timers) {
        timer.elapsed += deltaTime;

        if (timer.elapsed >= timer.delay) {
            if (timer.callback.IsValid()) {
                timer.callback.CallWithArgs({});
            }

            if (timer.repeat) {
                timer.elapsed -= timer.delay;
            } else {
                toRemove.push_back(timer.id);
            }
        }
    }

    for (uint32_t id : toRemove) {
        ClearTimer(id);
    }
}

// ============================================================================
// ScriptUtils Implementation
// ============================================================================

namespace ScriptUtils {

ScriptValueType GetType(const ScriptValue& value) {
    return std::visit([](auto&& arg) -> ScriptValueType {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return ScriptValueType::Nil;
        else if constexpr (std::is_same_v<T, bool>) return ScriptValueType::Boolean;
        else if constexpr (std::is_same_v<T, int64_t>) return ScriptValueType::Integer;
        else if constexpr (std::is_same_v<T, double>) return ScriptValueType::Number;
        else if constexpr (std::is_same_v<T, std::string>) return ScriptValueType::String;
        else if constexpr (std::is_same_v<T, glm::vec2>) return ScriptValueType::Vec2;
        else if constexpr (std::is_same_v<T, glm::vec3>) return ScriptValueType::Vec3;
        else if constexpr (std::is_same_v<T, glm::vec4>) return ScriptValueType::Vec4;
        else if constexpr (std::is_same_v<T, glm::quat>) return ScriptValueType::Quat;
        else if constexpr (std::is_same_v<T, Entity>) return ScriptValueType::Entity;
        else if constexpr (std::is_same_v<T, std::vector<ScriptValue>>) return ScriptValueType::Array;
        else if constexpr (std::is_same_v<T, std::unordered_map<std::string, ScriptValue>>) return ScriptValueType::Table;
        else return ScriptValueType::Nil;
    }, value);
}

std::string ToString(const ScriptValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) return "nil";
        else if constexpr (std::is_same_v<T, bool>) return arg ? "true" : "false";
        else if constexpr (std::is_same_v<T, int64_t>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, double>) return std::to_string(arg);
        else if constexpr (std::is_same_v<T, std::string>) return "\"" + arg + "\"";
        else if constexpr (std::is_same_v<T, glm::vec2>) {
            return "vec2(" + std::to_string(arg.x) + ", " + std::to_string(arg.y) + ")";
        }
        else if constexpr (std::is_same_v<T, glm::vec3>) {
            return "vec3(" + std::to_string(arg.x) + ", " + std::to_string(arg.y) +
                   ", " + std::to_string(arg.z) + ")";
        }
        else if constexpr (std::is_same_v<T, glm::vec4>) {
            return "vec4(" + std::to_string(arg.x) + ", " + std::to_string(arg.y) +
                   ", " + std::to_string(arg.z) + ", " + std::to_string(arg.w) + ")";
        }
        else if constexpr (std::is_same_v<T, glm::quat>) {
            return "quat(" + std::to_string(arg.x) + ", " + std::to_string(arg.y) +
                   ", " + std::to_string(arg.z) + ", " + std::to_string(arg.w) + ")";
        }
        else if constexpr (std::is_same_v<T, Entity>) return "entity(" + std::to_string(arg) + ")";
        else if constexpr (std::is_same_v<T, std::vector<ScriptValue>>) {
            return "[array: " + std::to_string(arg.size()) + " elements]";
        }
        else if constexpr (std::is_same_v<T, std::unordered_map<std::string, ScriptValue>>) {
            return "[table: " + std::to_string(arg.size()) + " entries]";
        }
        else return "unknown";
    }, value);
}

bool IsNil(const ScriptValue& value) {
    return std::holds_alternative<std::nullptr_t>(value);
}

ScriptValue MakeTable(std::initializer_list<std::pair<std::string, ScriptValue>> values) {
    std::unordered_map<std::string, ScriptValue> table;
    for (const auto& [key, val] : values) {
        table[key] = val;
    }
    return table;
}

ScriptValue MakeArray(const std::vector<ScriptValue>& values) {
    return values;
}

} // namespace ScriptUtils

} // namespace Cortex::Scripting
