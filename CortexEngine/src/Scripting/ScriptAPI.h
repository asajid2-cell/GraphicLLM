// ScriptAPI.h
// Additional Lua API bindings for math, input, and utilities.
// Provides vector math, input queries, coroutines, and helper functions.

#pragma once

#include "ScriptEngine.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cmath>
#include <random>

// Forward declarations
struct lua_State;

namespace Cortex::Scripting {

// Forward declarations
class ScriptEngine;

// ============================================================================
// Math Library Bindings
// ============================================================================

class MathBindings {
public:
    static void Register(ScriptEngine* engine);

    // Constants
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TAU = PI * 2.0f;
    static constexpr float E = 2.71828182845904523536f;
    static constexpr float DEG2RAD = PI / 180.0f;
    static constexpr float RAD2DEG = 180.0f / PI;
    static constexpr float EPSILON = 1e-6f;
    static constexpr float INFINITY_F = std::numeric_limits<float>::infinity();

private:
    // Basic math
    static int Lua_Abs(lua_State* L);
    static int Lua_Sign(lua_State* L);
    static int Lua_Floor(lua_State* L);
    static int Lua_Ceil(lua_State* L);
    static int Lua_Round(lua_State* L);
    static int Lua_Clamp(lua_State* L);
    static int Lua_Clamp01(lua_State* L);
    static int Lua_Min(lua_State* L);
    static int Lua_Max(lua_State* L);
    static int Lua_Pow(lua_State* L);
    static int Lua_Sqrt(lua_State* L);
    static int Lua_Exp(lua_State* L);
    static int Lua_Log(lua_State* L);
    static int Lua_Log10(lua_State* L);

    // Trigonometry
    static int Lua_Sin(lua_State* L);
    static int Lua_Cos(lua_State* L);
    static int Lua_Tan(lua_State* L);
    static int Lua_Asin(lua_State* L);
    static int Lua_Acos(lua_State* L);
    static int Lua_Atan(lua_State* L);
    static int Lua_Atan2(lua_State* L);

    // Interpolation
    static int Lua_Lerp(lua_State* L);
    static int Lua_LerpUnclamped(lua_State* L);
    static int Lua_InverseLerp(lua_State* L);
    static int Lua_SmoothStep(lua_State* L);
    static int Lua_SmoothDamp(lua_State* L);
    static int Lua_MoveTowards(lua_State* L);
    static int Lua_MoveTowardsAngle(lua_State* L);

    // Angle utilities
    static int Lua_DeltaAngle(lua_State* L);
    static int Lua_LerpAngle(lua_State* L);
    static int Lua_Repeat(lua_State* L);
    static int Lua_PingPong(lua_State* L);

    // Random
    static int Lua_Random(lua_State* L);
    static int Lua_RandomRange(lua_State* L);
    static int Lua_RandomInt(lua_State* L);
    static int Lua_RandomSeed(lua_State* L);
    static int Lua_RandomInsideUnitCircle(lua_State* L);
    static int Lua_RandomInsideUnitSphere(lua_State* L);
    static int Lua_RandomOnUnitSphere(lua_State* L);
    static int Lua_RandomRotation(lua_State* L);

    // Noise
    static int Lua_PerlinNoise(lua_State* L);
    static int Lua_SimplexNoise(lua_State* L);

    // Comparison
    static int Lua_Approximately(lua_State* L);
    static int Lua_IsPowerOfTwo(lua_State* L);
    static int Lua_NextPowerOfTwo(lua_State* L);
    static int Lua_ClosestPowerOfTwo(lua_State* L);

    static std::mt19937 s_rng;
};

// ============================================================================
// Vec2 Bindings
// ============================================================================

class Vec2Bindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Constructors
    static int Lua_New(lua_State* L);
    static int Lua_Zero(lua_State* L);
    static int Lua_One(lua_State* L);
    static int Lua_Up(lua_State* L);
    static int Lua_Down(lua_State* L);
    static int Lua_Left(lua_State* L);
    static int Lua_Right(lua_State* L);

