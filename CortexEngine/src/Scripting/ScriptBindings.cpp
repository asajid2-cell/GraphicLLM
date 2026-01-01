// ScriptBindings.cpp
// Lua bindings implementation for engine systems.

#include "ScriptBindings.h"
#include "ScriptEngine.h"
#include "../Scene/Components.h"
#include <lua.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <iostream>

namespace Cortex::Scripting {

// ============================================================================
// Lua Helpers Implementation
// ============================================================================

glm::vec3 Lua_ToVec3(lua_State* L, int index) {
    glm::vec3 result(0.0f);
    if (lua_istable(L, index)) {
        lua_getfield(L, index, "x");
        result.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "y");
        result.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "z");
        result.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return result;
}

void Lua_PushVec3(lua_State* L, const glm::vec3& v) {
    lua_newtable(L);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
}

glm::vec4 Lua_ToVec4(lua_State* L, int index) {
    glm::vec4 result(0.0f);
    if (lua_istable(L, index)) {
        lua_getfield(L, index, "x");
        result.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "y");
        result.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "z");
        result.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "w");
        result.w = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return result;
}

void Lua_PushVec4(lua_State* L, const glm::vec4& v) {
    lua_newtable(L);
    lua_pushnumber(L, v.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, v.w); lua_setfield(L, -2, "w");
}

glm::quat Lua_ToQuat(lua_State* L, int index) {
    glm::quat result(1, 0, 0, 0);
    if (lua_istable(L, index)) {
        lua_getfield(L, index, "x");
        result.x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "y");
        result.y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "z");
        result.z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, index, "w");
        result.w = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    return result;
}

void Lua_PushQuat(lua_State* L, const glm::quat& q) {
    lua_newtable(L);
    lua_pushnumber(L, q.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, q.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, q.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, q.w); lua_setfield(L, -2, "w");
}

Entity Lua_ToEntity(lua_State* L, int index) {
    return static_cast<Entity>(lua_tointeger(L, index));
}

void Lua_PushEntity(lua_State* L, Entity entity) {
    lua_pushinteger(L, static_cast<lua_Integer>(entity));
}

// ============================================================================
// ScriptBindingsManager Implementation
// ============================================================================

const char* ScriptBindingsManager::ENGINE_REGISTRY_KEY = "CortexScriptEngine";

void ScriptBindingsManager::RegisterAll(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();
    SetEngine(L, engine);

    EntityBindings::Register(engine);
    TransformBindings::Register(engine);
    PhysicsBindings::Register(engine);
    RendererBindings::Register(engine);
    AudioBindings::Register(engine);
    ComponentBindings::Register(engine);
    ScriptComponentBindings::Register(engine);
    SceneBindings::Register(engine);
    DebugBindings::Register(engine);
}

ScriptEngine* ScriptBindingsManager::GetEngine(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, ENGINE_REGISTRY_KEY);
    ScriptEngine* engine = static_cast<ScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return engine;
}

void ScriptBindingsManager::SetEngine(lua_State* L, ScriptEngine* engine) {
    lua_pushlightuserdata(L, engine);
    lua_setfield(L, LUA_REGISTRYINDEX, ENGINE_REGISTRY_KEY);
}

// ============================================================================
// EntityBindings Implementation
// ============================================================================

void EntityBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    // Create Entity namespace
    lua_newtable(L);

    lua_pushcfunction(L, Lua_CreateEntity);
    lua_setfield(L, -2, "Create");

    lua_pushcfunction(L, Lua_DestroyEntity);
    lua_setfield(L, -2, "Destroy");

    lua_pushcfunction(L, Lua_IsValid);
    lua_setfield(L, -2, "IsValid");

    lua_pushcfunction(L, Lua_FindByName);
    lua_setfield(L, -2, "FindByName");

    lua_pushcfunction(L, Lua_FindByTag);
    lua_setfield(L, -2, "FindByTag");

    lua_pushcfunction(L, Lua_FindAllByTag);
    lua_setfield(L, -2, "FindAllByTag");

    lua_pushcfunction(L, Lua_GetChildren);
    lua_setfield(L, -2, "GetChildren");

    lua_pushcfunction(L, Lua_GetParent);
    lua_setfield(L, -2, "GetParent");

    lua_pushcfunction(L, Lua_SetParent);
    lua_setfield(L, -2, "SetParent");

    lua_pushcfunction(L, Lua_IsActive);
    lua_setfield(L, -2, "IsActive");

    lua_pushcfunction(L, Lua_SetActive);
    lua_setfield(L, -2, "SetActive");

    lua_pushcfunction(L, Lua_GetName);
    lua_setfield(L, -2, "GetName");

    lua_pushcfunction(L, Lua_SetName);
    lua_setfield(L, -2, "SetName");

    lua_pushcfunction(L, Lua_GetTag);
    lua_setfield(L, -2, "GetTag");

    lua_pushcfunction(L, Lua_SetTag);
    lua_setfield(L, -2, "SetTag");

    lua_setglobal(L, "Entity");
}

Entity EntityBindings::CreateEntity(ScriptEngine* engine, const std::string& name) {
    auto* reg = engine->GetRegistry();
    if (!reg) return 0;

    auto entity = reg->create();
    reg->emplace<NameComponent>(entity, name);
    reg->emplace<TransformComponent>(entity);
    return static_cast<Entity>(entity);
}

void EntityBindings::DestroyEntity(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    if (reg->valid(static_cast<entt::entity>(entity))) {
        reg->destroy(static_cast<entt::entity>(entity));
    }
}

bool EntityBindings::IsValid(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return false;

    return reg->valid(static_cast<entt::entity>(entity));
}

Entity EntityBindings::FindByName(ScriptEngine* engine, const std::string& name) {
    auto* reg = engine->GetRegistry();
    if (!reg) return 0;

    auto view = reg->view<NameComponent>();
    for (auto entity : view) {
        const auto& nameComp = view.get<NameComponent>(entity);
        if (nameComp.name == name) {
            return static_cast<Entity>(entity);
        }
    }
    return 0;
}

int EntityBindings::Lua_CreateEntity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* name = luaL_optstring(L, 1, "Entity");
    Entity entity = CreateEntity(engine, name);
    Lua_PushEntity(L, entity);
    return 1;
}

int EntityBindings::Lua_DestroyEntity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    DestroyEntity(engine, entity);
    return 0;
}

int EntityBindings::Lua_IsValid(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    lua_pushboolean(L, IsValid(engine, entity) ? 1 : 0);
    return 1;
}

