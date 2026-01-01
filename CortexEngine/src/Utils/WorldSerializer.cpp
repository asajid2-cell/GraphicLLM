// WorldSerializer.cpp
// Implementation of world state serialization.

#include "WorldSerializer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>

namespace Cortex::Serialization {

// ============================================================================
// WorldSerializer
// ============================================================================

WorldSerializer::WorldSerializer() {}

nlohmann::json WorldSerializer::SerializeWorld(const WorldSaveData& data) const {
    nlohmann::json j;

    // Metadata
    j["version"] = data.version;
    j["worldName"] = data.worldName;
    j["seed"] = data.seed;
    j["totalPlayTime"] = data.totalPlayTime;
    j["saveTimestamp"] = data.saveTimestamp.empty() ? GenerateSaveTimestamp() : data.saveTimestamp;

    // World settings
    j["worldScale"] = data.worldScale;
    j["chunkSize"] = data.chunkSize;
    j["viewDistance"] = data.viewDistance;

    // Time and weather
    j["timeOfDay"] = SerializeTimeOfDay(data.timeOfDay);
    j["weather"] = SerializeWeather(data.weather);

    // Player state
    j["player"] = {
        {"position", {data.playerState.position.x, data.playerState.position.y, data.playerState.position.z}},
        {"rotation", {data.playerState.rotation.x, data.playerState.rotation.y, data.playerState.rotation.z}},
        {"currentBiome", data.playerState.currentBiome},
        {"lastCheckpoint", data.playerState.lastCheckpoint}
    };

    // Modified chunks (summary - actual chunk data saved separately)
    nlohmann::json chunksJson = nlohmann::json::array();
    for (const auto& chunk : data.modifiedChunks) {
        chunksJson.push_back({
            {"x", chunk.x},
            {"z", chunk.z},
            {"lodLevel", chunk.lodLevel},
            {"lastModified", chunk.lastModifiedTime}
        });
    }
    j["modifiedChunks"] = chunksJson;

    return j;
}

WorldSaveData WorldSerializer::DeserializeWorld(const nlohmann::json& j) const {
    WorldSaveData data;

    // Metadata
    if (j.contains("version")) data.version = j["version"].get<uint32_t>();
    if (j.contains("worldName")) data.worldName = j["worldName"].get<std::string>();
    if (j.contains("seed")) data.seed = j["seed"].get<std::string>();
    if (j.contains("totalPlayTime")) data.totalPlayTime = j["totalPlayTime"].get<float>();
    if (j.contains("saveTimestamp")) data.saveTimestamp = j["saveTimestamp"].get<std::string>();

    // World settings
    if (j.contains("worldScale")) data.worldScale = j["worldScale"].get<float>();
    if (j.contains("chunkSize")) data.chunkSize = j["chunkSize"].get<int32_t>();
    if (j.contains("viewDistance")) data.viewDistance = j["viewDistance"].get<int32_t>();

    // Time and weather
    if (j.contains("timeOfDay")) data.timeOfDay = DeserializeTimeOfDay(j["timeOfDay"]);
    if (j.contains("weather")) data.weather = DeserializeWeather(j["weather"]);

    // Player state
    if (j.contains("player")) {
        const auto& p = j["player"];
        if (p.contains("position") && p["position"].size() >= 3) {
            data.playerState.position = glm::vec3(
                p["position"][0].get<float>(),
                p["position"][1].get<float>(),
                p["position"][2].get<float>()
            );
        }
        if (p.contains("rotation") && p["rotation"].size() >= 3) {
            data.playerState.rotation = glm::vec3(
                p["rotation"][0].get<float>(),
                p["rotation"][1].get<float>(),
                p["rotation"][2].get<float>()
            );
        }
        if (p.contains("currentBiome")) data.playerState.currentBiome = p["currentBiome"].get<uint32_t>();
        if (p.contains("lastCheckpoint")) data.playerState.lastCheckpoint = p["lastCheckpoint"].get<std::string>();
    }

    // Modified chunks (metadata only - data loaded separately)
    if (j.contains("modifiedChunks")) {
        for (const auto& chunkJson : j["modifiedChunks"]) {
            ChunkSaveData chunk;
            if (chunkJson.contains("x")) chunk.x = chunkJson["x"].get<int32_t>();
            if (chunkJson.contains("z")) chunk.z = chunkJson["z"].get<int32_t>();
            if (chunkJson.contains("lodLevel")) chunk.lodLevel = chunkJson["lodLevel"].get<uint32_t>();
            if (chunkJson.contains("lastModified")) chunk.lastModifiedTime = chunkJson["lastModified"].get<float>();
            data.modifiedChunks.push_back(chunk);
        }
    }

    return data;
}

nlohmann::json WorldSerializer::SerializeChunk(const ChunkSaveData& chunk) const {
    nlohmann::json j;

    j["x"] = chunk.x;
    j["z"] = chunk.z;
    j["lodLevel"] = chunk.lodLevel;
    j["lastModified"] = chunk.lastModifiedTime;
    j["creationTime"] = chunk.creationTime;

    // Height modifications (as base64 for efficiency in JSON)
    if (!chunk.heightModifications.empty()) {
        std::vector<uint8_t> compressed = CompressHeightData(chunk.heightModifications);
        // Convert to hex string for JSON storage
        std::stringstream ss;
        for (uint8_t b : compressed) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        j["heightMods"] = ss.str();
        j["heightModsSize"] = chunk.heightModifications.size();
    }

    // Biome overrides
    if (!chunk.biomeOverrides.empty()) {
        std::stringstream ss;
        for (uint8_t b : chunk.biomeOverrides) {
            ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        j["biomeOverrides"] = ss.str();
    }

    // Entity IDs
    if (!chunk.spawnedEntityIds.empty()) {
        j["entities"] = chunk.spawnedEntityIds;
    }

    return j;
}

ChunkSaveData WorldSerializer::DeserializeChunk(const nlohmann::json& j) const {
    ChunkSaveData chunk;

    if (j.contains("x")) chunk.x = j["x"].get<int32_t>();
    if (j.contains("z")) chunk.z = j["z"].get<int32_t>();
    if (j.contains("lodLevel")) chunk.lodLevel = j["lodLevel"].get<uint32_t>();
    if (j.contains("lastModified")) chunk.lastModifiedTime = j["lastModified"].get<float>();
    if (j.contains("creationTime")) chunk.creationTime = j["creationTime"].get<float>();

    // Height modifications
    if (j.contains("heightMods") && j.contains("heightModsSize")) {
        std::string hexStr = j["heightMods"].get<std::string>();
        size_t expectedSize = j["heightModsSize"].get<size_t>();

        std::vector<uint8_t> compressed;
        for (size_t i = 0; i < hexStr.length(); i += 2) {
            uint8_t b = static_cast<uint8_t>(std::stoi(hexStr.substr(i, 2), nullptr, 16));
            compressed.push_back(b);
        }

        chunk.heightModifications = DecompressHeightData(compressed, expectedSize);
    }

    // Biome overrides
    if (j.contains("biomeOverrides")) {
        std::string hexStr = j["biomeOverrides"].get<std::string>();
        for (size_t i = 0; i < hexStr.length(); i += 2) {
            uint8_t b = static_cast<uint8_t>(std::stoi(hexStr.substr(i, 2), nullptr, 16));
            chunk.biomeOverrides.push_back(b);
        }
    }

    // Entity IDs
    if (j.contains("entities")) {
        chunk.spawnedEntityIds = j["entities"].get<std::vector<uint32_t>>();
    }

    return chunk;
}

nlohmann::json WorldSerializer::SerializeWeather(const WeatherSaveData& weather) const {
    return {
        {"currentType", weather.currentWeatherType},
        {"targetType", weather.targetWeatherType},
        {"transitionProgress", weather.transitionProgress},
        {"cloudCoverage", weather.cloudCoverage},
        {"precipitation", weather.precipitation},
        {"windSpeed", weather.windSpeed},
        {"windDirection", {weather.windDirection.x, weather.windDirection.y}},
        {"temperature", weather.temperature},
        {"humidity", weather.humidity}
    };
}

WeatherSaveData WorldSerializer::DeserializeWeather(const nlohmann::json& j) const {
    WeatherSaveData weather;

    if (j.contains("currentType")) weather.currentWeatherType = j["currentType"].get<int32_t>();
    if (j.contains("targetType")) weather.targetWeatherType = j["targetType"].get<int32_t>();
    if (j.contains("transitionProgress")) weather.transitionProgress = j["transitionProgress"].get<float>();
    if (j.contains("cloudCoverage")) weather.cloudCoverage = j["cloudCoverage"].get<float>();
    if (j.contains("precipitation")) weather.precipitation = j["precipitation"].get<float>();
    if (j.contains("windSpeed")) weather.windSpeed = j["windSpeed"].get<float>();
    if (j.contains("windDirection") && j["windDirection"].size() >= 2) {
        weather.windDirection.x = j["windDirection"][0].get<float>();
        weather.windDirection.y = j["windDirection"][1].get<float>();
    }
    if (j.contains("temperature")) weather.temperature = j["temperature"].get<float>();
    if (j.contains("humidity")) weather.humidity = j["humidity"].get<float>();

    return weather;
}

nlohmann::json WorldSerializer::SerializeTimeOfDay(const TimeOfDaySaveData& time) const {
    return {
        {"timeOfDay", time.timeOfDay},
        {"dayNumber", time.dayNumber},
        {"timeScale", time.timeScale},
        {"isPaused", time.isPaused}
    };
}

TimeOfDaySaveData WorldSerializer::DeserializeTimeOfDay(const nlohmann::json& j) const {
    TimeOfDaySaveData time;

    if (j.contains("timeOfDay")) time.timeOfDay = j["timeOfDay"].get<float>();
    if (j.contains("dayNumber")) time.dayNumber = j["dayNumber"].get<int32_t>();
    if (j.contains("timeScale")) time.timeScale = j["timeScale"].get<float>();
    if (j.contains("isPaused")) time.isPaused = j["isPaused"].get<bool>();

    return time;
}

bool WorldSerializer::SaveWorldToFile(const std::string& path, const WorldSaveData& data) {
    try {
        // Create directory if needed
        std::filesystem::path filePath(path);
        if (filePath.has_parent_path()) {
            std::filesystem::create_directories(filePath.parent_path());
        }

        nlohmann::json worldJson = SerializeWorld(data);

        std::ofstream file(path);
        if (!file.is_open()) {
            if (m_callbacks.onError) m_callbacks.onError("Failed to open file: " + path);
            return false;
        }

        file << std::setw(2) << worldJson;

        if (m_callbacks.onProgress) m_callbacks.onProgress(1.0f, "World saved");

        return true;
    } catch (const std::exception& e) {
        if (m_callbacks.onError) m_callbacks.onError(std::string("Save failed: ") + e.what());
        return false;
    }
}

bool WorldSerializer::LoadWorldFromFile(const std::string& path, WorldSaveData& data) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            if (m_callbacks.onError) m_callbacks.onError("Failed to open file: " + path);
            return false;
        }