    // Operations
    static int Lua_Add(lua_State* L);
    static int Lua_Sub(lua_State* L);
    static int Lua_Mul(lua_State* L);
    static int Lua_Div(lua_State* L);
    static int Lua_Negate(lua_State* L);

    // Vector math
    static int Lua_Dot(lua_State* L);
    static int Lua_Cross(lua_State* L);  // Returns scalar (2D cross product)
    static int Lua_Length(lua_State* L);
    static int Lua_LengthSq(lua_State* L);
    static int Lua_Distance(lua_State* L);
    static int Lua_DistanceSq(lua_State* L);
    static int Lua_Normalize(lua_State* L);
    static int Lua_Normalized(lua_State* L);

    // Utilities
    static int Lua_Lerp(lua_State* L);
    static int Lua_LerpUnclamped(lua_State* L);
    static int Lua_MoveTowards(lua_State* L);
    static int Lua_Reflect(lua_State* L);
    static int Lua_Perpendicular(lua_State* L);
    static int Lua_Angle(lua_State* L);
    static int Lua_SignedAngle(lua_State* L);
    static int Lua_ClampMagnitude(lua_State* L);
    static int Lua_Min(lua_State* L);
    static int Lua_Max(lua_State* L);
    static int Lua_Scale(lua_State* L);

    // Conversion
    static int Lua_ToVec3(lua_State* L);
    static int Lua_ToString(lua_State* L);
};

// ============================================================================
// Vec3 Bindings
// ============================================================================

class Vec3Bindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Constructors
    static int Lua_New(lua_State* L);
    static int Lua_Zero(lua_State* L);
    static int Lua_One(lua_State* L);
    static int Lua_Up(lua_State* L);
    static int Lua_Down(lua_State* L);
    static int Lua_Left(lua_State* L);
    static int Lua_Right(lua_State* L);
    static int Lua_Forward(lua_State* L);
    static int Lua_Back(lua_State* L);

    // Operations
    static int Lua_Add(lua_State* L);
    static int Lua_Sub(lua_State* L);
    static int Lua_Mul(lua_State* L);
    static int Lua_Div(lua_State* L);
    static int Lua_Negate(lua_State* L);

    // Vector math
    static int Lua_Dot(lua_State* L);
    static int Lua_Cross(lua_State* L);
    static int Lua_Length(lua_State* L);
    static int Lua_LengthSq(lua_State* L);
    static int Lua_Distance(lua_State* L);
    static int Lua_DistanceSq(lua_State* L);
    static int Lua_Normalize(lua_State* L);
    static int Lua_Normalized(lua_State* L);

    // Utilities
    static int Lua_Lerp(lua_State* L);
    static int Lua_LerpUnclamped(lua_State* L);
    static int Lua_Slerp(lua_State* L);
    static int Lua_MoveTowards(lua_State* L);
    static int Lua_RotateTowards(lua_State* L);
    static int Lua_Reflect(lua_State* L);
    static int Lua_Project(lua_State* L);
    static int Lua_ProjectOnPlane(lua_State* L);
    static int Lua_Angle(lua_State* L);
    static int Lua_SignedAngle(lua_State* L);
    static int Lua_ClampMagnitude(lua_State* L);
    static int Lua_Min(lua_State* L);
    static int Lua_Max(lua_State* L);
    static int Lua_Scale(lua_State* L);
    static int Lua_OrthoNormalize(lua_State* L);

    // Conversion
    static int Lua_ToVec2(lua_State* L);
    static int Lua_ToVec4(lua_State* L);
    static int Lua_ToString(lua_State* L);
};

// ============================================================================
// Vec4 Bindings
// ============================================================================

class Vec4Bindings {
public:
    static void Register(ScriptEngine* engine);

private:
    static int Lua_New(lua_State* L);
    static int Lua_Zero(lua_State* L);
    static int Lua_One(lua_State* L);
    static int Lua_Add(lua_State* L);
    static int Lua_Sub(lua_State* L);
    static int Lua_Mul(lua_State* L);
    static int Lua_Div(lua_State* L);
    static int Lua_Dot(lua_State* L);
    static int Lua_Length(lua_State* L);
    static int Lua_Normalize(lua_State* L);
    static int Lua_Lerp(lua_State* L);
    static int Lua_ToVec3(lua_State* L);
    static int Lua_ToString(lua_State* L);
};

