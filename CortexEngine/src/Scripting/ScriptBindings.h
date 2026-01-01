// ScriptBindings.h
// Lua bindings for engine systems.
// Exposes entities, components, and engine APIs to scripts.

#pragma once

#include "ScriptEngine.h"
#include "../Scene/ECS_Registry.h"
#include <functional>
#include <memory>

// Forward declarations
struct lua_State;

namespace Cortex::Scripting {

// Forward declarations
class ScriptEngine;

// ============================================================================
// Entity Bindings
// ============================================================================

class EntityBindings {
public:
    static void Register(ScriptEngine* engine);

    // Entity creation/destruction
    static Entity CreateEntity(ScriptEngine* engine, const std::string& name);
    static void DestroyEntity(ScriptEngine* engine, Entity entity);
    static bool IsValid(ScriptEngine* engine, Entity entity);

    // Entity queries
    static Entity FindByName(ScriptEngine* engine, const std::string& name);
    static Entity FindByTag(ScriptEngine* engine, const std::string& tag);
    static std::vector<Entity> FindAllByTag(ScriptEngine* engine, const std::string& tag);
    static std::vector<Entity> GetAllEntities(ScriptEngine* engine);
    static std::vector<Entity> GetChildren(ScriptEngine* engine, Entity parent);
    static Entity GetParent(ScriptEngine* engine, Entity entity);

    // Entity relationships
    static void SetParent(ScriptEngine* engine, Entity entity, Entity parent);
    static void RemoveParent(ScriptEngine* engine, Entity entity);

    // Entity state
    static bool IsActive(ScriptEngine* engine, Entity entity);
    static void SetActive(ScriptEngine* engine, Entity entity, bool active);
    static std::string GetName(ScriptEngine* engine, Entity entity);
    static void SetName(ScriptEngine* engine, Entity entity, const std::string& name);
    static std::string GetTag(ScriptEngine* engine, Entity entity);
    static void SetTag(ScriptEngine* engine, Entity entity, const std::string& tag);

private:
    static int Lua_CreateEntity(lua_State* L);
    static int Lua_DestroyEntity(lua_State* L);
    static int Lua_IsValid(lua_State* L);
    static int Lua_FindByName(lua_State* L);
    static int Lua_FindByTag(lua_State* L);
    static int Lua_FindAllByTag(lua_State* L);
    static int Lua_GetChildren(lua_State* L);
    static int Lua_GetParent(lua_State* L);
    static int Lua_SetParent(lua_State* L);
    static int Lua_IsActive(lua_State* L);
    static int Lua_SetActive(lua_State* L);
    static int Lua_GetName(lua_State* L);
    static int Lua_SetName(lua_State* L);
    static int Lua_GetTag(lua_State* L);
    static int Lua_SetTag(lua_State* L);
};

// ============================================================================
// Transform Bindings
// ============================================================================

class TransformBindings {
public:
    static void Register(ScriptEngine* engine);

    // Position
    static glm::vec3 GetPosition(ScriptEngine* engine, Entity entity);
    static void SetPosition(ScriptEngine* engine, Entity entity, const glm::vec3& position);
    static glm::vec3 GetLocalPosition(ScriptEngine* engine, Entity entity);
    static void SetLocalPosition(ScriptEngine* engine, Entity entity, const glm::vec3& position);

    // Rotation
    static glm::quat GetRotation(ScriptEngine* engine, Entity entity);
    static void SetRotation(ScriptEngine* engine, Entity entity, const glm::quat& rotation);
    static glm::vec3 GetEulerAngles(ScriptEngine* engine, Entity entity);
    static void SetEulerAngles(ScriptEngine* engine, Entity entity, const glm::vec3& euler);

    // Scale
    static glm::vec3 GetScale(ScriptEngine* engine, Entity entity);
    static void SetScale(ScriptEngine* engine, Entity entity, const glm::vec3& scale);

    // Direction vectors
    static glm::vec3 GetForward(ScriptEngine* engine, Entity entity);
    static glm::vec3 GetRight(ScriptEngine* engine, Entity entity);
    static glm::vec3 GetUp(ScriptEngine* engine, Entity entity);