int EntityBindings::Lua_FindByName(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* name = luaL_checkstring(L, 1);
    Entity entity = FindByName(engine, name);
    if (entity != 0) {
        Lua_PushEntity(L, entity);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int EntityBindings::Lua_FindByTag(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* tag = luaL_checkstring(L, 1);
    Entity entity = FindByTag(engine, tag);
    if (entity != 0) {
        Lua_PushEntity(L, entity);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

Entity EntityBindings::FindByTag(ScriptEngine* engine, const std::string& tag) {
    auto* reg = engine->GetRegistry();
    if (!reg) return 0;

    auto view = reg->view<TagComponent>();
    for (auto entity : view) {
        const auto& tagComp = view.get<TagComponent>(entity);
        if (tagComp.tag == tag) {
            return static_cast<Entity>(entity);
        }
    }
    return 0;
}

int EntityBindings::Lua_FindAllByTag(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* tag = luaL_checkstring(L, 1);
    std::vector<Entity> entities = FindAllByTag(engine, tag);

    lua_newtable(L);
    for (size_t i = 0; i < entities.size(); ++i) {
        Lua_PushEntity(L, entities[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

std::vector<Entity> EntityBindings::FindAllByTag(ScriptEngine* engine, const std::string& tag) {
    std::vector<Entity> result;
    auto* reg = engine->GetRegistry();
    if (!reg) return result;

    auto view = reg->view<TagComponent>();
    for (auto entity : view) {
        const auto& tagComp = view.get<TagComponent>(entity);
        if (tagComp.tag == tag) {
            result.push_back(static_cast<Entity>(entity));
        }
    }
    return result;
}

int EntityBindings::Lua_GetChildren(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity parent = Lua_ToEntity(L, 1);
    std::vector<Entity> children = GetChildren(engine, parent);

    lua_newtable(L);
    for (size_t i = 0; i < children.size(); ++i) {
        Lua_PushEntity(L, children[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

std::vector<Entity> EntityBindings::GetChildren(ScriptEngine* engine, Entity parent) {
    std::vector<Entity> result;
    auto* reg = engine->GetRegistry();
    if (!reg) return result;

    auto view = reg->view<TransformComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<TransformComponent>(entity);
        if (static_cast<Entity>(transform.parent) == parent) {
            result.push_back(static_cast<Entity>(entity));
        }
    }
    return result;
}

int EntityBindings::Lua_GetParent(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    Entity parent = GetParent(engine, entity);
    if (parent != 0) {
        Lua_PushEntity(L, parent);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

Entity EntityBindings::GetParent(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return 0;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return 0;

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        return static_cast<Entity>(transform->parent);
    }
    return 0;
}

int EntityBindings::Lua_SetParent(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    Entity parent = lua_isnil(L, 2) ? 0 : Lua_ToEntity(L, 2);
    SetParent(engine, entity, parent);
    return 0;
}

void EntityBindings::SetParent(ScriptEngine* engine, Entity entity, Entity parent) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        transform->parent = static_cast<entt::entity>(parent);
    }
}

int EntityBindings::Lua_IsActive(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    lua_pushboolean(L, IsActive(engine, entity) ? 1 : 0);
    return 1;
}

bool EntityBindings::IsActive(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return false;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return false;

    if (auto* active = reg->try_get<ActiveComponent>(e)) {
        return active->active;
    }
    return true;  // Default to active if no component
}

int EntityBindings::Lua_SetActive(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    bool active = lua_toboolean(L, 2) != 0;
    SetActive(engine, entity, active);
    return 0;
}

void EntityBindings::SetActive(ScriptEngine* engine, Entity entity, bool active) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* activeComp = reg->try_get<ActiveComponent>(e)) {
        activeComp->active = active;
    } else {
        reg->emplace<ActiveComponent>(e, active);
    }
}

int EntityBindings::Lua_GetName(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    std::string name = GetName(engine, entity);
    lua_pushstring(L, name.c_str());
    return 1;
}

std::string EntityBindings::GetName(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return "";

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return "";

    if (auto* name = reg->try_get<NameComponent>(e)) {
        return name->name;
    }
    return "";
}

int EntityBindings::Lua_SetName(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* name = luaL_checkstring(L, 2);
    SetName(engine, entity, name);
    return 0;
}

void EntityBindings::SetName(ScriptEngine* engine, Entity entity, const std::string& name) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* nameComp = reg->try_get<NameComponent>(e)) {
        nameComp->name = name;
    } else {
        reg->emplace<NameComponent>(e, name);
    }
}

int EntityBindings::Lua_GetTag(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    std::string tag = GetTag(engine, entity);
    lua_pushstring(L, tag.c_str());
    return 1;
}

std::string EntityBindings::GetTag(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return "";

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return "";

    if (auto* tag = reg->try_get<TagComponent>(e)) {
        return tag->tag;
    }
    return "";
}

int EntityBindings::Lua_SetTag(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* tag = luaL_checkstring(L, 2);
    SetTag(engine, entity, tag);
    return 0;
}

void EntityBindings::SetTag(ScriptEngine* engine, Entity entity, const std::string& tag) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* tagComp = reg->try_get<TagComponent>(e)) {
        tagComp->tag = tag;
    } else {
        reg->emplace<TagComponent>(e, tag);
    }
}

// ============================================================================
// TransformBindings Implementation
// ============================================================================

void TransformBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_GetPosition);
    lua_setfield(L, -2, "GetPosition");

    lua_pushcfunction(L, Lua_SetPosition);
    lua_setfield(L, -2, "SetPosition");

    lua_pushcfunction(L, Lua_GetLocalPosition);
    lua_setfield(L, -2, "GetLocalPosition");

    lua_pushcfunction(L, Lua_SetLocalPosition);
    lua_setfield(L, -2, "SetLocalPosition");

    lua_pushcfunction(L, Lua_GetRotation);
    lua_setfield(L, -2, "GetRotation");

    lua_pushcfunction(L, Lua_SetRotation);
    lua_setfield(L, -2, "SetRotation");

    lua_pushcfunction(L, Lua_GetEulerAngles);
    lua_setfield(L, -2, "GetEulerAngles");

    lua_pushcfunction(L, Lua_SetEulerAngles);
    lua_setfield(L, -2, "SetEulerAngles");

    lua_pushcfunction(L, Lua_GetScale);
    lua_setfield(L, -2, "GetScale");

    lua_pushcfunction(L, Lua_SetScale);
    lua_setfield(L, -2, "SetScale");

    lua_pushcfunction(L, Lua_GetForward);
    lua_setfield(L, -2, "GetForward");

    lua_pushcfunction(L, Lua_GetRight);
    lua_setfield(L, -2, "GetRight");

    lua_pushcfunction(L, Lua_GetUp);
    lua_setfield(L, -2, "GetUp");

    lua_pushcfunction(L, Lua_Translate);
    lua_setfield(L, -2, "Translate");

    lua_pushcfunction(L, Lua_Rotate);
    lua_setfield(L, -2, "Rotate");

    lua_pushcfunction(L, Lua_LookAt);
    lua_setfield(L, -2, "LookAt");

    lua_pushcfunction(L, Lua_TransformPoint);
    lua_setfield(L, -2, "TransformPoint");

    lua_pushcfunction(L, Lua_InverseTransformPoint);
    lua_setfield(L, -2, "InverseTransformPoint");

    lua_setglobal(L, "Transform");
}

glm::vec3 TransformBindings::GetPosition(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return glm::vec3(0);

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return glm::vec3(0);

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        return transform->position;
    }
    return glm::vec3(0);
}

void TransformBindings::SetPosition(ScriptEngine* engine, Entity entity, const glm::vec3& position) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        transform->position = position;
    }
}