// ============================================================================
// Quaternion Bindings
// ============================================================================

class QuatBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Constructors
    static int Lua_New(lua_State* L);
    static int Lua_Identity(lua_State* L);
    static int Lua_Euler(lua_State* L);
    static int Lua_AngleAxis(lua_State* L);
    static int Lua_FromToRotation(lua_State* L);
    static int Lua_LookRotation(lua_State* L);

    // Operations
    static int Lua_Mul(lua_State* L);
    static int Lua_MulVec3(lua_State* L);
    static int Lua_Inverse(lua_State* L);
    static int Lua_Conjugate(lua_State* L);

    // Utilities
    static int Lua_Dot(lua_State* L);
    static int Lua_Angle(lua_State* L);
    static int Lua_Normalize(lua_State* L);
    static int Lua_Lerp(lua_State* L);
    static int Lua_Slerp(lua_State* L);
    static int Lua_SlerpUnclamped(lua_State* L);
    static int Lua_RotateTowards(lua_State* L);

    // Conversion
    static int Lua_ToEuler(lua_State* L);
    static int Lua_ToAngleAxis(lua_State* L);
    static int Lua_ToMatrix(lua_State* L);
    static int Lua_ToString(lua_State* L);

    // Direction vectors
    static int Lua_GetForward(lua_State* L);
    static int Lua_GetRight(lua_State* L);
    static int Lua_GetUp(lua_State* L);
};

// ============================================================================
// Color Bindings
// ============================================================================

class ColorBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Constructors
    static int Lua_New(lua_State* L);
    static int Lua_RGB(lua_State* L);
    static int Lua_RGBA(lua_State* L);
    static int Lua_HSV(lua_State* L);
    static int Lua_Hex(lua_State* L);

    // Preset colors
    static int Lua_White(lua_State* L);
    static int Lua_Black(lua_State* L);
    static int Lua_Red(lua_State* L);
    static int Lua_Green(lua_State* L);
    static int Lua_Blue(lua_State* L);
    static int Lua_Yellow(lua_State* L);
    static int Lua_Cyan(lua_State* L);
    static int Lua_Magenta(lua_State* L);
    static int Lua_Gray(lua_State* L);
    static int Lua_Clear(lua_State* L);

    // Operations
    static int Lua_Add(lua_State* L);
    static int Lua_Sub(lua_State* L);
    static int Lua_Mul(lua_State* L);
    static int Lua_Lerp(lua_State* L);
    static int Lua_LerpUnclamped(lua_State* L);

    // Conversion
    static int Lua_ToHSV(lua_State* L);
    static int Lua_ToHex(lua_State* L);
    static int Lua_ToLinear(lua_State* L);
    static int Lua_ToGamma(lua_State* L);
    static int Lua_Grayscale(lua_State* L);
    static int Lua_ToString(lua_State* L);
};

// ============================================================================
// Input Bindings
// ============================================================================

class InputBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Key state
    static int Lua_GetKey(lua_State* L);
    static int Lua_GetKeyDown(lua_State* L);
    static int Lua_GetKeyUp(lua_State* L);

    // Mouse
    static int Lua_GetMouseButton(lua_State* L);
    static int Lua_GetMouseButtonDown(lua_State* L);
    static int Lua_GetMouseButtonUp(lua_State* L);
    static int Lua_GetMousePosition(lua_State* L);
    static int Lua_GetMouseDelta(lua_State* L);
    static int Lua_GetMouseScrollDelta(lua_State* L);

    // Axes
    static int Lua_GetAxis(lua_State* L);
    static int Lua_GetAxisRaw(lua_State* L);

    // Actions (from input manager)
    static int Lua_GetAction(lua_State* L);
    static int Lua_GetActionDown(lua_State* L);
    static int Lua_GetActionUp(lua_State* L);
    static int Lua_GetActionValue(lua_State* L);

    // Gamepad
    static int Lua_IsGamepadConnected(lua_State* L);
    static int Lua_GetGamepadButton(lua_State* L);
    static int Lua_GetGamepadAxis(lua_State* L);
    static int Lua_SetGamepadVibration(lua_State* L);

    // Touch (for future mobile support)
    static int Lua_GetTouchCount(lua_State* L);
    static int Lua_GetTouch(lua_State* L);

    // Cursor
    static int Lua_SetCursorVisible(lua_State* L);
    static int Lua_SetCursorLocked(lua_State* L);
    static int Lua_IsCursorVisible(lua_State* L);
    static int Lua_IsCursorLocked(lua_State* L);
};

