// test_phase2_engine_editor_mode.cpp
// Self-contained unit tests for Phase 2: EngineEditorMode implementation
// Compile: cl /EHsc /std:c++20 test_phase2_engine_editor_mode.cpp /Fe:test_phase2.exe
// Run: test_phase2.exe

#include <iostream>
#include <string>
#include <cmath>
#include <cassert>

// ============================================================================
// Test Framework (minimal)
// ============================================================================

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FUNCTION__ << ": " << message << std::endl; \
            g_testsFailed++; \
            return false; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        std::cout << "[PASS] " << __FUNCTION__ << std::endl; \
        g_testsPassed++; \
        return true; \
    } while(0)

#define TEST_FLOAT_EQ(a, b, eps) (std::abs((a) - (b)) < (eps))

// ============================================================================
// Extracted Logic to Test (copied from EngineEditorMode for isolated testing)
// ============================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Simple vec3 for testing
struct Vec3 {
    float x, y, z;
    Vec3(float x_ = 0, float y_ = 0, float z_ = 0) : x(x_), y(y_), z(z_) {}
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 normalized() const {
        float len = length();
        if (len > 0.0001f) return Vec3(x/len, y/len, z/len);
        return *this;
    }
};

// EditorState struct from EngineEditorMode.h
struct EditorState {
    bool showGrid = true;
    bool showGizmos = true;
    bool wireframeMode = false;
    float timeOfDay = 10.0f;
    float timeScale = 60.0f;
    bool timePaused = true;
    bool proceduralSky = true;
    bool shadows = true;
    bool ssao = false;
    bool showStats = true;
    bool showChunkBounds = false;
};

// Sun direction calculation (from EngineEditorMode.cpp)
Vec3 CalculateSunDirection(float timeOfDay) {
    float hourAngle = (timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);
    float sunY = std::cos(hourAngle);
    float sunX = std::sin(hourAngle);
    float sunZ = 0.3f;
    return Vec3(sunX, std::max(sunY, -0.2f), sunZ).normalized();
}

// Sun color calculation (from EngineEditorMode.cpp)
Vec3 CalculateSunColor(float timeOfDay) {
    float hourAngle = (timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);
    float sunAltitude = std::cos(hourAngle);

    if (sunAltitude > 0.5f) {
        return Vec3(1.0f, 0.98f, 0.95f);
    } else if (sunAltitude > 0.0f) {
        float t = sunAltitude / 0.5f;
        Vec3 noon(1.0f, 0.98f, 0.95f);
        Vec3 sunset(1.0f, 0.6f, 0.3f);
        return Vec3(
            sunset.x + t * (noon.x - sunset.x),
            sunset.y + t * (noon.y - sunset.y),
            sunset.z + t * (noon.z - sunset.z)
        );
    } else {
        float t = std::min(-sunAltitude / 0.3f, 1.0f);
        t = std::max(t, 0.0f);
        Vec3 twilight(0.3f, 0.4f, 0.6f);
        Vec3 sunset(1.0f, 0.6f, 0.3f);
        return Vec3(
            sunset.x + t * (twilight.x - sunset.x),
            sunset.y + t * (twilight.y - sunset.y),
            sunset.z + t * (twilight.z - sunset.z)
        );
    }
}

// Sun intensity calculation (from EngineEditorMode.cpp)
float CalculateSunIntensity(float timeOfDay) {
    float hourAngle = (timeOfDay - 12.0f) * (static_cast<float>(M_PI) / 12.0f);
    float sunAltitude = std::cos(hourAngle);

    if (sunAltitude > 0.0f) {
        return 5.0f + sunAltitude * 5.0f;
    } else {
        return std::max(0.1f, 0.5f + sunAltitude * 2.0f);
    }
}

// Time of day normalization (from EngineEditorMode.cpp)
float NormalizeTimeOfDay(float hour) {
    float result = std::fmod(hour, 24.0f);
    if (result < 0.0f) {
        result += 24.0f;
    }
    return result;
}

// ============================================================================
// Tests
// ============================================================================

bool test_editor_state_defaults() {
    EditorState state;
    TEST_ASSERT(state.showGrid == true, "showGrid should default to true");
    TEST_ASSERT(state.showGizmos == true, "showGizmos should default to true");
    TEST_ASSERT(state.wireframeMode == false, "wireframeMode should default to false");
    TEST_ASSERT(TEST_FLOAT_EQ(state.timeOfDay, 10.0f, 0.001f), "timeOfDay should default to 10.0");
    TEST_ASSERT(state.timePaused == true, "timePaused should default to true");
    TEST_ASSERT(state.proceduralSky == true, "proceduralSky should default to true");
    TEST_ASSERT(state.ssao == false, "ssao should default to false");
    TEST_PASS();
}

bool test_sun_direction_noon() {
    Vec3 sunDir = CalculateSunDirection(12.0f);  // Noon
    // At noon, sun should be roughly overhead (Y > 0)
    TEST_ASSERT(sunDir.y > 0.5f, "Sun at noon should be high (Y > 0.5)");
    // X should be near 0 at noon
    TEST_ASSERT(std::abs(sunDir.x) < 0.3f, "Sun at noon should have small X component");
    TEST_PASS();
}

