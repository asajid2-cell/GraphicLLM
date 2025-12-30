// test_phase1_engine_editor.cpp
// Self-contained unit tests for Phase 1: Engine Editor Mode implementation
// Compile: cl /EHsc /std:c++20 test_phase1_engine_editor.cpp /Fe:test_phase1.exe
// Run: test_phase1.exe

#include <iostream>
#include <string>
#include <algorithm>
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

// ============================================================================
// Extracted Logic to Test (copied from Engine.cpp for isolated testing)
// ============================================================================

// Simulates the preset parsing logic from Engine::Initialize
struct PresetParseResult {
    enum class Preset { Unknown, Dragon, Cornell, RTShowcase, GodRays, EngineEditor };
    Preset preset = Preset::Unknown;
    bool engineEditorMode = false;
};

PresetParseResult ParseScenePreset(const std::string& presetStr) {
    PresetParseResult result;

    if (presetStr.empty()) {
        return result;  // Unknown
    }

    std::string sceneLower = presetStr;
    std::transform(sceneLower.begin(), sceneLower.end(), sceneLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (sceneLower == "dragon" || sceneLower == "dragonoverwater") {
        result.preset = PresetParseResult::Preset::Dragon;
    } else if (sceneLower == "cornell" || sceneLower == "cornellbox") {
        result.preset = PresetParseResult::Preset::Cornell;
    } else if (sceneLower == "rt" || sceneLower == "rtshowcase" || sceneLower == "rt_showcase") {
        result.preset = PresetParseResult::Preset::RTShowcase;
    } else if (sceneLower == "god_rays" || sceneLower == "godrays") {
        result.preset = PresetParseResult::Preset::GodRays;
    } else if (sceneLower == "engine_editor" || sceneLower == "engineeditor") {
        result.preset = PresetParseResult::Preset::EngineEditor;
        result.engineEditorMode = true;
    }

    return result;
}

// Simulates the EngineMode enum from Engine.h
enum class EngineMode { Editor = 0, Play = 1 };

// Simulates the LauncherControlId enum from main.cpp
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

// ============================================================================
// Tests
// ============================================================================

bool test_preset_parsing_engine_editor() {
    auto result = ParseScenePreset("engine_editor");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::EngineEditor,
                "engine_editor should parse to EngineEditor preset");
    TEST_ASSERT(result.engineEditorMode == true,
                "engine_editor should set engineEditorMode to true");
    TEST_PASS();
}

bool test_preset_parsing_engine_editor_camelcase() {
    auto result = ParseScenePreset("engineeditor");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::EngineEditor,
                "engineeditor (no underscore) should parse to EngineEditor preset");
    TEST_ASSERT(result.engineEditorMode == true,
                "engineeditor should set engineEditorMode to true");
    TEST_PASS();
}

bool test_preset_parsing_engine_editor_uppercase() {
    auto result = ParseScenePreset("ENGINE_EDITOR");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::EngineEditor,
                "ENGINE_EDITOR should parse to EngineEditor preset (case insensitive)");
    TEST_PASS();
}

bool test_preset_parsing_dragon() {
    auto result = ParseScenePreset("dragon");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::Dragon,
                "dragon should parse to Dragon preset");
    TEST_ASSERT(result.engineEditorMode == false,
                "dragon should NOT set engineEditorMode");
    TEST_PASS();
}

bool test_preset_parsing_cornell() {
    auto result = ParseScenePreset("cornellbox");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::Cornell,
                "cornellbox should parse to Cornell preset");
    TEST_PASS();
}

bool test_preset_parsing_rt_showcase() {
    auto result = ParseScenePreset("rt_showcase");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::RTShowcase,
                "rt_showcase should parse to RTShowcase preset");
    TEST_PASS();
}

bool test_preset_parsing_god_rays() {
    auto result = ParseScenePreset("god_rays");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::GodRays,
                "god_rays should parse to GodRays preset");
    TEST_PASS();
}

bool test_preset_parsing_unknown() {
    auto result = ParseScenePreset("invalid_preset");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::Unknown,
                "invalid preset should return Unknown");
    TEST_ASSERT(result.engineEditorMode == false,
                "invalid preset should NOT set engineEditorMode");
    TEST_PASS();
}

bool test_preset_parsing_empty() {
    auto result = ParseScenePreset("");
    TEST_ASSERT(result.preset == PresetParseResult::Preset::Unknown,
                "empty string should return Unknown");
    TEST_PASS();
}

bool test_engine_mode_enum_values() {
    TEST_ASSERT(static_cast<int>(EngineMode::Editor) == 0,
                "EngineMode::Editor should be 0");
    TEST_ASSERT(static_cast<int>(EngineMode::Play) == 1,
                "EngineMode::Play should be 1");
    TEST_PASS();
}

bool test_launcher_control_ids_unique() {
    // Ensure all launcher control IDs are unique
    int ids[] = {
        IDC_LAUNCH_SCENE, IDC_LAUNCH_QUALITY, IDC_LAUNCH_RT,
        IDC_LAUNCH_LLM, IDC_LAUNCH_DREAMER, IDC_LAUNCH_RASTER,
        IDC_LAUNCH_VOXEL, IDC_LAUNCH_OK, IDC_LAUNCH_CANCEL,
        IDC_LAUNCH_EDITOR
    };
    int count = sizeof(ids) / sizeof(ids[0]);

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            TEST_ASSERT(ids[i] != ids[j],
                        "Launcher control IDs must be unique");
        }
    }
    TEST_PASS();
}

bool test_launcher_editor_button_id() {
    TEST_ASSERT(IDC_LAUNCH_EDITOR == 2012,
                "IDC_LAUNCH_EDITOR should be 2012");
    TEST_PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Phase 1: Engine Editor Mode Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Run all tests
    test_preset_parsing_engine_editor();
    test_preset_parsing_engine_editor_camelcase();
    test_preset_parsing_engine_editor_uppercase();
    test_preset_parsing_dragon();
    test_preset_parsing_cornell();
    test_preset_parsing_rt_showcase();
    test_preset_parsing_god_rays();
    test_preset_parsing_unknown();
    test_preset_parsing_empty();
    test_engine_mode_enum_values();
    test_launcher_control_ids_unique();
    test_launcher_editor_button_id();

    std::cout << "========================================" << std::endl;
    std::cout << "Results: " << g_testsPassed << " passed, " << g_testsFailed << " failed" << std::endl;
    std::cout << "========================================" << std::endl;

    return g_testsFailed > 0 ? 1 : 0;
}