// ============================================================================
// Time Bindings
// ============================================================================

class TimeBindings {
public:
    static void Register(ScriptEngine* engine);

    // Update time values (called by engine each frame)
    static void Update(float deltaTime, float unscaledDeltaTime, float time,
                        float unscaledTime, float fixedDeltaTime);

private:
    static int Lua_GetTime(lua_State* L);
    static int Lua_GetDeltaTime(lua_State* L);
    static int Lua_GetUnscaledTime(lua_State* L);
    static int Lua_GetUnscaledDeltaTime(lua_State* L);
    static int Lua_GetFixedDeltaTime(lua_State* L);
    static int Lua_GetTimeScale(lua_State* L);
    static int Lua_SetTimeScale(lua_State* L);
    static int Lua_GetFrameCount(lua_State* L);
    static int Lua_GetRealtimeSinceStartup(lua_State* L);

    static float s_time;
    static float s_deltaTime;
    static float s_unscaledTime;
    static float s_unscaledDeltaTime;
    static float s_fixedDeltaTime;
    static float s_timeScale;
    static uint64_t s_frameCount;
};

// ============================================================================
// Coroutine Utilities
// ============================================================================

class CoroutineBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Yield functions (to be called from coroutines)
    static int Lua_WaitForSeconds(lua_State* L);
    static int Lua_WaitForSecondsRealtime(lua_State* L);
    static int Lua_WaitForEndOfFrame(lua_State* L);
    static int Lua_WaitForFixedUpdate(lua_State* L);
    static int Lua_WaitUntil(lua_State* L);
    static int Lua_WaitWhile(lua_State* L);

    // Coroutine management
    static int Lua_StartCoroutine(lua_State* L);
    static int Lua_StopCoroutine(lua_State* L);
    static int Lua_StopAllCoroutines(lua_State* L);
};

// ============================================================================
// Utility Bindings
// ============================================================================

class UtilityBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Type checking
    static int Lua_TypeOf(lua_State* L);
    static int Lua_IsNumber(lua_State* L);
    static int Lua_IsString(lua_State* L);
    static int Lua_IsTable(lua_State* L);
    static int Lua_IsFunction(lua_State* L);
    static int Lua_IsEntity(lua_State* L);
    static int Lua_IsVec2(lua_State* L);
    static int Lua_IsVec3(lua_State* L);
    static int Lua_IsQuat(lua_State* L);

    // Table utilities
    static int Lua_TableCopy(lua_State* L);
    static int Lua_TableDeepCopy(lua_State* L);
    static int Lua_TableMerge(lua_State* L);
    static int Lua_TableKeys(lua_State* L);
    static int Lua_TableValues(lua_State* L);
    static int Lua_TableContains(lua_State* L);
    static int Lua_TableFind(lua_State* L);
    static int Lua_TableFilter(lua_State* L);
    static int Lua_TableMap(lua_State* L);
    static int Lua_TableReduce(lua_State* L);
    static int Lua_TableSort(lua_State* L);
    static int Lua_TableShuffle(lua_State* L);

    // String utilities
    static int Lua_StringSplit(lua_State* L);
    static int Lua_StringJoin(lua_State* L);
    static int Lua_StringTrim(lua_State* L);
    static int Lua_StringStartsWith(lua_State* L);
    static int Lua_StringEndsWith(lua_State* L);
    static int Lua_StringContains(lua_State* L);
    static int Lua_StringReplace(lua_State* L);
    static int Lua_StringFormat(lua_State* L);

    // JSON
    static int Lua_JsonEncode(lua_State* L);
    static int Lua_JsonDecode(lua_State* L);

    // UUID
    static int Lua_GenerateUUID(lua_State* L);
};