    // Transform operations
    static void Translate(ScriptEngine* engine, Entity entity, const glm::vec3& delta);
    static void Rotate(ScriptEngine* engine, Entity entity, const glm::vec3& eulerDelta);
    static void RotateAround(ScriptEngine* engine, Entity entity, const glm::vec3& point,
                              const glm::vec3& axis, float angle);
    static void LookAt(ScriptEngine* engine, Entity entity, const glm::vec3& target,
                        const glm::vec3& up = glm::vec3(0, 1, 0));

    // World/Local space conversion
    static glm::vec3 TransformPoint(ScriptEngine* engine, Entity entity, const glm::vec3& localPoint);
    static glm::vec3 InverseTransformPoint(ScriptEngine* engine, Entity entity, const glm::vec3& worldPoint);
    static glm::vec3 TransformDirection(ScriptEngine* engine, Entity entity, const glm::vec3& localDir);
    static glm::vec3 InverseTransformDirection(ScriptEngine* engine, Entity entity, const glm::vec3& worldDir);

private:
    static int Lua_GetPosition(lua_State* L);
    static int Lua_SetPosition(lua_State* L);
    static int Lua_GetLocalPosition(lua_State* L);
    static int Lua_SetLocalPosition(lua_State* L);
    static int Lua_GetRotation(lua_State* L);
    static int Lua_SetRotation(lua_State* L);
    static int Lua_GetEulerAngles(lua_State* L);
    static int Lua_SetEulerAngles(lua_State* L);
    static int Lua_GetScale(lua_State* L);
    static int Lua_SetScale(lua_State* L);
    static int Lua_GetForward(lua_State* L);
    static int Lua_GetRight(lua_State* L);
    static int Lua_GetUp(lua_State* L);
    static int Lua_Translate(lua_State* L);
    static int Lua_Rotate(lua_State* L);
    static int Lua_LookAt(lua_State* L);
    static int Lua_TransformPoint(lua_State* L);
    static int Lua_InverseTransformPoint(lua_State* L);
};

// ============================================================================
// Physics Bindings
// ============================================================================

class PhysicsBindings {
public:
    static void Register(ScriptEngine* engine);

    // Rigidbody
    static glm::vec3 GetVelocity(ScriptEngine* engine, Entity entity);
    static void SetVelocity(ScriptEngine* engine, Entity entity, const glm::vec3& velocity);
    static glm::vec3 GetAngularVelocity(ScriptEngine* engine, Entity entity);
    static void SetAngularVelocity(ScriptEngine* engine, Entity entity, const glm::vec3& angularVel);
    static float GetMass(ScriptEngine* engine, Entity entity);
    static void SetMass(ScriptEngine* engine, Entity entity, float mass);
    static bool IsKinematic(ScriptEngine* engine, Entity entity);
    static void SetKinematic(ScriptEngine* engine, Entity entity, bool kinematic);
    static bool UseGravity(ScriptEngine* engine, Entity entity);
    static void SetUseGravity(ScriptEngine* engine, Entity entity, bool useGravity);

    // Forces
    static void AddForce(ScriptEngine* engine, Entity entity, const glm::vec3& force);
    static void AddForceAtPosition(ScriptEngine* engine, Entity entity, const glm::vec3& force,
                                     const glm::vec3& position);
    static void AddImpulse(ScriptEngine* engine, Entity entity, const glm::vec3& impulse);
    static void AddTorque(ScriptEngine* engine, Entity entity, const glm::vec3& torque);

    // Raycasting
    struct RaycastHit {
        bool hit = false;
        Entity entity = 0;
        glm::vec3 point;
        glm::vec3 normal;
        float distance = 0.0f;
    };

    static RaycastHit Raycast(ScriptEngine* engine, const glm::vec3& origin,
                               const glm::vec3& direction, float maxDistance);
    static std::vector<RaycastHit> RaycastAll(ScriptEngine* engine, const glm::vec3& origin,
                                                const glm::vec3& direction, float maxDistance);

