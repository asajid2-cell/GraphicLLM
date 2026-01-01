// SaveSystem.cpp
// Implementation of the save/load system.

#include "SaveSystem.h"
#include "../Scene/ECS_Registry.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace Cortex::Game {

// ============================================================================
// SaveSystem
// ============================================================================

SaveSystem::SaveSystem()
    : m_entitySerializer(std::make_unique<Serialization::EntitySerializer>())
    , m_worldSerializer(std::make_unique<Serialization::WorldSerializer>())
    , m_worldStateManager(std::make_unique<Serialization::WorldStateManager>()) {
}

SaveSystem::~SaveSystem() {
    Shutdown();
}

void SaveSystem::Initialize(ECS_Registry* registry,
                             ChunkGenerator* chunks,
                             BiomeMap* biomes,
                             WeatherSystem* weather) {
    m_registry = registry;
    m_chunks = chunks;
    m_biomes = biomes;
    m_weather = weather;

    CreateSaveDirectory();
    m_slotCacheValid = false;
}

void SaveSystem::Shutdown() {
    // Wait for any async operation to complete
    if (m_asyncThread.joinable()) {
        m_asyncThread.join();
    }
}

void SaveSystem::Update(float deltaTime) {
    // Track play time
    m_totalPlayTime += deltaTime;

    // Autosave logic
    if (m_config.autoSaveEnabled && !m_operationInProgress) {
        m_timeSinceAutoSave += deltaTime;

        if (m_timeSinceAutoSave >= m_config.autoSaveIntervalSeconds) {
            ForceAutoSave();
        }
    }
}

SaveResult SaveSystem::Save(uint32_t slotIndex, const std::string& saveName) {
    std::string filePath = GetSaveFilePath(slotIndex);
    return SaveInternal(filePath, saveName.empty() ? "Save " + std::to_string(slotIndex) : saveName);
}

SaveResult SaveSystem::QuickSave() {
    std::string filePath = GetQuickSaveFilePath();
    return SaveInternal(filePath, "Quick Save", false, true);
}

SaveResult SaveSystem::AutoSave() {
    // Rotate autosave index
    m_currentAutoSaveIndex = (m_currentAutoSaveIndex + 1) % m_config.maxAutoSaves;
    std::string filePath = GetAutoSaveFilePath(m_currentAutoSaveIndex);
    return SaveInternal(filePath, "Auto Save", true, false);
}

SaveResult SaveSystem::SaveInternal(const std::string& filePath,
                                      const std::string& saveName,
                                      bool isAutoSave, bool isQuickSave) {
    SaveResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(m_saveMutex);

    try {
        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.0f, "Preparing save...");
        }

        // Create save data structure
        nlohmann::json saveJson;
        saveJson["version"] = Serialization::SERIALIZATION_VERSION;
        saveJson["name"] = saveName;
        saveJson["isAutoSave"] = isAutoSave;
        saveJson["isQuickSave"] = isQuickSave;
        saveJson["playTime"] = m_totalPlayTime;

        // Generate timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        saveJson["timestamp"] = ss.str();

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.2f, "Saving entities...");
        }

        // Serialize entities
        if (m_registry) {
            Serialization::SerializationContext ctx;
            ctx.basePath = m_config.saveFolderPath;
            saveJson["entities"] = m_entitySerializer->SerializeScene(m_registry, ctx);
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.5f, "Saving world state...");
        }

        // Serialize world state
        Serialization::WorldSaveData worldData = m_worldStateManager->CaptureWorldState(
            m_chunks, m_biomes, m_weather);
        saveJson["world"] = m_worldSerializer->SerializeWorld(worldData);

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.7f, "Saving checkpoints...");
        }

        // Save checkpoint state
        nlohmann::json checkpointsJson = nlohmann::json::array();
        for (const auto& cp : m_checkpoints) {
            if (cp.isActivated) {
                checkpointsJson.push_back({
                    {"id", cp.id},
                    {"activationTime", cp.activationTime}
                });
            }
        }
        saveJson["checkpoints"] = checkpointsJson;
        saveJson["activeCheckpoint"] = m_activeCheckpointId;

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.8f, "Writing file...");
        }

        // Write to file
        CreateSaveDirectory();
        std::ofstream file(filePath);
        if (!file.is_open()) {
            result.errorMessage = "Failed to open file for writing: " + filePath;
            if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
            return result;
        }

        file << std::setw(2) << saveJson;
        file.close();

        // Calculate checksum
        uint32_t checksum = CalculateFileChecksum(filePath);

        // Append checksum to file
        std::ofstream appendFile(filePath, std::ios::app);
        appendFile << "\n// Checksum: " << std::hex << checksum;
        appendFile.close();

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.9f, "Capturing thumbnail...");
        }

        // Capture thumbnail
        std::string thumbnailPath = GenerateThumbnailPath(filePath);
        CaptureThumbnail(thumbnailPath);

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(1.0f, "Save complete");
        }

        // Invalidate cache
        m_slotCacheValid = false;

        // Build result
        result.success = true;
        result.filePath = filePath;

        auto endTime = std::chrono::high_resolution_clock::now();
        result.elapsedTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Fire callback
        if (m_callbacks.onSaveComplete) {
            m_callbacks.onSaveComplete(ParseSaveFile(filePath));
        }

        // Reset autosave timer on successful manual save
        if (!isAutoSave) {
            m_timeSinceAutoSave = 0.0f;
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Save failed: ") + e.what();
        if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
    }

    return result;
}