int TransformBindings::Lua_GetPosition(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 pos = GetPosition(engine, entity);
    Lua_PushVec3(L, pos);
    return 1;
}

int TransformBindings::Lua_SetPosition(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 pos = Lua_ToVec3(L, 2);
    SetPosition(engine, entity, pos);
    return 0;
}

glm::vec3 TransformBindings::GetLocalPosition(ScriptEngine* engine, Entity entity) {
    // For now, same as world position (would need hierarchy traversal for local)
    return GetPosition(engine, entity);
}

void TransformBindings::SetLocalPosition(ScriptEngine* engine, Entity entity, const glm::vec3& position) {
    SetPosition(engine, entity, position);
}

int TransformBindings::Lua_GetLocalPosition(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 pos = GetLocalPosition(engine, entity);
    Lua_PushVec3(L, pos);
    return 1;
}

int TransformBindings::Lua_SetLocalPosition(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 pos = Lua_ToVec3(L, 2);
    SetLocalPosition(engine, entity, pos);
    return 0;
}

glm::quat TransformBindings::GetRotation(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return glm::quat(1, 0, 0, 0);

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return glm::quat(1, 0, 0, 0);

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        return transform->rotation;
    }
    return glm::quat(1, 0, 0, 0);
}

void TransformBindings::SetRotation(ScriptEngine* engine, Entity entity, const glm::quat& rotation) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        transform->rotation = rotation;
    }
}

int TransformBindings::Lua_GetRotation(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::quat rot = GetRotation(engine, entity);
    Lua_PushQuat(L, rot);
    return 1;
}

int TransformBindings::Lua_SetRotation(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::quat rot = Lua_ToQuat(L, 2);
    SetRotation(engine, entity, rot);
    return 0;
}

glm::vec3 TransformBindings::GetEulerAngles(ScriptEngine* engine, Entity entity) {
    glm::quat rot = GetRotation(engine, entity);
    return glm::degrees(glm::eulerAngles(rot));
}

void TransformBindings::SetEulerAngles(ScriptEngine* engine, Entity entity, const glm::vec3& euler) {
    glm::vec3 radians = glm::radians(euler);
    glm::quat rot = glm::quat(radians);
    SetRotation(engine, entity, rot);
}

int TransformBindings::Lua_GetEulerAngles(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 euler = GetEulerAngles(engine, entity);
    Lua_PushVec3(L, euler);
    return 1;
}

int TransformBindings::Lua_SetEulerAngles(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 euler = Lua_ToVec3(L, 2);
    SetEulerAngles(engine, entity, euler);
    return 0;
}

glm::vec3 TransformBindings::GetScale(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return glm::vec3(1);

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return glm::vec3(1);

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        return transform->scale;
    }
    return glm::vec3(1);
}

void TransformBindings::SetScale(ScriptEngine* engine, Entity entity, const glm::vec3& scale) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* transform = reg->try_get<TransformComponent>(e)) {
        transform->scale = scale;
    }
}

int TransformBindings::Lua_GetScale(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 scale = GetScale(engine, entity);
    Lua_PushVec3(L, scale);
    return 1;
}

int TransformBindings::Lua_SetScale(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 scale = Lua_ToVec3(L, 2);
    SetScale(engine, entity, scale);
    return 0;
}

glm::vec3 TransformBindings::GetForward(ScriptEngine* engine, Entity entity) {
    glm::quat rot = GetRotation(engine, entity);
    return rot * glm::vec3(0, 0, -1);
}

glm::vec3 TransformBindings::GetRight(ScriptEngine* engine, Entity entity) {
    glm::quat rot = GetRotation(engine, entity);
    return rot * glm::vec3(1, 0, 0);
}

glm::vec3 TransformBindings::GetUp(ScriptEngine* engine, Entity entity) {
    glm::quat rot = GetRotation(engine, entity);
    return rot * glm::vec3(0, 1, 0);
}

int TransformBindings::Lua_GetForward(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    Lua_PushVec3(L, GetForward(engine, entity));
    return 1;
}

int TransformBindings::Lua_GetRight(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    Lua_PushVec3(L, GetRight(engine, entity));
    return 1;
}

int TransformBindings::Lua_GetUp(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    Lua_PushVec3(L, GetUp(engine, entity));
    return 1;
}

void TransformBindings::Translate(ScriptEngine* engine, Entity entity, const glm::vec3& delta) {
    glm::vec3 pos = GetPosition(engine, entity);
    SetPosition(engine, entity, pos + delta);
}

void TransformBindings::Rotate(ScriptEngine* engine, Entity entity, const glm::vec3& eulerDelta) {
    glm::quat rot = GetRotation(engine, entity);
    glm::quat deltaRot = glm::quat(glm::radians(eulerDelta));
    SetRotation(engine, entity, rot * deltaRot);
}

int TransformBindings::Lua_Translate(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 delta = Lua_ToVec3(L, 2);
    Translate(engine, entity, delta);
    return 0;
}

int TransformBindings::Lua_Rotate(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 euler = Lua_ToVec3(L, 2);
    Rotate(engine, entity, euler);
    return 0;
}

void TransformBindings::LookAt(ScriptEngine* engine, Entity entity, const glm::vec3& target,
                                 const glm::vec3& up) {
    glm::vec3 pos = GetPosition(engine, entity);
    glm::vec3 direction = glm::normalize(target - pos);

    if (glm::length(direction) < 0.001f) return;

    glm::vec3 right = glm::normalize(glm::cross(up, -direction));
    glm::vec3 correctedUp = glm::cross(-direction, right);

    glm::mat3 rotMatrix(right, correctedUp, -direction);
    SetRotation(engine, entity, glm::quat_cast(rotMatrix));
}

