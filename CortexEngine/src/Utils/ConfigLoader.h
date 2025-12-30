#pragma once

// ConfigLoader.h
// Utility for loading JSON configuration files for the Engine Editor.
// Provides type-safe access to configuration values with defaults.

#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include "Scene/TerrainNoise.h"
#include "Utils/Result.h"

namespace Cortex {

// Forward declarations
struct EditorWorldConfig;

namespace Utils {

// Configuration data structures loaded from JSON
struct EditorConfig {
    // World settings
    struct World {
        float chunkSize = 64.0f;
        int32_t loadRadius = 8;
        int32_t maxLoadedChunks = 500;
        uint32_t chunkGeneratorThreads = 2;
        uint32_t maxChunksPerFrame = 4;
    } world;

    // LOD distance settings (in world units, will be squared for comparison)
    struct LOD {
        float distance1 = 256.0f;
        float distance2 = 512.0f;
        float distance3 = 1024.0f;
    } lod;

    // Camera settings
    struct Camera {
        float flySpeed = 20.0f;
        float sprintMultiplier = 3.0f;
        float mouseSensitivity = 0.003f;
        float fov = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 2000.0f;
    } camera;

    // Rendering settings
    struct Rendering {
        bool proceduralSky = true;
        bool shadows = true;
        bool ssao = false;
        bool ssr = false;
        bool fog = true;
        float fogDensity = 0.01f;
        float fogHeight = 0.0f;
        float fogFalloff = 0.5f;
    } rendering;

    // Debug visualization settings
    struct Debug {
        bool showGrid = true;
        bool showChunkBounds = false;
        bool showStats = true;
        bool showGizmos = true;
    } debug;

    // Time of day settings
    struct TimeOfDay {
        float defaultHour = 10.0f;
        bool autoAdvance = false;
        float scale = 60.0f;  // 1 real second = N game minutes
    } timeOfDay;
};

// Terrain preset loaded from JSON
struct TerrainPreset {
    std::string name;
    Scene::TerrainNoiseParams params;
};

// ConfigLoader - loads and parses JSON configuration files
class ConfigLoader {
public:
    // Load editor defaults from assets/config/editor_defaults.json
    static Result<EditorConfig> LoadEditorDefaults(const std::string& basePath = "assets/config");

    // Load terrain presets from assets/config/terrain_presets.json
    static Result<std::vector<TerrainPreset>> LoadTerrainPresets(const std::string& basePath = "assets/config");

    // Get a specific terrain preset by name
    static std::optional<TerrainPreset> GetTerrainPreset(
        const std::vector<TerrainPreset>& presets,
        const std::string& name);

    // Apply EditorConfig to EditorWorldConfig
    static void ApplyToWorldConfig(const EditorConfig& config, EditorWorldConfig& worldConfig);

private:
    // Helper to read JSON file
    static Result<nlohmann::json> ReadJsonFile(const std::string& path);

    // Parse helpers with defaults
    template<typename T>
    static T GetOr(const nlohmann::json& j, const std::string& key, T defaultValue) {
        if (j.contains(key)) {
            try {
                return j[key].get<T>();
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
};

} // namespace Utils
} // namespace Cortex