        nlohmann::json worldJson;
        file >> worldJson;

        data = DeserializeWorld(worldJson);

        if (m_callbacks.onProgress) m_callbacks.onProgress(1.0f, "World loaded");

        return true;
    } catch (const std::exception& e) {
        if (m_callbacks.onError) m_callbacks.onError(std::string("Load failed: ") + e.what());
        return false;
    }
}

bool WorldSerializer::SaveChunkToFile(const std::string& basePath, const ChunkSaveData& chunk) {
    std::string filename = basePath + "/" + GetChunkFilename(chunk.x, chunk.z);

    // Use binary format for chunk data
    return SaveChunkBinary(filename, chunk);
}

bool WorldSerializer::LoadChunkFromFile(const std::string& basePath, int32_t x, int32_t z,
                                          ChunkSaveData& chunk) {
    std::string filename = basePath + "/" + GetChunkFilename(x, z);

    return LoadChunkBinary(filename, chunk);
}

bool WorldSerializer::DoesChunkSaveExist(const std::string& basePath, int32_t x, int32_t z) const {
    std::string filename = basePath + "/" + GetChunkFilename(x, z);
    return std::filesystem::exists(filename);
}

std::string WorldSerializer::GetChunkFilename(int32_t x, int32_t z) {
    std::stringstream ss;
    ss << "chunk_" << x << "_" << z << ".bin";
    return ss.str();
}