// ============================================================================
// PlayerPrefs Bindings (Simple Key-Value Storage)
// ============================================================================

class PlayerPrefsBindings {
public:
    static void Register(ScriptEngine* engine);

    // Set storage path
    static void SetStoragePath(const std::string& path);

private:
    static int Lua_GetInt(lua_State* L);
    static int Lua_SetInt(lua_State* L);
    static int Lua_GetFloat(lua_State* L);
    static int Lua_SetFloat(lua_State* L);
    static int Lua_GetString(lua_State* L);
    static int Lua_SetString(lua_State* L);
    static int Lua_HasKey(lua_State* L);
    static int Lua_DeleteKey(lua_State* L);
    static int Lua_DeleteAll(lua_State* L);
    static int Lua_Save(lua_State* L);

    static std::string s_storagePath;
    static std::unordered_map<std::string, std::string> s_prefs;
    static bool s_dirty;
};

// ============================================================================
// Resources Bindings
// ============================================================================

class ResourcesBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    static int Lua_Load(lua_State* L);
    static int Lua_LoadAsync(lua_State* L);
    static int Lua_Unload(lua_State* L);
    static int Lua_UnloadUnused(lua_State* L);
    static int Lua_FindAllOfType(lua_State* L);
};

// ============================================================================
// Application Bindings
// ============================================================================

class ApplicationBindings {
public:
    static void Register(ScriptEngine* engine);

private:
    // Application info
    static int Lua_GetVersion(lua_State* L);
    static int Lua_GetPlatform(lua_State* L);
    static int Lua_GetDataPath(lua_State* L);
    static int Lua_GetPersistentDataPath(lua_State* L);
    static int Lua_GetStreamingAssetsPath(lua_State* L);

    // Screen
    static int Lua_GetScreenWidth(lua_State* L);
    static int Lua_GetScreenHeight(lua_State* L);
    static int Lua_GetTargetFrameRate(lua_State* L);
    static int Lua_SetTargetFrameRate(lua_State* L);
    static int Lua_IsFullscreen(lua_State* L);
    static int Lua_SetFullscreen(lua_State* L);

    // State
    static int Lua_IsPlaying(lua_State* L);
    static int Lua_IsFocused(lua_State* L);
    static int Lua_Quit(lua_State* L);
    static int Lua_OpenURL(lua_State* L);
};

// ============================================================================
// All API Registration
// ============================================================================

class ScriptAPIManager {
public:
    // Register all standard API bindings
    static void RegisterAll(ScriptEngine* engine);

    // Update per-frame values (call from engine)
    static void Update(float deltaTime, float unscaledDeltaTime, float time,
                        float unscaledTime, float fixedDeltaTime);
};

// ============================================================================
// Inline Helper Functions
// ============================================================================

namespace ScriptMath {

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float LerpUnclamped(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float InverseLerp(float a, float b, float value) {
    if (std::abs(b - a) < 1e-6f) return 0.0f;
    return (value - a) / (b - a);
}

inline float SmoothStep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float MoveTowards(float current, float target, float maxDelta) {
    float diff = target - current;
    if (std::abs(diff) <= maxDelta) return target;
    return current + std::copysign(maxDelta, diff);
}

inline float DeltaAngle(float current, float target) {
    float delta = std::fmod(target - current, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    return delta;
}

inline float Repeat(float t, float length) {
    return std::clamp(t - std::floor(t / length) * length, 0.0f, length);
}

inline float PingPong(float t, float length) {
    t = Repeat(t, length * 2.0f);
    return length - std::abs(t - length);
}

inline bool Approximately(float a, float b, float epsilon = 1e-6f) {
    return std::abs(a - b) < epsilon;
}

inline int NextPowerOfTwo(int value) {
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

inline bool IsPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

// Simple Perlin noise implementation
float PerlinNoise2D(float x, float y);
float PerlinNoise3D(float x, float y, float z);

// Simple Simplex noise
float SimplexNoise2D(float x, float y);
float SimplexNoise3D(float x, float y, float z);

} // namespace ScriptMath

} // namespace Cortex::Scripting
