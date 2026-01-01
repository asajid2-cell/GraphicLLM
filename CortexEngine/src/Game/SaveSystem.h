#pragma once

// SaveSystem.h
// Complete save/load system with slots, autosave, quicksave, and checkpoints.
// Supports versioning, integrity checking, and cloud sync preparation.

#include "../Utils/EntitySerializer.h"
#include "../Utils/WorldSerializer.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

namespace Cortex {

// Forward declarations
class ECS_Registry;
class ChunkGenerator;
class BiomeMap;
class WeatherSystem;

namespace Game {

// Save slot info
struct SaveSlotInfo {
    uint32_t slotIndex = 0;
    std::string saveName;
    std::string timestamp;
    float playTime = 0.0f;
    uint32_t version = 0;

    // Preview data
    std::string locationName;
    int32_t level = 0;
    float healthPercent = 1.0f;
    std::string thumbnailPath;

    // Metadata
    std::string filePath;
    size_t fileSizeBytes = 0;
    uint32_t checksum = 0;
    bool isCorrupted = false;
    bool isAutoSave = false;
    bool isQuickSave = false;
};

// Save operation result
struct SaveResult {
    bool success = false;
    std::string errorMessage;
    std::string filePath;
    float elapsedTimeMs = 0.0f;
};

// Load operation result
struct LoadResult {
    bool success = false;
    std::string errorMessage;
    uint32_t loadedVersion = 0;
    float elapsedTimeMs = 0.0f;
};

// Checkpoint data
struct Checkpoint {
    std::string id;
    std::string displayName;
    glm::vec3 respawnPosition = glm::vec3(0.0f);
    glm::vec3 respawnRotation = glm::vec3(0.0f);
    bool isActivated = false;
    float activationTime = 0.0f;

    // Optional conditions
    std::string questRequirement;
    std::vector<std::string> flags;
};

// Save system configuration
struct SaveSystemConfig {
    std::string saveFolderPath = "saves";
    std::string quickSaveName = "quicksave";
    std::string autoSavePrefix = "autosave_";

    uint32_t maxSaveSlots = 20;
    uint32_t maxAutoSaves = 3;           // Rotating autosaves
    float autoSaveIntervalSeconds = 300.0f;  // 5 minutes
    bool autoSaveEnabled = true;

    bool compressSaves = false;
    bool encryptSaves = false;
    bool verifyIntegrity = true;

    // File extension
    std::string saveExtension = ".sav";
    std::string thumbnailExtension = ".png";
};

// Save system callbacks
struct SaveSystemCallbacks {
    // Progress reporting
    std::function<void(float progress, const std::string& status)> onProgress;

    // Save/load events
    std::function<void(const SaveSlotInfo& slot)> onSaveComplete;
    std::function<void(const SaveSlotInfo& slot)> onLoadComplete;
    std::function<void(const std::string& error)> onError;

    // Checkpoint events
    std::function<void(const Checkpoint& checkpoint)> onCheckpointActivated;
    std::function<void(const Checkpoint& checkpoint)> onCheckpointRespawn;

    // Autosave notification
    std::function<void()> onAutoSaveStarting;
    std::function<void(bool success)> onAutoSaveComplete;
};

// Save system
class SaveSystem {
public:
    SaveSystem();
    ~SaveSystem();

    // Initialize with engine systems
    void Initialize(ECS_Registry* registry,
                     ChunkGenerator* chunks = nullptr,
                     BiomeMap* biomes = nullptr,
                     WeatherSystem* weather = nullptr);

    void Shutdown();

    // Configuration
    void SetConfig(const SaveSystemConfig& config) { m_config = config; }
    const SaveSystemConfig& GetConfig() const { return m_config; }

    // Callbacks
    void SetCallbacks(const SaveSystemCallbacks& callbacks) { m_callbacks = callbacks; }

    // Update (for autosave timing)
    void Update(float deltaTime);

    // Save operations
    SaveResult Save(uint32_t slotIndex, const std::string& saveName = "");
    SaveResult QuickSave();
    SaveResult AutoSave();

    // Load operations
    LoadResult Load(uint32_t slotIndex);
    LoadResult LoadQuickSave();
    LoadResult LoadAutoSave(uint32_t autoSaveIndex = 0);
    LoadResult LoadFromFile(const std::string& filePath);