    // Overlap queries
    static std::vector<Entity> OverlapSphere(ScriptEngine* engine, const glm::vec3& center, float radius);
    static std::vector<Entity> OverlapBox(ScriptEngine* engine, const glm::vec3& center,
                                           const glm::vec3& halfExtents);

private:
    static int Lua_GetVelocity(lua_State* L);
    static int Lua_SetVelocity(lua_State* L);
    static int Lua_GetAngularVelocity(lua_State* L);
    static int Lua_SetAngularVelocity(lua_State* L);
    static int Lua_AddForce(lua_State* L);
    static int Lua_AddImpulse(lua_State* L);
    static int Lua_AddTorque(lua_State* L);
    static int Lua_Raycast(lua_State* L);
    static int Lua_RaycastAll(lua_State* L);
    static int Lua_OverlapSphere(lua_State* L);
    static int Lua_OverlapBox(lua_State* L);
};

// ============================================================================
// Renderer Bindings
// ============================================================================

class RendererBindings {
public:
    static void Register(ScriptEngine* engine);

    // Mesh renderer
    static bool HasMeshRenderer(ScriptEngine* engine, Entity entity);
    static void SetMeshEnabled(ScriptEngine* engine, Entity entity, bool enabled);
    static bool IsMeshEnabled(ScriptEngine* engine, Entity entity);
    static void SetMesh(ScriptEngine* engine, Entity entity, const std::string& meshPath);
    static void SetMaterial(ScriptEngine* engine, Entity entity, uint32_t slot, const std::string& materialPath);

    // Material properties
    static void SetMaterialColor(ScriptEngine* engine, Entity entity, const std::string& property,
                                   const glm::vec4& color);
    static void SetMaterialFloat(ScriptEngine* engine, Entity entity, const std::string& property,
                                   float value);
    static void SetMaterialTexture(ScriptEngine* engine, Entity entity, const std::string& property,
                                     const std::string& texturePath);

    // Light
    static bool HasLight(ScriptEngine* engine, Entity entity);
    static void SetLightColor(ScriptEngine* engine, Entity entity, const glm::vec3& color);
    static void SetLightIntensity(ScriptEngine* engine, Entity entity, float intensity);
    static void SetLightRange(ScriptEngine* engine, Entity entity, float range);
    static void SetLightEnabled(ScriptEngine* engine, Entity entity, bool enabled);

    // Camera
    static bool HasCamera(ScriptEngine* engine, Entity entity);
    static void SetCameraFOV(ScriptEngine* engine, Entity entity, float fov);
    static void SetCameraNearFar(ScriptEngine* engine, Entity entity, float near, float far);
    static void SetCameraEnabled(ScriptEngine* engine, Entity entity, bool enabled);
    static glm::vec3 ScreenToWorldPoint(ScriptEngine* engine, Entity entity, const glm::vec3& screenPoint);
    static glm::vec3 WorldToScreenPoint(ScriptEngine* engine, Entity entity, const glm::vec3& worldPoint);

private:
    static int Lua_SetMeshEnabled(lua_State* L);
    static int Lua_SetMesh(lua_State* L);
    static int Lua_SetMaterial(lua_State* L);
    static int Lua_SetMaterialColor(lua_State* L);
    static int Lua_SetMaterialFloat(lua_State* L);
    static int Lua_SetLightColor(lua_State* L);
    static int Lua_SetLightIntensity(lua_State* L);
    static int Lua_SetCameraFOV(lua_State* L);
    static int Lua_ScreenToWorldPoint(lua_State* L);
    static int Lua_WorldToScreenPoint(lua_State* L);
};

// ============================================================================
// Audio Bindings
// ============================================================================

class AudioBindings {
public:
    static void Register(ScriptEngine* engine);

    // Sound playback
    static uint32_t PlaySound(ScriptEngine* engine, const std::string& soundPath, float volume = 1.0f,
                               float pitch = 1.0f);
    static uint32_t PlaySound3D(ScriptEngine* engine, const std::string& soundPath,
                                  const glm::vec3& position, float volume = 1.0f);
    static void StopSound(ScriptEngine* engine, uint32_t handle);
    static void SetSoundVolume(ScriptEngine* engine, uint32_t handle, float volume);
    static void SetSoundPitch(ScriptEngine* engine, uint32_t handle, float pitch);
    static bool IsSoundPlaying(ScriptEngine* engine, uint32_t handle);