int TransformBindings::Lua_LookAt(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 target = Lua_ToVec3(L, 2);
    glm::vec3 up = lua_istable(L, 3) ? Lua_ToVec3(L, 3) : glm::vec3(0, 1, 0);
    LookAt(engine, entity, target, up);
    return 0;
}

glm::vec3 TransformBindings::TransformPoint(ScriptEngine* engine, Entity entity, const glm::vec3& localPoint) {
    glm::vec3 pos = GetPosition(engine, entity);
    glm::quat rot = GetRotation(engine, entity);
    glm::vec3 scale = GetScale(engine, entity);
    return pos + rot * (localPoint * scale);
}

glm::vec3 TransformBindings::InverseTransformPoint(ScriptEngine* engine, Entity entity, const glm::vec3& worldPoint) {
    glm::vec3 pos = GetPosition(engine, entity);
    glm::quat rot = GetRotation(engine, entity);
    glm::vec3 scale = GetScale(engine, entity);
    return (glm::inverse(rot) * (worldPoint - pos)) / scale;
}

int TransformBindings::Lua_TransformPoint(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 local = Lua_ToVec3(L, 2);
    Lua_PushVec3(L, TransformPoint(engine, entity, local));
    return 1;
}

int TransformBindings::Lua_InverseTransformPoint(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 world = Lua_ToVec3(L, 2);
    Lua_PushVec3(L, InverseTransformPoint(engine, entity, world));
    return 1;
}

// ============================================================================
// PhysicsBindings Implementation
// ============================================================================

void PhysicsBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_GetVelocity);
    lua_setfield(L, -2, "GetVelocity");

    lua_pushcfunction(L, Lua_SetVelocity);
    lua_setfield(L, -2, "SetVelocity");

    lua_pushcfunction(L, Lua_GetAngularVelocity);
    lua_setfield(L, -2, "GetAngularVelocity");

    lua_pushcfunction(L, Lua_SetAngularVelocity);
    lua_setfield(L, -2, "SetAngularVelocity");

    lua_pushcfunction(L, Lua_AddForce);
    lua_setfield(L, -2, "AddForce");

    lua_pushcfunction(L, Lua_AddImpulse);
    lua_setfield(L, -2, "AddImpulse");

    lua_pushcfunction(L, Lua_AddTorque);
    lua_setfield(L, -2, "AddTorque");

    lua_pushcfunction(L, Lua_Raycast);
    lua_setfield(L, -2, "Raycast");

    lua_pushcfunction(L, Lua_RaycastAll);
    lua_setfield(L, -2, "RaycastAll");

    lua_pushcfunction(L, Lua_OverlapSphere);
    lua_setfield(L, -2, "OverlapSphere");

    lua_pushcfunction(L, Lua_OverlapBox);
    lua_setfield(L, -2, "OverlapBox");

    lua_setglobal(L, "Physics");
}

int PhysicsBindings::Lua_GetVelocity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 vel = GetVelocity(engine, entity);
    Lua_PushVec3(L, vel);
    return 1;
}

glm::vec3 PhysicsBindings::GetVelocity(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return glm::vec3(0);

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return glm::vec3(0);

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        return rb->velocity;
    }
    return glm::vec3(0);
}

int PhysicsBindings::Lua_SetVelocity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 vel = Lua_ToVec3(L, 2);
    SetVelocity(engine, entity, vel);
    return 0;
}

void PhysicsBindings::SetVelocity(ScriptEngine* engine, Entity entity, const glm::vec3& velocity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        rb->velocity = velocity;
    }
}

int PhysicsBindings::Lua_GetAngularVelocity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 angVel = GetAngularVelocity(engine, entity);
    Lua_PushVec3(L, angVel);
    return 1;
}

glm::vec3 PhysicsBindings::GetAngularVelocity(ScriptEngine* engine, Entity entity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return glm::vec3(0);

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return glm::vec3(0);

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        return rb->angularVelocity;
    }
    return glm::vec3(0);
}

int PhysicsBindings::Lua_SetAngularVelocity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 angVel = Lua_ToVec3(L, 2);
    SetAngularVelocity(engine, entity, angVel);
    return 0;
}

void PhysicsBindings::SetAngularVelocity(ScriptEngine* engine, Entity entity, const glm::vec3& angularVel) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        rb->angularVelocity = angularVel;
    }
}

int PhysicsBindings::Lua_AddForce(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 force = Lua_ToVec3(L, 2);
    AddForce(engine, entity, force);
    return 0;
}

void PhysicsBindings::AddForce(ScriptEngine* engine, Entity entity, const glm::vec3& force) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        rb->force += force;
    }
}

int PhysicsBindings::Lua_AddImpulse(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 impulse = Lua_ToVec3(L, 2);
    AddImpulse(engine, entity, impulse);
    return 0;
}

void PhysicsBindings::AddImpulse(ScriptEngine* engine, Entity entity, const glm::vec3& impulse) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        if (rb->mass > 0.0f) {
            rb->velocity += impulse / rb->mass;
        }
    }
}

int PhysicsBindings::Lua_AddTorque(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 torque = Lua_ToVec3(L, 2);
    AddTorque(engine, entity, torque);
    return 0;
}

void PhysicsBindings::AddTorque(ScriptEngine* engine, Entity entity, const glm::vec3& torque) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* rb = reg->try_get<RigidbodyComponent>(e)) {
        rb->torque += torque;
    }
}

int PhysicsBindings::Lua_Raycast(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    glm::vec3 origin = Lua_ToVec3(L, 1);
    glm::vec3 direction = Lua_ToVec3(L, 2);
    float maxDistance = static_cast<float>(luaL_optnumber(L, 3, 1000.0));

    RaycastHit hit = Raycast(engine, origin, direction, maxDistance);

    if (hit.hit) {
        lua_newtable(L);
        lua_pushboolean(L, 1); lua_setfield(L, -2, "hit");
        Lua_PushEntity(L, hit.entity); lua_setfield(L, -2, "entity");
        Lua_PushVec3(L, hit.point); lua_setfield(L, -2, "point");
        Lua_PushVec3(L, hit.normal); lua_setfield(L, -2, "normal");
        lua_pushnumber(L, hit.distance); lua_setfield(L, -2, "distance");
    } else {
        lua_pushnil(L);
    }
    return 1;
}

