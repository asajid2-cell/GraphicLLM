// ConfigLoader.cpp
// Implementation of JSON configuration loading for Engine Editor.

#include "ConfigLoader.h"
#include "Editor/EditorWorld.h"
#include <fstream>
#include <spdlog/spdlog.h>

namespace Cortex::Utils {

Result<nlohmann::json> ConfigLoader::ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<nlohmann::json>::Err("Failed to open config file: " + path);
    }

    try {
        nlohmann::json j;
        file >> j;
        return Result<nlohmann::json>::Ok(std::move(j));
    } catch (const nlohmann::json::parse_error& e) {
        return Result<nlohmann::json>::Err("JSON parse error in " + path + ": " + e.what());
    }
}

Result<EditorConfig> ConfigLoader::LoadEditorDefaults(const std::string& basePath) {
    std::string path = basePath + "/editor_defaults.json";
    auto jsonResult = ReadJsonFile(path);

    // If file doesn't exist or can't be parsed, return defaults
    if (jsonResult.IsErr()) {
        spdlog::warn("Could not load {}: {}. Using defaults.", path, jsonResult.Error());
        return Result<EditorConfig>::Ok(EditorConfig{});
    }

    const auto& j = jsonResult.Value();
    EditorConfig config;

    // Parse world settings
    if (j.contains("world")) {
        const auto& w = j["world"];
        config.world.chunkSize = GetOr(w, "chunkSize", 64.0f);
        config.world.loadRadius = GetOr(w, "loadRadius", 8);
        config.world.maxLoadedChunks = GetOr(w, "maxLoadedChunks", 500);
        config.world.chunkGeneratorThreads = GetOr(w, "chunkGeneratorThreads", 2u);
        config.world.maxChunksPerFrame = GetOr(w, "maxChunksPerFrame", 4u);
    }

    // Parse LOD settings
    if (j.contains("lod")) {
        const auto& l = j["lod"];
        config.lod.distance1 = GetOr(l, "distance1", 256.0f);
        config.lod.distance2 = GetOr(l, "distance2", 512.0f);
        config.lod.distance3 = GetOr(l, "distance3", 1024.0f);
    }

    // Parse camera settings
    if (j.contains("camera")) {
        const auto& c = j["camera"];
        config.camera.flySpeed = GetOr(c, "flySpeed", 20.0f);
        config.camera.sprintMultiplier = GetOr(c, "sprintMultiplier", 3.0f);
        config.camera.mouseSensitivity = GetOr(c, "mouseSensitivity", 0.003f);
        config.camera.fov = GetOr(c, "fov", 60.0f);
        config.camera.nearPlane = GetOr(c, "nearPlane", 0.1f);
        config.camera.farPlane = GetOr(c, "farPlane", 2000.0f);
    }

    // Parse rendering settings
    if (j.contains("rendering")) {
        const auto& r = j["rendering"];
        config.rendering.proceduralSky = GetOr(r, "proceduralSky", true);
        config.rendering.shadows = GetOr(r, "shadows", true);
        config.rendering.ssao = GetOr(r, "ssao", false);
        config.rendering.ssr = GetOr(r, "ssr", false);
        config.rendering.fog = GetOr(r, "fog", true);
        config.rendering.fogDensity = GetOr(r, "fogDensity", 0.01f);
        config.rendering.fogHeight = GetOr(r, "fogHeight", 0.0f);
        config.rendering.fogFalloff = GetOr(r, "fogFalloff", 0.5f);
    }

    // Parse debug settings
    if (j.contains("debug")) {
        const auto& d = j["debug"];
        config.debug.showGrid = GetOr(d, "showGrid", true);
        config.debug.showChunkBounds = GetOr(d, "showChunkBounds", false);
        config.debug.showStats = GetOr(d, "showStats", true);
        config.debug.showGizmos = GetOr(d, "showGizmos", true);
    }

    // Parse time of day settings
    if (j.contains("timeOfDay")) {
        const auto& t = j["timeOfDay"];
        config.timeOfDay.defaultHour = GetOr(t, "default", 10.0f);
        config.timeOfDay.autoAdvance = GetOr(t, "autoAdvance", false);
        config.timeOfDay.scale = GetOr(t, "scale", 60.0f);
    }

    spdlog::info("Loaded editor config from {}", path);
    return Result<EditorConfig>::Ok(std::move(config));
}