LoadResult SaveSystem::Load(uint32_t slotIndex) {
    std::string filePath = GetSaveFilePath(slotIndex);
    return LoadInternal(filePath);
}

LoadResult SaveSystem::LoadQuickSave() {
    return LoadInternal(GetQuickSaveFilePath());
}

LoadResult SaveSystem::LoadAutoSave(uint32_t autoSaveIndex) {
    return LoadInternal(GetAutoSaveFilePath(autoSaveIndex));
}

LoadResult SaveSystem::LoadFromFile(const std::string& filePath) {
    return LoadInternal(filePath);
}

LoadResult SaveSystem::LoadInternal(const std::string& filePath) {
    LoadResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::lock_guard<std::mutex> lock(m_saveMutex);

    try {
        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.0f, "Loading save file...");
        }

        // Check file exists
        if (!std::filesystem::exists(filePath)) {
            result.errorMessage = "Save file not found: " + filePath;
            if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
            return result;
        }

        // Validate file
        if (m_config.verifyIntegrity && !ValidateSaveFile(filePath)) {
            result.errorMessage = "Save file is corrupted: " + filePath;
            if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
            return result;
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.1f, "Parsing save data...");
        }

        // Read file
        std::ifstream file(filePath);
        if (!file.is_open()) {
            result.errorMessage = "Failed to open file: " + filePath;
            if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
            return result;
        }

        // Read JSON (stop before checksum comment)
        std::stringstream buffer;
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("// Checksum:") != std::string::npos) break;
            buffer << line << "\n";
        }

        nlohmann::json saveJson = nlohmann::json::parse(buffer.str());

        // Check version
        result.loadedVersion = saveJson.value("version", 0u);
        if (result.loadedVersion > Serialization::SERIALIZATION_VERSION) {
            result.errorMessage = "Save file version is newer than supported";
            if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
            return result;
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.3f, "Loading entities...");
        }

        // Load entities
        if (m_registry && saveJson.contains("entities")) {
            // Clear existing entities (optional, could merge instead)
            m_registry->Clear();

            Serialization::DeserializationContext ctx;
            ctx.basePath = m_config.saveFolderPath;
            m_entitySerializer->DeserializeScene(m_registry, saveJson["entities"], ctx);
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.6f, "Loading world state...");
        }

        // Load world state
        if (saveJson.contains("world")) {
            Serialization::WorldSaveData worldData = m_worldSerializer->DeserializeWorld(saveJson["world"]);
            m_worldStateManager->ApplyWorldState(worldData, m_chunks, m_biomes, m_weather);
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(0.8f, "Loading checkpoints...");
        }

        // Load checkpoint state
        if (saveJson.contains("checkpoints")) {
            for (const auto& cpJson : saveJson["checkpoints"]) {
                std::string id = cpJson.value("id", "");
                float activationTime = cpJson.value("activationTime", 0.0f);

                for (auto& cp : m_checkpoints) {
                    if (cp.id == id) {
                        cp.isActivated = true;
                        cp.activationTime = activationTime;
                        break;
                    }
                }
            }
        }

        if (saveJson.contains("activeCheckpoint")) {
            m_activeCheckpointId = saveJson["activeCheckpoint"].get<std::string>();
        }

        // Restore play time
        if (saveJson.contains("playTime")) {
            m_totalPlayTime = saveJson["playTime"].get<float>();
        }

        if (m_callbacks.onProgress) {
            m_callbacks.onProgress(1.0f, "Load complete");
        }

        result.success = true;

        auto endTime = std::chrono::high_resolution_clock::now();
        result.elapsedTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Fire callback
        if (m_callbacks.onLoadComplete) {
            m_callbacks.onLoadComplete(ParseSaveFile(filePath));
        }

    } catch (const std::exception& e) {
        result.errorMessage = std::string("Load failed: ") + e.what();
        if (m_callbacks.onError) m_callbacks.onError(result.errorMessage);
    }

    return result;
}