PhysicsBindings::RaycastHit PhysicsBindings::Raycast(ScriptEngine* engine, const glm::vec3& origin,
                                                      const glm::vec3& direction, float maxDistance) {
    RaycastHit result;
    // Placeholder - would integrate with actual physics system
    (void)engine; (void)origin; (void)direction; (void)maxDistance;
    return result;
}

int PhysicsBindings::Lua_RaycastAll(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    glm::vec3 origin = Lua_ToVec3(L, 1);
    glm::vec3 direction = Lua_ToVec3(L, 2);
    float maxDistance = static_cast<float>(luaL_optnumber(L, 3, 1000.0));

    std::vector<RaycastHit> hits = RaycastAll(engine, origin, direction, maxDistance);

    lua_newtable(L);
    for (size_t i = 0; i < hits.size(); ++i) {
        lua_newtable(L);
        lua_pushboolean(L, 1); lua_setfield(L, -2, "hit");
        Lua_PushEntity(L, hits[i].entity); lua_setfield(L, -2, "entity");
        Lua_PushVec3(L, hits[i].point); lua_setfield(L, -2, "point");
        Lua_PushVec3(L, hits[i].normal); lua_setfield(L, -2, "normal");
        lua_pushnumber(L, hits[i].distance); lua_setfield(L, -2, "distance");
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

std::vector<PhysicsBindings::RaycastHit> PhysicsBindings::RaycastAll(ScriptEngine* engine,
                                                                       const glm::vec3& origin,
                                                                       const glm::vec3& direction,
                                                                       float maxDistance) {
    std::vector<RaycastHit> results;
    // Placeholder - would integrate with actual physics system
    (void)engine; (void)origin; (void)direction; (void)maxDistance;
    return results;
}

int PhysicsBindings::Lua_OverlapSphere(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    glm::vec3 center = Lua_ToVec3(L, 1);
    float radius = static_cast<float>(luaL_checknumber(L, 2));

    std::vector<Entity> entities = OverlapSphere(engine, center, radius);

    lua_newtable(L);
    for (size_t i = 0; i < entities.size(); ++i) {
        Lua_PushEntity(L, entities[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

std::vector<Entity> PhysicsBindings::OverlapSphere(ScriptEngine* engine, const glm::vec3& center,
                                                     float radius) {
    std::vector<Entity> results;
    // Placeholder - would integrate with actual physics system
    (void)engine; (void)center; (void)radius;
    return results;
}

int PhysicsBindings::Lua_OverlapBox(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    glm::vec3 center = Lua_ToVec3(L, 1);
    glm::vec3 halfExtents = Lua_ToVec3(L, 2);

    std::vector<Entity> entities = OverlapBox(engine, center, halfExtents);

    lua_newtable(L);
    for (size_t i = 0; i < entities.size(); ++i) {
        Lua_PushEntity(L, entities[i]);
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

std::vector<Entity> PhysicsBindings::OverlapBox(ScriptEngine* engine, const glm::vec3& center,
                                                  const glm::vec3& halfExtents) {
    std::vector<Entity> results;
    // Placeholder - would integrate with actual physics system
    (void)engine; (void)center; (void)halfExtents;
    return results;
}

// ============================================================================
// RendererBindings Implementation
// ============================================================================

void RendererBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_SetMeshEnabled);
    lua_setfield(L, -2, "SetMeshEnabled");

    lua_pushcfunction(L, Lua_SetMesh);
    lua_setfield(L, -2, "SetMesh");

    lua_pushcfunction(L, Lua_SetMaterial);
    lua_setfield(L, -2, "SetMaterial");

    lua_pushcfunction(L, Lua_SetMaterialColor);
    lua_setfield(L, -2, "SetMaterialColor");

    lua_pushcfunction(L, Lua_SetMaterialFloat);
    lua_setfield(L, -2, "SetMaterialFloat");

    lua_pushcfunction(L, Lua_SetLightColor);
    lua_setfield(L, -2, "SetLightColor");

    lua_pushcfunction(L, Lua_SetLightIntensity);
    lua_setfield(L, -2, "SetLightIntensity");

    lua_pushcfunction(L, Lua_SetCameraFOV);
    lua_setfield(L, -2, "SetCameraFOV");

    lua_pushcfunction(L, Lua_ScreenToWorldPoint);
    lua_setfield(L, -2, "ScreenToWorldPoint");

    lua_pushcfunction(L, Lua_WorldToScreenPoint);
    lua_setfield(L, -2, "WorldToScreenPoint");

    lua_setglobal(L, "Renderer");
}

int RendererBindings::Lua_SetMeshEnabled(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    bool enabled = lua_toboolean(L, 2) != 0;
    SetMeshEnabled(engine, entity, enabled);
    return 0;
}

void RendererBindings::SetMeshEnabled(ScriptEngine* engine, Entity entity, bool enabled) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* mesh = reg->try_get<MeshComponent>(e)) {
        mesh->visible = enabled;
    }
}

int RendererBindings::Lua_SetMesh(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* meshPath = luaL_checkstring(L, 2);
    SetMesh(engine, entity, meshPath);
    return 0;
}

void RendererBindings::SetMesh(ScriptEngine* engine, Entity entity, const std::string& meshPath) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* mesh = reg->try_get<MeshComponent>(e)) {
        mesh->meshPath = meshPath;
        // Would trigger mesh reload
    }
}

int RendererBindings::Lua_SetMaterial(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    uint32_t slot = static_cast<uint32_t>(luaL_checkinteger(L, 2));
    const char* materialPath = luaL_checkstring(L, 3);
    SetMaterial(engine, entity, slot, materialPath);
    return 0;
}

void RendererBindings::SetMaterial(ScriptEngine* engine, Entity entity, uint32_t slot,
                                    const std::string& materialPath) {
    (void)engine; (void)entity; (void)slot; (void)materialPath;
    // Placeholder
}

int RendererBindings::Lua_SetMaterialColor(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* property = luaL_checkstring(L, 2);
    glm::vec4 color = Lua_ToVec4(L, 3);
    SetMaterialColor(engine, entity, property, color);
    return 0;
}

void RendererBindings::SetMaterialColor(ScriptEngine* engine, Entity entity,
                                          const std::string& property, const glm::vec4& color) {
    (void)engine; (void)entity; (void)property; (void)color;
    // Placeholder
}

int RendererBindings::Lua_SetMaterialFloat(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* property = luaL_checkstring(L, 2);
    float value = static_cast<float>(luaL_checknumber(L, 3));
    SetMaterialFloat(engine, entity, property, value);
    return 0;
}

void RendererBindings::SetMaterialFloat(ScriptEngine* engine, Entity entity,
                                          const std::string& property, float value) {
    (void)engine; (void)entity; (void)property; (void)value;
    // Placeholder
}

int RendererBindings::Lua_SetLightColor(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 color = Lua_ToVec3(L, 2);
    SetLightColor(engine, entity, color);
    return 0;
}

void RendererBindings::SetLightColor(ScriptEngine* engine, Entity entity, const glm::vec3& color) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* light = reg->try_get<LightComponent>(e)) {
        light->color = color;
    }
}