Result<std::vector<TerrainPreset>> ConfigLoader::LoadTerrainPresets(const std::string& basePath) {
    std::string path = basePath + "/terrain_presets.json";
    auto jsonResult = ReadJsonFile(path);

    if (jsonResult.IsErr()) {
        spdlog::warn("Could not load {}: {}. Using default terrain.", path, jsonResult.Error());
        // Return a single default preset
        std::vector<TerrainPreset> defaults;
        TerrainPreset defaultPreset;
        defaultPreset.name = "Default";
        defaultPreset.params = Scene::TerrainNoiseParams{
            .seed = 42,
            .amplitude = 20.0f,
            .frequency = 0.003f,
            .octaves = 6,
            .lacunarity = 2.0f,
            .gain = 0.5f,
            .warp = 15.0f
        };
        defaults.push_back(defaultPreset);
        return Result<std::vector<TerrainPreset>>::Ok(std::move(defaults));
    }

    const auto& j = jsonResult.Value();
    std::vector<TerrainPreset> presets;

    if (j.contains("presets") && j["presets"].is_object()) {
        for (auto& [key, value] : j["presets"].items()) {
            TerrainPreset preset;
            preset.name = GetOr(value, "name", key);
            preset.params.seed = GetOr(value, "seed", 42u);
            preset.params.amplitude = GetOr(value, "amplitude", 20.0f);
            preset.params.frequency = GetOr(value, "frequency", 0.003f);
            preset.params.octaves = GetOr(value, "octaves", 6u);
            preset.params.lacunarity = GetOr(value, "lacunarity", 2.0f);
            preset.params.gain = GetOr(value, "gain", 0.5f);
            preset.params.warp = GetOr(value, "warp", 15.0f);
            presets.push_back(preset);
        }
    }

    if (presets.empty()) {
        // Add a default if none were loaded
        TerrainPreset defaultPreset;
        defaultPreset.name = "Default";
        defaultPreset.params = Scene::TerrainNoiseParams{
            .seed = 42,
            .amplitude = 20.0f,
            .frequency = 0.003f,
            .octaves = 6,
            .lacunarity = 2.0f,
            .gain = 0.5f,
            .warp = 15.0f
        };
        presets.push_back(defaultPreset);
    }

    spdlog::info("Loaded {} terrain presets from {}", presets.size(), path);
    return Result<std::vector<TerrainPreset>>::Ok(std::move(presets));
}

std::optional<TerrainPreset> ConfigLoader::GetTerrainPreset(
    const std::vector<TerrainPreset>& presets,
    const std::string& name) {

    for (const auto& preset : presets) {
        if (preset.name == name) {
            return preset;
        }
    }
    return std::nullopt;
}

void ConfigLoader::ApplyToWorldConfig(const EditorConfig& config, EditorWorldConfig& worldConfig) {
    worldConfig.chunkSize = config.world.chunkSize;
    worldConfig.loadRadius = config.world.loadRadius;
    worldConfig.maxLoadedChunks = config.world.maxLoadedChunks;
    worldConfig.chunkGeneratorThreads = config.world.chunkGeneratorThreads;
    worldConfig.maxChunksPerFrame = config.world.maxChunksPerFrame;

    // Convert distances to squared values for efficient comparison
    worldConfig.lodDistance1Sq = config.lod.distance1 * config.lod.distance1;
    worldConfig.lodDistance2Sq = config.lod.distance2 * config.lod.distance2;
    worldConfig.lodDistance3Sq = config.lod.distance3 * config.lod.distance3;
}

} // namespace Cortex::Utils