    // Audio source component
    static void PlayAudioSource(ScriptEngine* engine, Entity entity);
    static void StopAudioSource(ScriptEngine* engine, Entity entity);
    static void PauseAudioSource(ScriptEngine* engine, Entity entity);
    static void SetAudioSourceVolume(ScriptEngine* engine, Entity entity, float volume);
    static void SetAudioSourcePitch(ScriptEngine* engine, Entity entity, float pitch);
    static void SetAudioSourceLoop(ScriptEngine* engine, Entity entity, bool loop);

    // Music
    static void PlayMusic(ScriptEngine* engine, const std::string& musicPath, float fadeIn = 0.0f);
    static void StopMusic(ScriptEngine* engine, float fadeOut = 0.0f);
    static void SetMusicVolume(ScriptEngine* engine, float volume);

    // Global audio
    static void SetMasterVolume(ScriptEngine* engine, float volume);
    static void SetSFXVolume(ScriptEngine* engine, float volume);

private:
    static int Lua_PlaySound(lua_State* L);
    static int Lua_PlaySound3D(lua_State* L);
    static int Lua_StopSound(lua_State* L);
    static int Lua_PlayMusic(lua_State* L);
    static int Lua_StopMusic(lua_State* L);
    static int Lua_SetMasterVolume(lua_State* L);
};

// ============================================================================
// Component Bindings
// ============================================================================

class ComponentBindings {
public:
    static void Register(ScriptEngine* engine);

    // Component management
    template<typename T>
    static bool HasComponent(ScriptEngine* engine, Entity entity);

    template<typename T>
    static T* GetComponent(ScriptEngine* engine, Entity entity);

    template<typename T>
    static T* AddComponent(ScriptEngine* engine, Entity entity);

    template<typename T>
    static void RemoveComponent(ScriptEngine* engine, Entity entity);

    // Component type registration
    static void RegisterComponentType(const std::string& name, uint32_t typeId);

private:
    static std::unordered_map<std::string, uint32_t> s_componentTypeIds;

    static int Lua_HasComponent(lua_State* L);
    static int Lua_GetComponent(lua_State* L);
    static int Lua_AddComponent(lua_State* L);
    static int Lua_RemoveComponent(lua_State* L);
};

// ============================================================================
// Script Component Bindings
// ============================================================================

class ScriptComponentBindings {
public:
    static void Register(ScriptEngine* engine);

    // Script access
    static ScriptInstance* GetScript(ScriptEngine* engine, Entity entity);
    static ScriptInstance* AddScript(ScriptEngine* engine, Entity entity, const std::string& scriptPath);
    static void RemoveScript(ScriptEngine* engine, Entity entity);
    static bool HasScript(ScriptEngine* engine, Entity entity);

    // Script communication
    static void SendMessage(ScriptEngine* engine, Entity entity, const std::string& message,
                             const std::vector<ScriptValue>& args = {});
    static void BroadcastMessage(ScriptEngine* engine, const std::string& message,
                                   const std::vector<ScriptValue>& args = {});

private:
    static int Lua_GetScript(lua_State* L);
    static int Lua_AddScript(lua_State* L);
    static int Lua_RemoveScript(lua_State* L);
    static int Lua_SendMessage(lua_State* L);
    static int Lua_BroadcastMessage(lua_State* L);
};

// ============================================================================
// Scene Bindings
// ============================================================================

class SceneBindings {
public:
    static void Register(ScriptEngine* engine);

    // Scene management
    static void LoadScene(ScriptEngine* engine, const std::string& scenePath);
    static void LoadSceneAsync(ScriptEngine* engine, const std::string& scenePath);
    static void UnloadScene(ScriptEngine* engine, const std::string& scenePath);
    static std::string GetActiveScene(ScriptEngine* engine);

    // Instantiation
    static Entity Instantiate(ScriptEngine* engine, const std::string& prefabPath,
                               const glm::vec3& position = glm::vec3(0),
                               const glm::quat& rotation = glm::quat(1, 0, 0, 0));
    static Entity InstantiateAt(ScriptEngine* engine, const std::string& prefabPath,
                                  Entity parent);
    static void Destroy(ScriptEngine* engine, Entity entity, float delay = 0.0f);