int RendererBindings::Lua_SetLightIntensity(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    float intensity = static_cast<float>(luaL_checknumber(L, 2));
    SetLightIntensity(engine, entity, intensity);
    return 0;
}

void RendererBindings::SetLightIntensity(ScriptEngine* engine, Entity entity, float intensity) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* light = reg->try_get<LightComponent>(e)) {
        light->intensity = intensity;
    }
}

int RendererBindings::Lua_SetCameraFOV(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    float fov = static_cast<float>(luaL_checknumber(L, 2));
    SetCameraFOV(engine, entity, fov);
    return 0;
}

void RendererBindings::SetCameraFOV(ScriptEngine* engine, Entity entity, float fov) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto e = static_cast<entt::entity>(entity);
    if (!reg->valid(e)) return;

    if (auto* cam = reg->try_get<CameraComponent>(e)) {
        cam->fov = fov;
    }
}

int RendererBindings::Lua_ScreenToWorldPoint(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 screenPoint = Lua_ToVec3(L, 2);
    glm::vec3 worldPoint = ScreenToWorldPoint(engine, entity, screenPoint);
    Lua_PushVec3(L, worldPoint);
    return 1;
}

glm::vec3 RendererBindings::ScreenToWorldPoint(ScriptEngine* engine, Entity entity,
                                                 const glm::vec3& screenPoint) {
    (void)engine; (void)entity; (void)screenPoint;
    // Placeholder - would need viewport/projection matrices
    return glm::vec3(0);
}

int RendererBindings::Lua_WorldToScreenPoint(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    glm::vec3 worldPoint = Lua_ToVec3(L, 2);
    glm::vec3 screenPoint = WorldToScreenPoint(engine, entity, worldPoint);
    Lua_PushVec3(L, screenPoint);
    return 1;
}

glm::vec3 RendererBindings::WorldToScreenPoint(ScriptEngine* engine, Entity entity,
                                                 const glm::vec3& worldPoint) {
    (void)engine; (void)entity; (void)worldPoint;
    // Placeholder
    return glm::vec3(0);
}

// ============================================================================
// AudioBindings Implementation
// ============================================================================

void AudioBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_PlaySound);
    lua_setfield(L, -2, "PlaySound");

    lua_pushcfunction(L, Lua_PlaySound3D);
    lua_setfield(L, -2, "PlaySound3D");

    lua_pushcfunction(L, Lua_StopSound);
    lua_setfield(L, -2, "StopSound");

    lua_pushcfunction(L, Lua_PlayMusic);
    lua_setfield(L, -2, "PlayMusic");

    lua_pushcfunction(L, Lua_StopMusic);
    lua_setfield(L, -2, "StopMusic");

    lua_pushcfunction(L, Lua_SetMasterVolume);
    lua_setfield(L, -2, "SetMasterVolume");

    lua_setglobal(L, "Audio");
}

int AudioBindings::Lua_PlaySound(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* soundPath = luaL_checkstring(L, 1);
    float volume = static_cast<float>(luaL_optnumber(L, 2, 1.0));
    float pitch = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    uint32_t handle = PlaySound(engine, soundPath, volume, pitch);
    lua_pushinteger(L, handle);
    return 1;
}

uint32_t AudioBindings::PlaySound(ScriptEngine* engine, const std::string& soundPath,
                                    float volume, float pitch) {
    (void)engine; (void)soundPath; (void)volume; (void)pitch;
    // Placeholder - would integrate with audio system
    return 0;
}

int AudioBindings::Lua_PlaySound3D(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* soundPath = luaL_checkstring(L, 1);
    glm::vec3 position = Lua_ToVec3(L, 2);
    float volume = static_cast<float>(luaL_optnumber(L, 3, 1.0));
    uint32_t handle = PlaySound3D(engine, soundPath, position, volume);
    lua_pushinteger(L, handle);
    return 1;
}

uint32_t AudioBindings::PlaySound3D(ScriptEngine* engine, const std::string& soundPath,
                                      const glm::vec3& position, float volume) {
    (void)engine; (void)soundPath; (void)position; (void)volume;
    // Placeholder
    return 0;
}

int AudioBindings::Lua_StopSound(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    uint32_t handle = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    StopSound(engine, handle);
    return 0;
}

void AudioBindings::StopSound(ScriptEngine* engine, uint32_t handle) {
    (void)engine; (void)handle;
    // Placeholder
}

int AudioBindings::Lua_PlayMusic(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* musicPath = luaL_checkstring(L, 1);
    float fadeIn = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    PlayMusic(engine, musicPath, fadeIn);
    return 0;
}

void AudioBindings::PlayMusic(ScriptEngine* engine, const std::string& musicPath, float fadeIn) {
    (void)engine; (void)musicPath; (void)fadeIn;
    // Placeholder
}

int AudioBindings::Lua_StopMusic(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    float fadeOut = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    StopMusic(engine, fadeOut);
    return 0;
}

void AudioBindings::StopMusic(ScriptEngine* engine, float fadeOut) {
    (void)engine; (void)fadeOut;
    // Placeholder
}

int AudioBindings::Lua_SetMasterVolume(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    float volume = static_cast<float>(luaL_checknumber(L, 1));
    SetMasterVolume(engine, volume);
    return 0;
}

void AudioBindings::SetMasterVolume(ScriptEngine* engine, float volume) {
    (void)engine; (void)volume;
    // Placeholder
}

// ============================================================================
// ComponentBindings Implementation
// ============================================================================

std::unordered_map<std::string, uint32_t> ComponentBindings::s_componentTypeIds;

void ComponentBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_HasComponent);
    lua_setfield(L, -2, "Has");

    lua_pushcfunction(L, Lua_GetComponent);
    lua_setfield(L, -2, "Get");

    lua_pushcfunction(L, Lua_AddComponent);
    lua_setfield(L, -2, "Add");

    lua_pushcfunction(L, Lua_RemoveComponent);
    lua_setfield(L, -2, "Remove");

    lua_setglobal(L, "Component");
}