void SaveSystem::SaveAsync(uint32_t slotIndex, const std::string& saveName) {
    if (m_operationInProgress) return;

    m_operationInProgress = true;
    m_operationProgress = 0.0f;

    // Launch async thread
    if (m_asyncThread.joinable()) {
        m_asyncThread.join();
    }

    m_asyncThread = std::thread([this, slotIndex, saveName]() {
        Save(slotIndex, saveName);
        m_operationInProgress = false;
    });
}

void SaveSystem::LoadAsync(uint32_t slotIndex) {
    if (m_operationInProgress) return;

    m_operationInProgress = true;
    m_operationProgress = 0.0f;

    if (m_asyncThread.joinable()) {
        m_asyncThread.join();
    }

    m_asyncThread = std::thread([this, slotIndex]() {
        Load(slotIndex);
        m_operationInProgress = false;
    });
}

std::vector<SaveSlotInfo> SaveSystem::GetAllSaveSlots() const {
    if (m_slotCacheValid) {
        return m_cachedSlots;
    }

    m_cachedSlots.clear();

    std::filesystem::path saveDir(m_config.saveFolderPath);
    if (!std::filesystem::exists(saveDir)) {
        return m_cachedSlots;
    }

    for (const auto& entry : std::filesystem::directory_iterator(saveDir)) {
        if (entry.path().extension() == m_config.saveExtension) {
            try {
                SaveSlotInfo info = ParseSaveFile(entry.path().string());
                m_cachedSlots.push_back(info);
            } catch (...) {
                // Skip corrupted files
            }
        }
    }

    // Sort by timestamp (newest first)
    std::sort(m_cachedSlots.begin(), m_cachedSlots.end(),
              [](const SaveSlotInfo& a, const SaveSlotInfo& b) {
                  return a.timestamp > b.timestamp;
              });

    m_slotCacheValid = true;
    return m_cachedSlots;
}

SaveSlotInfo SaveSystem::GetSlotInfo(uint32_t slotIndex) const {
    std::string filePath = GetSaveFilePath(slotIndex);
    if (!std::filesystem::exists(filePath)) {
        return SaveSlotInfo{};
    }
    return ParseSaveFile(filePath);
}

bool SaveSystem::DeleteSave(uint32_t slotIndex) {
    std::string filePath = GetSaveFilePath(slotIndex);
    std::string thumbnailPath = GenerateThumbnailPath(filePath);

    bool deleted = false;
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        deleted = true;
    }
    if (std::filesystem::exists(thumbnailPath)) {
        std::filesystem::remove(thumbnailPath);
    }

    m_slotCacheValid = false;
    return deleted;
}