    // Async save/load (non-blocking)
    void SaveAsync(uint32_t slotIndex, const std::string& saveName = "");
    void LoadAsync(uint32_t slotIndex);
    bool IsOperationInProgress() const { return m_operationInProgress; }
    float GetOperationProgress() const { return m_operationProgress; }

    // Slot management
    std::vector<SaveSlotInfo> GetAllSaveSlots() const;
    SaveSlotInfo GetSlotInfo(uint32_t slotIndex) const;
    bool DeleteSave(uint32_t slotIndex);
    bool RenameSave(uint32_t slotIndex, const std::string& newName);
    bool DoesSlotExist(uint32_t slotIndex) const;

    // Quick save management
    bool HasQuickSave() const;
    SaveSlotInfo GetQuickSaveInfo() const;
    bool DeleteQuickSave();

    // Autosave management
    std::vector<SaveSlotInfo> GetAutoSaves() const;
    void SetAutoSaveEnabled(bool enabled);
    bool IsAutoSaveEnabled() const { return m_config.autoSaveEnabled; }
    void ForceAutoSave();
    float GetTimeSinceLastAutoSave() const { return m_timeSinceAutoSave; }
    float GetTimeUntilNextAutoSave() const;

    // Checkpoints
    void RegisterCheckpoint(const Checkpoint& checkpoint);
    void ActivateCheckpoint(const std::string& checkpointId);
    const Checkpoint* GetActiveCheckpoint() const;
    const Checkpoint* GetCheckpoint(const std::string& id) const;
    std::vector<Checkpoint> GetAllCheckpoints() const;
    void RespawnAtCheckpoint();

    // Utility
    std::string GetSaveFilePath(uint32_t slotIndex) const;
    std::string GetQuickSaveFilePath() const;
    std::string GetAutoSaveFilePath(uint32_t index) const;

    // Validation
    bool ValidateSaveFile(const std::string& filePath) const;
    bool RepairSaveFile(const std::string& filePath);

    // Statistics
    size_t GetTotalSaveSize() const;
    uint32_t GetSaveCount() const;
    float GetTotalPlayTime() const { return m_totalPlayTime; }
    void AddPlayTime(float seconds) { m_totalPlayTime += seconds; }

private:
    // Internal save/load implementation
    SaveResult SaveInternal(const std::string& filePath, const std::string& saveName,
                             bool isAutoSave = false, bool isQuickSave = false);
    LoadResult LoadInternal(const std::string& filePath);

    // Helpers
    void CreateSaveDirectory();
    std::string GenerateThumbnailPath(const std::string& savePath) const;
    void CaptureThumbnail(const std::string& thumbnailPath);
    uint32_t CalculateFileChecksum(const std::string& filePath) const;
    SaveSlotInfo ParseSaveFile(const std::string& filePath) const;

    // Async worker
    void AsyncWorkerThread();

    // Engine systems
    ECS_Registry* m_registry = nullptr;
    ChunkGenerator* m_chunks = nullptr;
    BiomeMap* m_biomes = nullptr;
    WeatherSystem* m_weather = nullptr;

    // Serializers
    std::unique_ptr<Serialization::EntitySerializer> m_entitySerializer;
    std::unique_ptr<Serialization::WorldSerializer> m_worldSerializer;
    std::unique_ptr<Serialization::WorldStateManager> m_worldStateManager;

    // Config and callbacks
    SaveSystemConfig m_config;
    SaveSystemCallbacks m_callbacks;

    // Autosave tracking
    float m_timeSinceAutoSave = 0.0f;
    uint32_t m_currentAutoSaveIndex = 0;

    // Play time tracking
    float m_totalPlayTime = 0.0f;

    // Checkpoints
    std::vector<Checkpoint> m_checkpoints;
    std::string m_activeCheckpointId;

    // Async operation state
    std::atomic<bool> m_operationInProgress{false};
    std::atomic<float> m_operationProgress{0.0f};
    std::thread m_asyncThread;
    std::mutex m_saveMutex;

    // Cached slot info
    mutable std::vector<SaveSlotInfo> m_cachedSlots;
    mutable bool m_slotCacheValid = false;
};

// Save/Load guard for RAII-style pause/resume
class SaveLoadGuard {
public:
    SaveLoadGuard();
    ~SaveLoadGuard();

    SaveLoadGuard(const SaveLoadGuard&) = delete;
    SaveLoadGuard& operator=(const SaveLoadGuard&) = delete;

private:
    bool m_wasGamePaused = false;
};

} // namespace Game
} // namespace Cortex