void ComponentBindings::RegisterComponentType(const std::string& name, uint32_t typeId) {
    s_componentTypeIds[name] = typeId;
}

int ComponentBindings::Lua_HasComponent(lua_State* L) {
    (void)L;
    // Generic component check - would need type info
    lua_pushboolean(L, 0);
    return 1;
}

int ComponentBindings::Lua_GetComponent(lua_State* L) {
    (void)L;
    lua_pushnil(L);
    return 1;
}

int ComponentBindings::Lua_AddComponent(lua_State* L) {
    (void)L;
    lua_pushnil(L);
    return 1;
}

int ComponentBindings::Lua_RemoveComponent(lua_State* L) {
    (void)L;
    return 0;
}

// ============================================================================
// ScriptComponentBindings Implementation
// ============================================================================

void ScriptComponentBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_GetScript);
    lua_setfield(L, -2, "GetScript");

    lua_pushcfunction(L, Lua_AddScript);
    lua_setfield(L, -2, "AddScript");

    lua_pushcfunction(L, Lua_RemoveScript);
    lua_setfield(L, -2, "RemoveScript");

    lua_pushcfunction(L, Lua_SendMessage);
    lua_setfield(L, -2, "SendMessage");

    lua_pushcfunction(L, Lua_BroadcastMessage);
    lua_setfield(L, -2, "BroadcastMessage");

    lua_setglobal(L, "Script");
}

int ScriptComponentBindings::Lua_GetScript(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    ScriptInstance* instance = GetScript(engine, entity);
    if (instance && instance->GetTable().IsValid()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance->GetTable().GetRef());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

ScriptInstance* ScriptComponentBindings::GetScript(ScriptEngine* engine, Entity entity) {
    return engine->GetInstance(entity);
}

int ScriptComponentBindings::Lua_AddScript(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* scriptPath = luaL_checkstring(L, 2);
    ScriptInstance* instance = AddScript(engine, entity, scriptPath);
    if (instance && instance->GetTable().IsValid()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance->GetTable().GetRef());
    } else {
        lua_pushnil(L);
    }
    return 1;
}

ScriptInstance* ScriptComponentBindings::AddScript(ScriptEngine* engine, Entity entity,
                                                     const std::string& scriptPath) {
    return engine->CreateInstance(scriptPath, entity);
}

int ScriptComponentBindings::Lua_RemoveScript(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    RemoveScript(engine, entity);
    return 0;
}

void ScriptComponentBindings::RemoveScript(ScriptEngine* engine, Entity entity) {
    engine->DestroyInstance(entity);
}

int ScriptComponentBindings::Lua_SendMessage(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    const char* message = luaL_checkstring(L, 2);

    std::vector<ScriptValue> args;
    int numArgs = lua_gettop(L);
    for (int i = 3; i <= numArgs; ++i) {
        args.push_back(GetScriptValue(L, i));
    }

    SendMessage(engine, entity, message, args);
    return 0;
}

void ScriptComponentBindings::SendMessage(ScriptEngine* engine, Entity entity,
                                            const std::string& message,
                                            const std::vector<ScriptValue>& args) {
    ScriptInstance* instance = engine->GetInstance(entity);
    if (instance) {
        instance->SendMessage(message, args);
    }
}

int ScriptComponentBindings::Lua_BroadcastMessage(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* message = luaL_checkstring(L, 1);

    std::vector<ScriptValue> args;
    int numArgs = lua_gettop(L);
    for (int i = 2; i <= numArgs; ++i) {
        args.push_back(GetScriptValue(L, i));
    }

    BroadcastMessage(engine, message, args);
    return 0;
}

void ScriptComponentBindings::BroadcastMessage(ScriptEngine* engine, const std::string& message,
                                                 const std::vector<ScriptValue>& args) {
    auto* reg = engine->GetRegistry();
    if (!reg) return;

    auto view = reg->view<entt::entity>();
    for (auto entity : view) {
        ScriptInstance* instance = engine->GetInstance(static_cast<Entity>(entity));
        if (instance) {
            instance->SendMessage(message, args);
        }
    }
}

// ============================================================================
// SceneBindings Implementation
// ============================================================================

void SceneBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_LoadScene);
    lua_setfield(L, -2, "LoadScene");

    lua_pushcfunction(L, Lua_Instantiate);
    lua_setfield(L, -2, "Instantiate");

    lua_pushcfunction(L, Lua_Destroy);
    lua_setfield(L, -2, "Destroy");

    lua_pushcfunction(L, Lua_GetTime);
    lua_setfield(L, -2, "GetTime");

    lua_pushcfunction(L, Lua_GetDeltaTime);
    lua_setfield(L, -2, "GetDeltaTime");

    lua_pushcfunction(L, Lua_SetTimeScale);
    lua_setfield(L, -2, "SetTimeScale");

    lua_setglobal(L, "Scene");
}

int SceneBindings::Lua_LoadScene(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* scenePath = luaL_checkstring(L, 1);
    LoadScene(engine, scenePath);
    return 0;
}

void SceneBindings::LoadScene(ScriptEngine* engine, const std::string& scenePath) {
    (void)engine; (void)scenePath;
    // Placeholder - would integrate with scene system
}

int SceneBindings::Lua_Instantiate(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    const char* prefabPath = luaL_checkstring(L, 1);
    glm::vec3 position = lua_istable(L, 2) ? Lua_ToVec3(L, 2) : glm::vec3(0);
    glm::quat rotation = lua_istable(L, 3) ? Lua_ToQuat(L, 3) : glm::quat(1, 0, 0, 0);

    Entity entity = Instantiate(engine, prefabPath, position, rotation);
    Lua_PushEntity(L, entity);
    return 1;
}

Entity SceneBindings::Instantiate(ScriptEngine* engine, const std::string& prefabPath,
                                   const glm::vec3& position, const glm::quat& rotation) {
    (void)engine; (void)prefabPath; (void)position; (void)rotation;
    // Placeholder - would integrate with prefab system
    return 0;
}

int SceneBindings::Lua_Destroy(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    Entity entity = Lua_ToEntity(L, 1);
    float delay = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    Destroy(engine, entity, delay);
    return 0;
}

void SceneBindings::Destroy(ScriptEngine* engine, Entity entity, float delay) {
    (void)delay;  // Would queue for delayed destruction
    EntityBindings::DestroyEntity(engine, entity);
}

int SceneBindings::Lua_GetTime(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    lua_pushnumber(L, GetTime(engine));
    return 1;
}

float SceneBindings::GetTime(ScriptEngine* engine) {
    (void)engine;
    // Placeholder - would get from engine time
    return 0.0f;
}