bool SaveSystem::RenameSave(uint32_t slotIndex, const std::string& newName) {
    std::string filePath = GetSaveFilePath(slotIndex);
    if (!std::filesystem::exists(filePath)) {
        return false;
    }

    try {
        // Read existing save
        std::ifstream file(filePath);
        std::stringstream buffer;
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("// Checksum:") != std::string::npos) break;
            buffer << line << "\n";
        }
        file.close();

        nlohmann::json saveJson = nlohmann::json::parse(buffer.str());
        saveJson["name"] = newName;

        // Write back
        std::ofstream outFile(filePath);
        outFile << std::setw(2) << saveJson;
        outFile.close();

        m_slotCacheValid = false;
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveSystem::DoesSlotExist(uint32_t slotIndex) const {
    return std::filesystem::exists(GetSaveFilePath(slotIndex));
}

bool SaveSystem::HasQuickSave() const {
    return std::filesystem::exists(GetQuickSaveFilePath());
}

SaveSlotInfo SaveSystem::GetQuickSaveInfo() const {
    std::string filePath = GetQuickSaveFilePath();
    if (!std::filesystem::exists(filePath)) {
        return SaveSlotInfo{};
    }
    return ParseSaveFile(filePath);
}

bool SaveSystem::DeleteQuickSave() {
    std::string filePath = GetQuickSaveFilePath();
    if (std::filesystem::exists(filePath)) {
        std::filesystem::remove(filePath);
        std::filesystem::remove(GenerateThumbnailPath(filePath));
        m_slotCacheValid = false;
        return true;
    }
    return false;
}

std::vector<SaveSlotInfo> SaveSystem::GetAutoSaves() const {
    std::vector<SaveSlotInfo> autoSaves;
    for (uint32_t i = 0; i < m_config.maxAutoSaves; i++) {
        std::string filePath = GetAutoSaveFilePath(i);
        if (std::filesystem::exists(filePath)) {
            autoSaves.push_back(ParseSaveFile(filePath));
        }
    }
    return autoSaves;
}

void SaveSystem::SetAutoSaveEnabled(bool enabled) {
    m_config.autoSaveEnabled = enabled;
}

void SaveSystem::ForceAutoSave() {
    if (m_operationInProgress) return;

    if (m_callbacks.onAutoSaveStarting) {
        m_callbacks.onAutoSaveStarting();
    }

    SaveResult result = AutoSave();
    m_timeSinceAutoSave = 0.0f;

    if (m_callbacks.onAutoSaveComplete) {
        m_callbacks.onAutoSaveComplete(result.success);
    }
}

float SaveSystem::GetTimeUntilNextAutoSave() const {
    return std::max(0.0f, m_config.autoSaveIntervalSeconds - m_timeSinceAutoSave);
}

void SaveSystem::RegisterCheckpoint(const Checkpoint& checkpoint) {
    // Check if already registered
    for (auto& cp : m_checkpoints) {
        if (cp.id == checkpoint.id) {
            cp = checkpoint;
            return;
        }
    }
    m_checkpoints.push_back(checkpoint);
}

void SaveSystem::ActivateCheckpoint(const std::string& checkpointId) {
    for (auto& cp : m_checkpoints) {
        if (cp.id == checkpointId) {
            cp.isActivated = true;
            cp.activationTime = m_totalPlayTime;
            m_activeCheckpointId = checkpointId;

            if (m_callbacks.onCheckpointActivated) {
                m_callbacks.onCheckpointActivated(cp);
            }
            break;
        }
    }
}

const Checkpoint* SaveSystem::GetActiveCheckpoint() const {
    return GetCheckpoint(m_activeCheckpointId);
}

const Checkpoint* SaveSystem::GetCheckpoint(const std::string& id) const {
    for (const auto& cp : m_checkpoints) {
        if (cp.id == id) {
            return &cp;
        }
    }
    return nullptr;
}

std::vector<Checkpoint> SaveSystem::GetAllCheckpoints() const {
    return m_checkpoints;
}

void SaveSystem::RespawnAtCheckpoint() {
    const Checkpoint* cp = GetActiveCheckpoint();
    if (!cp) return;

    // TODO: Teleport player to checkpoint position
    // This would require access to player entity

    if (m_callbacks.onCheckpointRespawn) {
        m_callbacks.onCheckpointRespawn(*cp);
    }
}

std::string SaveSystem::GetSaveFilePath(uint32_t slotIndex) const {
    return m_config.saveFolderPath + "/save_" + std::to_string(slotIndex) + m_config.saveExtension;
}