    // Time
    static float GetTime(ScriptEngine* engine);
    static float GetDeltaTime(ScriptEngine* engine);
    static float GetFixedDeltaTime(ScriptEngine* engine);
    static float GetTimeScale(ScriptEngine* engine);
    static void SetTimeScale(ScriptEngine* engine, float scale);

private:
    static int Lua_LoadScene(lua_State* L);
    static int Lua_Instantiate(lua_State* L);
    static int Lua_Destroy(lua_State* L);
    static int Lua_GetTime(lua_State* L);
    static int Lua_GetDeltaTime(lua_State* L);
    static int Lua_SetTimeScale(lua_State* L);
};

// ============================================================================
// Debug Bindings
// ============================================================================

class DebugBindings {
public:
    static void Register(ScriptEngine* engine);

    // Logging
    static void Log(const std::string& message);
    static void LogWarning(const std::string& message);
    static void LogError(const std::string& message);

    // Debug drawing (temporary, cleared each frame)
    static void DrawLine(const glm::vec3& start, const glm::vec3& end,
                          const glm::vec4& color = glm::vec4(1, 1, 1, 1), float duration = 0.0f);
    static void DrawRay(const glm::vec3& origin, const glm::vec3& direction,
                         const glm::vec4& color = glm::vec4(1, 1, 1, 1), float duration = 0.0f);
    static void DrawSphere(const glm::vec3& center, float radius,
                            const glm::vec4& color = glm::vec4(1, 1, 1, 1), float duration = 0.0f);
    static void DrawBox(const glm::vec3& center, const glm::vec3& size,
                         const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                         const glm::vec4& color = glm::vec4(1, 1, 1, 1), float duration = 0.0f);
    static void DrawText(const glm::vec3& position, const std::string& text,
                          const glm::vec4& color = glm::vec4(1, 1, 1, 1), float duration = 0.0f);

    // Performance
    static void BeginProfile(const std::string& name);
    static void EndProfile(const std::string& name);

private:
    static int Lua_Log(lua_State* L);
    static int Lua_LogWarning(lua_State* L);
    static int Lua_LogError(lua_State* L);
    static int Lua_DrawLine(lua_State* L);
    static int Lua_DrawRay(lua_State* L);
    static int Lua_DrawSphere(lua_State* L);
    static int Lua_DrawBox(lua_State* L);
    static int Lua_DrawText(lua_State* L);
};

// ============================================================================
// All Bindings Registration
// ============================================================================

class ScriptBindingsManager {
public:
    // Register all standard bindings
    static void RegisterAll(ScriptEngine* engine);

    // Register custom bindings
    template<typename BindingsClass>
    static void RegisterCustom(ScriptEngine* engine) {
        BindingsClass::Register(engine);
    }

    // Get engine pointer from Lua state
    static ScriptEngine* GetEngine(lua_State* L);

    // Helper: Push ScriptEngine pointer to registry
    static void SetEngine(lua_State* L, ScriptEngine* engine);

private:
    static const char* ENGINE_REGISTRY_KEY;
};

// ============================================================================
// Lua Helper Macros
// ============================================================================

// Get vec3 from Lua stack (table with x,y,z)
glm::vec3 Lua_ToVec3(lua_State* L, int index);

// Push vec3 to Lua stack (as table with x,y,z)
void Lua_PushVec3(lua_State* L, const glm::vec3& v);

// Get vec4 from Lua stack
glm::vec4 Lua_ToVec4(lua_State* L, int index);

// Push vec4 to Lua stack
void Lua_PushVec4(lua_State* L, const glm::vec4& v);

// Get quat from Lua stack
glm::quat Lua_ToQuat(lua_State* L, int index);

// Push quat to Lua stack
void Lua_PushQuat(lua_State* L, const glm::quat& q);

// Get Entity from Lua stack
Entity Lua_ToEntity(lua_State* L, int index);

// Push Entity to Lua stack
void Lua_PushEntity(lua_State* L, Entity entity);

} // namespace Cortex::Scripting