std::string WorldSerializer::GenerateSaveTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

bool WorldSerializer::SaveChunkBinary(const std::string& path, const ChunkSaveData& chunk) {
    try {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Write header
        uint32_t magic = 0x434E4B44;  // "CNKD"
        uint32_t version = 1;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Write chunk position
        file.write(reinterpret_cast<const char*>(&chunk.x), sizeof(chunk.x));
        file.write(reinterpret_cast<const char*>(&chunk.z), sizeof(chunk.z));
        file.write(reinterpret_cast<const char*>(&chunk.lodLevel), sizeof(chunk.lodLevel));
        file.write(reinterpret_cast<const char*>(&chunk.lastModifiedTime), sizeof(chunk.lastModifiedTime));
        file.write(reinterpret_cast<const char*>(&chunk.creationTime), sizeof(chunk.creationTime));

        // Write height modifications
        uint32_t heightCount = static_cast<uint32_t>(chunk.heightModifications.size());
        file.write(reinterpret_cast<const char*>(&heightCount), sizeof(heightCount));
        if (heightCount > 0) {
            file.write(reinterpret_cast<const char*>(chunk.heightModifications.data()),
                       heightCount * sizeof(float));
        }

        // Write biome overrides
        uint32_t biomeCount = static_cast<uint32_t>(chunk.biomeOverrides.size());
        file.write(reinterpret_cast<const char*>(&biomeCount), sizeof(biomeCount));
        if (biomeCount > 0) {
            file.write(reinterpret_cast<const char*>(chunk.biomeOverrides.data()), biomeCount);
        }

        // Write entity IDs
        uint32_t entityCount = static_cast<uint32_t>(chunk.spawnedEntityIds.size());
        file.write(reinterpret_cast<const char*>(&entityCount), sizeof(entityCount));
        if (entityCount > 0) {
            file.write(reinterpret_cast<const char*>(chunk.spawnedEntityIds.data()),
                       entityCount * sizeof(uint32_t));
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool WorldSerializer::LoadChunkBinary(const std::string& path, ChunkSaveData& chunk) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        // Read and verify header
        uint32_t magic, version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != 0x434E4B44) return false;  // Invalid magic

        // Read chunk position
        file.read(reinterpret_cast<char*>(&chunk.x), sizeof(chunk.x));
        file.read(reinterpret_cast<char*>(&chunk.z), sizeof(chunk.z));
        file.read(reinterpret_cast<char*>(&chunk.lodLevel), sizeof(chunk.lodLevel));
        file.read(reinterpret_cast<char*>(&chunk.lastModifiedTime), sizeof(chunk.lastModifiedTime));
        file.read(reinterpret_cast<char*>(&chunk.creationTime), sizeof(chunk.creationTime));

        // Read height modifications
        uint32_t heightCount;
        file.read(reinterpret_cast<char*>(&heightCount), sizeof(heightCount));
        if (heightCount > 0) {
            chunk.heightModifications.resize(heightCount);
            file.read(reinterpret_cast<char*>(chunk.heightModifications.data()),
                      heightCount * sizeof(float));
        }

        // Read biome overrides
        uint32_t biomeCount;
        file.read(reinterpret_cast<char*>(&biomeCount), sizeof(biomeCount));
        if (biomeCount > 0) {
            chunk.biomeOverrides.resize(biomeCount);
            file.read(reinterpret_cast<char*>(chunk.biomeOverrides.data()), biomeCount);
        }

        // Read entity IDs
        uint32_t entityCount;
        file.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
        if (entityCount > 0) {
            chunk.spawnedEntityIds.resize(entityCount);
            file.read(reinterpret_cast<char*>(chunk.spawnedEntityIds.data()),
                      entityCount * sizeof(uint32_t));
        }

        return true;
    } catch (...) {
        return false;
    }
}