std::string SaveSystem::GetQuickSaveFilePath() const {
    return m_config.saveFolderPath + "/" + m_config.quickSaveName + m_config.saveExtension;
}

std::string SaveSystem::GetAutoSaveFilePath(uint32_t index) const {
    return m_config.saveFolderPath + "/" + m_config.autoSavePrefix + std::to_string(index) + m_config.saveExtension;
}

bool SaveSystem::ValidateSaveFile(const std::string& filePath) const {
    try {
        std::ifstream file(filePath);
        if (!file.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Find checksum line
        size_t checksumPos = content.rfind("// Checksum:");
        if (checksumPos == std::string::npos) return false;

        // Extract stored checksum
        std::string checksumLine = content.substr(checksumPos + 13);
        uint32_t storedChecksum = std::stoul(checksumLine, nullptr, 16);

        // Calculate checksum of content before checksum line
        std::string dataContent = content.substr(0, checksumPos);
        uint32_t calculatedChecksum = 0;
        for (char c : dataContent) {
            calculatedChecksum ^= static_cast<uint8_t>(c);
            calculatedChecksum *= 16777619u;
        }

        return storedChecksum == calculatedChecksum;
    } catch (...) {
        return false;
    }
}

bool SaveSystem::RepairSaveFile(const std::string& filePath) {
    // Try to recover what we can from a corrupted save
    // For now, just verify JSON parsing works
    try {
        std::ifstream file(filePath);
        nlohmann::json j = nlohmann::json::parse(file);
        return j.contains("version");
    } catch (...) {
        return false;
    }
}

size_t SaveSystem::GetTotalSaveSize() const {
    size_t total = 0;
    std::filesystem::path saveDir(m_config.saveFolderPath);
    if (std::filesystem::exists(saveDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(saveDir)) {
            total += entry.file_size();
        }
    }
    return total;
}

uint32_t SaveSystem::GetSaveCount() const {
    return static_cast<uint32_t>(GetAllSaveSlots().size());
}

void SaveSystem::CreateSaveDirectory() {
    std::filesystem::path saveDir(m_config.saveFolderPath);
    if (!std::filesystem::exists(saveDir)) {
        std::filesystem::create_directories(saveDir);
    }
}

std::string SaveSystem::GenerateThumbnailPath(const std::string& savePath) const {
    std::filesystem::path p(savePath);
    return p.replace_extension(m_config.thumbnailExtension).string();
}

void SaveSystem::CaptureThumbnail(const std::string& thumbnailPath) {
    // TODO: Implement screenshot capture and save to file
    // This would require access to the renderer
}

uint32_t SaveSystem::CalculateFileChecksum(const std::string& filePath) const {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return 0;

    uint32_t checksum = 2166136261u;  // FNV-1a offset basis
    char c;
    while (file.get(c)) {
        checksum ^= static_cast<uint8_t>(c);
        checksum *= 16777619u;  // FNV-1a prime
    }

    return checksum;
}

SaveSlotInfo SaveSystem::ParseSaveFile(const std::string& filePath) const {
    SaveSlotInfo info;
    info.filePath = filePath;

    try {
        std::ifstream file(filePath);
        std::stringstream buffer;
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("// Checksum:") != std::string::npos) break;
            buffer << line << "\n";
        }

        nlohmann::json j = nlohmann::json::parse(buffer.str());

        info.saveName = j.value("name", "Unnamed Save");
        info.timestamp = j.value("timestamp", "");
        info.playTime = j.value("playTime", 0.0f);
        info.version = j.value("version", 0u);
        info.isAutoSave = j.value("isAutoSave", false);
        info.isQuickSave = j.value("isQuickSave", false);

        info.fileSizeBytes = std::filesystem::file_size(filePath);

    } catch (...) {
        info.isCorrupted = true;
    }

    return info;
}

// ============================================================================
// SaveLoadGuard
// ============================================================================

SaveLoadGuard::SaveLoadGuard() {
    // TODO: Pause game and store state
    m_wasGamePaused = false;
}

SaveLoadGuard::~SaveLoadGuard() {
    // TODO: Restore game state if it wasn't paused before
}

} // namespace Cortex::Game