int SceneBindings::Lua_GetDeltaTime(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    lua_pushnumber(L, GetDeltaTime(engine));
    return 1;
}

float SceneBindings::GetDeltaTime(ScriptEngine* engine) {
    (void)engine;
    // Placeholder
    return 1.0f / 60.0f;
}

int SceneBindings::Lua_SetTimeScale(lua_State* L) {
    ScriptEngine* engine = ScriptBindingsManager::GetEngine(L);
    float scale = static_cast<float>(luaL_checknumber(L, 1));
    SetTimeScale(engine, scale);
    return 0;
}

void SceneBindings::SetTimeScale(ScriptEngine* engine, float scale) {
    (void)engine; (void)scale;
    // Placeholder
}

// ============================================================================
// DebugBindings Implementation
// ============================================================================

void DebugBindings::Register(ScriptEngine* engine) {
    lua_State* L = engine->GetLuaState();

    lua_newtable(L);

    lua_pushcfunction(L, Lua_Log);
    lua_setfield(L, -2, "Log");

    lua_pushcfunction(L, Lua_LogWarning);
    lua_setfield(L, -2, "LogWarning");

    lua_pushcfunction(L, Lua_LogError);
    lua_setfield(L, -2, "LogError");

    lua_pushcfunction(L, Lua_DrawLine);
    lua_setfield(L, -2, "DrawLine");

    lua_pushcfunction(L, Lua_DrawRay);
    lua_setfield(L, -2, "DrawRay");

    lua_pushcfunction(L, Lua_DrawSphere);
    lua_setfield(L, -2, "DrawSphere");

    lua_pushcfunction(L, Lua_DrawBox);
    lua_setfield(L, -2, "DrawBox");

    lua_pushcfunction(L, Lua_DrawText);
    lua_setfield(L, -2, "DrawText");

    lua_setglobal(L, "Debug");

    // Also expose print as a shortcut to Debug.Log
    lua_pushcfunction(L, Lua_Log);
    lua_setglobal(L, "print");
}

int DebugBindings::Lua_Log(lua_State* L) {
    std::string message;
    int numArgs = lua_gettop(L);
    for (int i = 1; i <= numArgs; ++i) {
        if (i > 1) message += "\t";
        const char* str = luaL_tolstring(L, i, nullptr);
        message += str ? str : "nil";
        lua_pop(L, 1);
    }
    Log(message);
    return 0;
}

void DebugBindings::Log(const std::string& message) {
    std::cout << "[Script] " << message << std::endl;
}

int DebugBindings::Lua_LogWarning(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    LogWarning(message);
    return 0;
}

void DebugBindings::LogWarning(const std::string& message) {
    std::cout << "[Script Warning] " << message << std::endl;
}

int DebugBindings::Lua_LogError(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    LogError(message);
    return 0;
}

void DebugBindings::LogError(const std::string& message) {
    std::cerr << "[Script Error] " << message << std::endl;
}

int DebugBindings::Lua_DrawLine(lua_State* L) {
    glm::vec3 start = Lua_ToVec3(L, 1);
    glm::vec3 end = Lua_ToVec3(L, 2);
    glm::vec4 color = lua_istable(L, 3) ? Lua_ToVec4(L, 3) : glm::vec4(1, 1, 1, 1);
    float duration = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    DrawLine(start, end, color, duration);
    return 0;
}

void DebugBindings::DrawLine(const glm::vec3& start, const glm::vec3& end,
                               const glm::vec4& color, float duration) {
    (void)start; (void)end; (void)color; (void)duration;
    // Placeholder - would integrate with debug renderer
}

int DebugBindings::Lua_DrawRay(lua_State* L) {
    glm::vec3 origin = Lua_ToVec3(L, 1);
    glm::vec3 direction = Lua_ToVec3(L, 2);
    glm::vec4 color = lua_istable(L, 3) ? Lua_ToVec4(L, 3) : glm::vec4(1, 1, 1, 1);
    float duration = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    DrawRay(origin, direction, color, duration);
    return 0;
}

void DebugBindings::DrawRay(const glm::vec3& origin, const glm::vec3& direction,
                              const glm::vec4& color, float duration) {
    DrawLine(origin, origin + direction, color, duration);
}

int DebugBindings::Lua_DrawSphere(lua_State* L) {
    glm::vec3 center = Lua_ToVec3(L, 1);
    float radius = static_cast<float>(luaL_checknumber(L, 2));
    glm::vec4 color = lua_istable(L, 3) ? Lua_ToVec4(L, 3) : glm::vec4(1, 1, 1, 1);
    float duration = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    DrawSphere(center, radius, color, duration);
    return 0;
}

void DebugBindings::DrawSphere(const glm::vec3& center, float radius,
                                 const glm::vec4& color, float duration) {
    (void)center; (void)radius; (void)color; (void)duration;
    // Placeholder
}

int DebugBindings::Lua_DrawBox(lua_State* L) {
    glm::vec3 center = Lua_ToVec3(L, 1);
    glm::vec3 size = Lua_ToVec3(L, 2);
    glm::quat rotation = lua_istable(L, 3) ? Lua_ToQuat(L, 3) : glm::quat(1, 0, 0, 0);
    glm::vec4 color = lua_istable(L, 4) ? Lua_ToVec4(L, 4) : glm::vec4(1, 1, 1, 1);
    float duration = static_cast<float>(luaL_optnumber(L, 5, 0.0));
    DrawBox(center, size, rotation, color, duration);
    return 0;
}

void DebugBindings::DrawBox(const glm::vec3& center, const glm::vec3& size,
                              const glm::quat& rotation, const glm::vec4& color, float duration) {
    (void)center; (void)size; (void)rotation; (void)color; (void)duration;
    // Placeholder
}

int DebugBindings::Lua_DrawText(lua_State* L) {
    glm::vec3 position = Lua_ToVec3(L, 1);
    const char* text = luaL_checkstring(L, 2);
    glm::vec4 color = lua_istable(L, 3) ? Lua_ToVec4(L, 3) : glm::vec4(1, 1, 1, 1);
    float duration = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    DrawText(position, text, color, duration);
    return 0;
}

void DebugBindings::DrawText(const glm::vec3& position, const std::string& text,
                               const glm::vec4& color, float duration) {
    (void)position; (void)text; (void)color; (void)duration;
    // Placeholder
}

void DebugBindings::BeginProfile(const std::string& name) {
    (void)name;
    // Placeholder
}

void DebugBindings::EndProfile(const std::string& name) {
    (void)name;
    // Placeholder
}

} // namespace Cortex::Scripting