bool test_sun_direction_sunrise() {
    Vec3 sunDir = CalculateSunDirection(6.0f);  // 6am sunrise
    // At sunrise, sun should be in east (+X) and near horizon (low Y)
    TEST_ASSERT(sunDir.x < -0.5f, "Sun at 6am should be in east (X < -0.5)");
    TEST_PASS();
}

bool test_sun_direction_sunset() {
    Vec3 sunDir = CalculateSunDirection(18.0f);  // 6pm sunset
    // At sunset, sun should be in west (-X)
    TEST_ASSERT(sunDir.x > 0.5f, "Sun at 6pm should be in west (X > 0.5)");
    TEST_PASS();
}

bool test_sun_direction_midnight() {
    Vec3 sunDir = CalculateSunDirection(0.0f);  // Midnight
    // At midnight, sun should be below horizon but clamped to -0.2
    TEST_ASSERT(sunDir.y <= 0.0f, "Sun at midnight should be low or below horizon");
    TEST_PASS();
}

bool test_sun_color_noon() {
    Vec3 color = CalculateSunColor(12.0f);  // Noon
    // At noon, color should be white-ish
    TEST_ASSERT(color.x > 0.9f, "Sun color at noon should have high R");
    TEST_ASSERT(color.y > 0.9f, "Sun color at noon should have high G");
    TEST_ASSERT(color.z > 0.9f, "Sun color at noon should have high B");
    TEST_PASS();
}

bool test_sun_color_sunset() {
    Vec3 color = CalculateSunColor(6.0f);  // Near sunrise/sunset
    // At sunrise/sunset, color should be warm (more red than blue)
    TEST_ASSERT(color.x > color.z, "Sun color at sunrise should be warmer (R > B)");
    TEST_PASS();
}

bool test_sun_intensity_noon() {
    float intensity = CalculateSunIntensity(12.0f);  // Noon
    // At noon, intensity should be maximum (around 10)
    TEST_ASSERT(intensity > 9.0f, "Sun intensity at noon should be high");
    TEST_ASSERT(intensity <= 10.0f, "Sun intensity at noon should be <= 10");
    TEST_PASS();
}

bool test_sun_intensity_night() {
    float intensity = CalculateSunIntensity(0.0f);  // Midnight
    // At midnight, intensity should be very low
    TEST_ASSERT(intensity < 1.0f, "Sun intensity at midnight should be low");
    TEST_ASSERT(intensity >= 0.1f, "Sun intensity should never go below 0.1");
    TEST_PASS();
}

bool test_time_normalization_wrap_forward() {
    float time = NormalizeTimeOfDay(25.0f);  // 25h -> 1h
    TEST_ASSERT(TEST_FLOAT_EQ(time, 1.0f, 0.001f), "25h should wrap to 1h");
    TEST_PASS();
}

bool test_time_normalization_wrap_backward() {
    float time = NormalizeTimeOfDay(-1.0f);  // -1h -> 23h
    TEST_ASSERT(TEST_FLOAT_EQ(time, 23.0f, 0.001f), "-1h should wrap to 23h");
    TEST_PASS();
}

bool test_time_normalization_normal() {
    float time = NormalizeTimeOfDay(14.5f);  // 2:30pm
    TEST_ASSERT(TEST_FLOAT_EQ(time, 14.5f, 0.001f), "14.5h should stay 14.5h");
    TEST_PASS();
}

bool test_time_normalization_wrap_large() {
    float time = NormalizeTimeOfDay(50.0f);  // 50h -> 2h (50 % 24 = 2)
    TEST_ASSERT(TEST_FLOAT_EQ(time, 2.0f, 0.001f), "50h should wrap to 2h");
    TEST_PASS();
}

bool test_sun_direction_normalized() {
    // Test various times to ensure sun direction is always normalized
    float times[] = {0.0f, 6.0f, 12.0f, 18.0f, 23.5f};
    for (float t : times) {
        Vec3 dir = CalculateSunDirection(t);
        float len = dir.length();
        TEST_ASSERT(TEST_FLOAT_EQ(len, 1.0f, 0.01f),
                    "Sun direction should be normalized");
    }
    TEST_PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Phase 2: EngineEditorMode Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Run all tests
    test_editor_state_defaults();
    test_sun_direction_noon();
    test_sun_direction_sunrise();
    test_sun_direction_sunset();
    test_sun_direction_midnight();
    test_sun_color_noon();
    test_sun_color_sunset();
    test_sun_intensity_noon();
    test_sun_intensity_night();
    test_time_normalization_wrap_forward();
    test_time_normalization_wrap_backward();
    test_time_normalization_normal();
    test_time_normalization_wrap_large();
    test_sun_direction_normalized();

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << g_testsPassed << " passed, " << g_testsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return g_testsFailed > 0 ? 1 : 0;
}
