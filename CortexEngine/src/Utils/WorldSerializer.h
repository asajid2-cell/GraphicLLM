#pragma once

// WorldSerializer.h
// Serialization of world state including terrain, chunks, weather, and time.
// Supports incremental saves and streaming.

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Cortex {

// Forward declarations
class ChunkGenerator;
class BiomeMap;
class WeatherSystem;

namespace Serialization {

// Chunk save data
struct ChunkSaveData {
    int32_t x = 0;
    int32_t z = 0;
    uint32_t lodLevel = 0;

    // Terrain modifications (erosion, sculpting)
    std::vector<float> heightModifications;     // Delta from original height
    std::vector<uint8_t> biomeOverrides;        // Per-vertex biome overrides

    // Entity spawns within chunk
    std::vector<uint32_t> spawnedEntityIds;

    // Timestamps
    float lastModifiedTime = 0.0f;
    float creationTime = 0.0f;

    bool IsDirty() const { return !heightModifications.empty() || !biomeOverrides.empty(); }
};

// Weather save data
struct WeatherSaveData {
    int32_t currentWeatherType = 0;
    int32_t targetWeatherType = 0;
    float transitionProgress = 0.0f;
    float cloudCoverage = 0.0f;
    float precipitation = 0.0f;
    float windSpeed = 0.0f;
    glm::vec2 windDirection = glm::vec2(1, 0);
    float temperature = 20.0f;
    float humidity = 0.5f;
};

// Time-of-day save data
struct TimeOfDaySaveData {
    float timeOfDay = 12.0f;        // Hours (0-24)
    int32_t dayNumber = 1;
    float timeScale = 1.0f;
    bool isPaused = false;
};

// Player state for world context
struct PlayerWorldState {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    uint32_t currentBiome = 0;
    std::string lastCheckpoint;
};

// Complete world save data
struct WorldSaveData {
    // Metadata
    uint32_t version = 1;
    std::string worldName;
    std::string seed;
    float totalPlayTime = 0.0f;
    std::string saveTimestamp;

    // World settings
    float worldScale = 1.0f;
    int32_t chunkSize = 64;
    int32_t viewDistance = 8;

    // Time and weather
    TimeOfDaySaveData timeOfDay;
    WeatherSaveData weather;

    // Player context
    PlayerWorldState playerState;

    // Modified chunks only (not procedural base)
    std::vector<ChunkSaveData> modifiedChunks;

    // Global terrain modifications (e.g., from erosion simulation)
    std::vector<std::pair<glm::ivec2, std::vector<float>>> globalHeightMods;
};

// World serialization callbacks
struct WorldSerializationCallbacks {
    // Custom data serializers
    std::function<nlohmann::json()> serializeCustomData;
    std::function<void(const nlohmann::json&)> deserializeCustomData;

    // Progress callbacks
    std::function<void(float progress, const std::string& status)> onProgress;

    // Error callback
    std::function<void(const std::string& error)> onError;
};

// World serializer class
class WorldSerializer {
public:
    WorldSerializer();
    ~WorldSerializer() = default;

    // Serialize world state
    nlohmann::json SerializeWorld(const WorldSaveData& data) const;
    WorldSaveData DeserializeWorld(const nlohmann::json& json) const;

    // Serialize individual chunks
    nlohmann::json SerializeChunk(const ChunkSaveData& chunk) const;
    ChunkSaveData DeserializeChunk(const nlohmann::json& json) const;

    // Serialize weather state
    nlohmann::json SerializeWeather(const WeatherSaveData& weather) const;
    WeatherSaveData DeserializeWeather(const nlohmann::json& json) const;

    // Serialize time of day
    nlohmann::json SerializeTimeOfDay(const TimeOfDaySaveData& time) const;
    TimeOfDaySaveData DeserializeTimeOfDay(const nlohmann::json& json) const;

    // File operations
    bool SaveWorldToFile(const std::string& path, const WorldSaveData& data);
    bool LoadWorldFromFile(const std::string& path, WorldSaveData& data);

    // Incremental chunk saving (for streaming worlds)
    bool SaveChunkToFile(const std::string& basePath, const ChunkSaveData& chunk);
    bool LoadChunkFromFile(const std::string& basePath, int32_t x, int32_t z, ChunkSaveData& chunk);
    bool DoesChunkSaveExist(const std::string& basePath, int32_t x, int32_t z) const;

    // Callbacks
    void SetCallbacks(const WorldSerializationCallbacks& callbacks) { m_callbacks = callbacks; }

    // Utilities
    static std::string GetChunkFilename(int32_t x, int32_t z);
    static std::string GenerateSaveTimestamp();

private:
    // Binary chunk format for efficiency
    bool SaveChunkBinary(const std::string& path, const ChunkSaveData& chunk);
    bool LoadChunkBinary(const std::string& path, ChunkSaveData& chunk);

    // Compression helpers
    std::vector<uint8_t> CompressHeightData(const std::vector<float>& heights) const;
    std::vector<float> DecompressHeightData(const std::vector<uint8_t>& compressed, size_t expectedSize) const;

    WorldSerializationCallbacks m_callbacks;
};

// World state manager (integrates with engine systems)
class WorldStateManager {
public:
    WorldStateManager();
    ~WorldStateManager() = default;

    // Capture current world state from engine systems
    WorldSaveData CaptureWorldState(ChunkGenerator* chunks,
                                     BiomeMap* biomes,
                                     WeatherSystem* weather) const;

    // Apply loaded world state to engine systems
    void ApplyWorldState(const WorldSaveData& data,
                          ChunkGenerator* chunks,
                          BiomeMap* biomes,
                          WeatherSystem* weather);

    // Track chunk modifications
    void MarkChunkDirty(int32_t x, int32_t z);
    bool IsChunkDirty(int32_t x, int32_t z) const;
    std::vector<glm::ivec2> GetDirtyChunks() const;
    void ClearDirtyChunks();

    // Track modified height data
    void RecordHeightModification(int32_t chunkX, int32_t chunkZ,
                                   uint32_t vertexIndex, float delta);
    const std::vector<float>& GetHeightModifications(int32_t chunkX, int32_t chunkZ) const;

private:
    std::unordered_map<uint64_t, bool> m_dirtyChunks;
    std::unordered_map<uint64_t, std::vector<float>> m_heightModifications;

    uint64_t ChunkKey(int32_t x, int32_t z) const {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(z));
    }
};

} // namespace Serialization
} // namespace Cortex