std::vector<uint8_t> WorldSerializer::CompressHeightData(const std::vector<float>& heights) const {
    // Simple delta encoding for now (can use zlib in production)
    std::vector<uint8_t> result;
    result.reserve(heights.size() * sizeof(float));

    for (float h : heights) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&h);
        for (size_t i = 0; i < sizeof(float); i++) {
            result.push_back(bytes[i]);
        }
    }

    return result;
}

std::vector<float> WorldSerializer::DecompressHeightData(const std::vector<uint8_t>& compressed,
                                                          size_t expectedSize) const {
    std::vector<float> result;
    result.reserve(expectedSize);

    for (size_t i = 0; i + sizeof(float) <= compressed.size(); i += sizeof(float)) {
        float h;
        std::memcpy(&h, &compressed[i], sizeof(float));
        result.push_back(h);
    }

    return result;
}

// ============================================================================
// WorldStateManager
// ============================================================================

WorldStateManager::WorldStateManager() {}

WorldSaveData WorldStateManager::CaptureWorldState(ChunkGenerator* chunks,
                                                    BiomeMap* biomes,
                                                    WeatherSystem* weather) const {
    WorldSaveData data;

    // Get dirty chunks
    for (const auto& [key, dirty] : m_dirtyChunks) {
        if (!dirty) continue;

        int32_t x = static_cast<int32_t>(key >> 32);
        int32_t z = static_cast<int32_t>(key & 0xFFFFFFFF);

        ChunkSaveData chunkData;
        chunkData.x = x;
        chunkData.z = z;

        // Get height modifications
        auto it = m_heightModifications.find(key);
        if (it != m_heightModifications.end()) {
            chunkData.heightModifications = it->second;
        }

        data.modifiedChunks.push_back(chunkData);
    }

    // TODO: Capture weather state when WeatherSystem interface is defined
    // TODO: Capture time of day from engine

    data.saveTimestamp = WorldSerializer::GenerateSaveTimestamp();

    return data;
}

void WorldStateManager::ApplyWorldState(const WorldSaveData& data,
                                          ChunkGenerator* chunks,
                                          BiomeMap* biomes,
                                          WeatherSystem* weather) {
    // Apply chunk modifications
    for (const auto& chunk : data.modifiedChunks) {
        uint64_t key = ChunkKey(chunk.x, chunk.z);

        if (!chunk.heightModifications.empty()) {
            m_heightModifications[key] = chunk.heightModifications;
            m_dirtyChunks[key] = true;
        }

        // TODO: Apply to actual chunk generator
    }

    // TODO: Apply weather state
    // TODO: Apply time of day
}

void WorldStateManager::MarkChunkDirty(int32_t x, int32_t z) {
    m_dirtyChunks[ChunkKey(x, z)] = true;
}

bool WorldStateManager::IsChunkDirty(int32_t x, int32_t z) const {
    auto it = m_dirtyChunks.find(ChunkKey(x, z));
    return it != m_dirtyChunks.end() && it->second;
}

std::vector<glm::ivec2> WorldStateManager::GetDirtyChunks() const {
    std::vector<glm::ivec2> result;
    for (const auto& [key, dirty] : m_dirtyChunks) {
        if (dirty) {
            int32_t x = static_cast<int32_t>(key >> 32);
            int32_t z = static_cast<int32_t>(key & 0xFFFFFFFF);
            result.push_back(glm::ivec2(x, z));
        }
    }
    return result;
}

void WorldStateManager::ClearDirtyChunks() {
    for (auto& [key, dirty] : m_dirtyChunks) {
        dirty = false;
    }
}

void WorldStateManager::RecordHeightModification(int32_t chunkX, int32_t chunkZ,
                                                   uint32_t vertexIndex, float delta) {
    uint64_t key = ChunkKey(chunkX, chunkZ);

    auto& mods = m_heightModifications[key];
    if (mods.size() <= vertexIndex) {
        mods.resize(vertexIndex + 1, 0.0f);
    }
    mods[vertexIndex] += delta;

    MarkChunkDirty(chunkX, chunkZ);
}

const std::vector<float>& WorldStateManager::GetHeightModifications(int32_t chunkX, int32_t chunkZ) const {
    static const std::vector<float> empty;
    auto it = m_heightModifications.find(ChunkKey(chunkX, chunkZ));
    return (it != m_heightModifications.end()) ? it->second : empty;
}

} // namespace Cortex::Serialization
