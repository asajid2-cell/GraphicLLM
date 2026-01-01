#pragma once

// AssetDatabase.h
// Asset management system with metadata tracking, dependency resolution, and hot-reload.
// Provides centralized asset cataloging, caching, and lifecycle management.

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <atomic>

namespace Cortex::Utils {

// Forward declarations
class AssetDatabase;

// Asset type enumeration
enum class AssetType : uint8_t {
    Unknown = 0,
    Texture,
    Mesh,
    Material,
    Shader,
    Audio,
    Animation,
    Prefab,
    Scene,
    Script,
    Font,
    Config
};

// Asset import status
enum class AssetStatus : uint8_t {
    Unknown,
    Pending,
    Importing,
    Ready,
    Error,
    Deleted
};

// Asset metadata
struct AssetMetadata {
    std::string guid;                       // Unique asset identifier
    std::string path;                       // Relative path from asset root
    std::string absolutePath;               // Full filesystem path
    AssetType type = AssetType::Unknown;
    AssetStatus status = AssetStatus::Unknown;

    // File info
    size_t fileSize = 0;
    std::filesystem::file_time_type lastModified;
    uint32_t contentHash = 0;               // Hash of file contents

    // Import settings path (e.g., texture.png.import)
    std::string importSettingsPath;

    // Dependencies (other assets this depends on)
    std::vector<std::string> dependencies;

    // Dependents (assets that depend on this)
    std::vector<std::string> dependents;

    // Cache info
    std::string cachePath;                  // Path to processed/cached version
    bool isCached = false;

    // Labels/tags for organization
    std::vector<std::string> labels;

    // Thumbnail path (for editor)
    std::string thumbnailPath;

    // Error info
    std::string lastError;

    // Timestamps
    std::chrono::system_clock::time_point importTime;
    std::chrono::system_clock::time_point lastAccessTime;
};

// Import settings base class
struct AssetImportSettings {
    virtual ~AssetImportSettings() = default;
    virtual AssetType GetAssetType() const = 0;
    virtual std::string Serialize() const { return "{}"; }
    virtual void Deserialize(const std::string& data) {}
};

// Texture import settings
struct TextureImportSettings : AssetImportSettings {
    bool generateMipmaps = true;
    bool sRGB = true;
    bool compress = true;
    uint32_t maxSize = 4096;
    float compressionQuality = 0.8f;
    std::string textureType = "default";    // default, normal, roughness, etc.

    AssetType GetAssetType() const override { return AssetType::Texture; }
    std::string Serialize() const override;
    void Deserialize(const std::string& data) override;
};

// Mesh import settings
struct MeshImportSettings : AssetImportSettings {
    bool generateLODs = true;
    uint32_t lodLevels = 4;
    bool calculateTangents = true;
    bool weldVertices = true;
    float scale = 1.0f;
    bool importAnimations = true;
    bool importMaterials = true;

    AssetType GetAssetType() const override { return AssetType::Mesh; }
    std::string Serialize() const override;
    void Deserialize(const std::string& data) override;
};

// Asset reference (lightweight handle)
struct AssetRef {
    std::string guid;
    std::string path;               // For display/debugging
    mutable void* cachedPtr = nullptr;  // Cached loaded asset pointer

    bool IsValid() const { return !guid.empty(); }
    bool operator==(const AssetRef& other) const { return guid == other.guid; }
    bool operator!=(const AssetRef& other) const { return guid != other.guid; }
};

// Asset hash for unordered containers
struct AssetRefHash {
    size_t operator()(const AssetRef& ref) const {
        return std::hash<std::string>{}(ref.guid);
    }
};

// Asset change event
struct AssetChangeEvent {
    enum class Type {
        Created,
        Modified,
        Deleted,
        Moved,
        Reimported
    };

    Type type;
    std::string guid;
    std::string path;
    std::string oldPath;            // For moved events
};

// Asset database callbacks
struct AssetDatabaseCallbacks {
    // Asset events
    std::function<void(const AssetChangeEvent&)> onAssetChanged;

    // Import progress
    std::function<void(const std::string& path, float progress)> onImportProgress;

    // Error handling
    std::function<void(const std::string& path, const std::string& error)> onError;
};

// Asset importer interface
class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    // Get supported file extensions
    virtual std::vector<std::string> GetSupportedExtensions() const = 0;

    // Get asset type
    virtual AssetType GetAssetType() const = 0;

    // Create default import settings
    virtual std::unique_ptr<AssetImportSettings> CreateDefaultSettings() const = 0;

    // Import asset
    virtual bool Import(const std::string& sourcePath, const std::string& destPath,
                         const AssetImportSettings* settings) = 0;

    // Generate thumbnail
    virtual bool GenerateThumbnail(const std::string& sourcePath,
                                    const std::string& thumbnailPath) { return false; }

    // Get dependencies from source file
    virtual std::vector<std::string> GetDependencies(const std::string& sourcePath) { return {}; }
};

// Asset database class
class AssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase();

    // Initialization
    bool Initialize(const std::string& assetRootPath, const std::string& cachePath);
    void Shutdown();

    // Set callbacks
    void SetCallbacks(const AssetDatabaseCallbacks& callbacks) { m_callbacks = callbacks; }

    // Register importer
    void RegisterImporter(std::unique_ptr<IAssetImporter> importer);

    // Scan and refresh
    void Refresh();                          // Full rescan
    void RefreshPath(const std::string& path);  // Refresh specific path
    bool IsRefreshing() const { return m_isRefreshing; }
    float GetRefreshProgress() const { return m_refreshProgress; }

    // Asset lookup
    const AssetMetadata* GetAssetByGUID(const std::string& guid) const;
    const AssetMetadata* GetAssetByPath(const std::string& path) const;
    std::vector<const AssetMetadata*> GetAssetsByType(AssetType type) const;
    std::vector<const AssetMetadata*> GetAssetsByLabel(const std::string& label) const;
    std::vector<const AssetMetadata*> SearchAssets(const std::string& query) const;

    // Asset reference creation
    AssetRef CreateRef(const std::string& path) const;
    AssetRef CreateRefByGUID(const std::string& guid) const;

    // Import/reimport
    bool ImportAsset(const std::string& path, const AssetImportSettings* settings = nullptr);
    bool ReimportAsset(const std::string& guid);
    void ReimportAll();

    // Import settings
    std::unique_ptr<AssetImportSettings> GetImportSettings(const std::string& guid) const;
    bool SaveImportSettings(const std::string& guid, const AssetImportSettings& settings);

    // Dependencies
    std::vector<std::string> GetDependencies(const std::string& guid) const;
    std::vector<std::string> GetDependents(const std::string& guid) const;

    // Labels
    void AddLabel(const std::string& guid, const std::string& label);
    void RemoveLabel(const std::string& guid, const std::string& label);
    std::vector<std::string> GetLabels(const std::string& guid) const;

    // File watching (hot-reload)
    void EnableFileWatching(bool enable);
    bool IsFileWatchingEnabled() const { return m_fileWatchingEnabled; }
    void ProcessFileChanges();              // Call from main thread

    // Cache management
    std::string GetCachedAssetPath(const std::string& guid) const;
    bool IsCacheValid(const std::string& guid) const;
    void InvalidateCache(const std::string& guid);
    void ClearCache();
    size_t GetCacheSize() const;

    // Utility
    std::string GenerateGUID() const;
    static AssetType GetAssetTypeFromExtension(const std::string& extension);
    static std::string GetAssetTypeName(AssetType type);

    // Statistics
    uint32_t GetAssetCount() const;
    uint32_t GetAssetCount(AssetType type) const;
    size_t GetTotalAssetSize() const;

    // Paths
    const std::string& GetAssetRootPath() const { return m_assetRootPath; }
    const std::string& GetCachePath() const { return m_cachePath; }
    std::string GetRelativePath(const std::string& absolutePath) const;
    std::string GetAbsolutePath(const std::string& relativePath) const;

private:
    // Internal methods
    void ScanDirectory(const std::filesystem::path& dir);
    void ProcessAssetFile(const std::filesystem::path& filePath);
    void LoadDatabase();
    void SaveDatabase();
    void UpdateDependencies(const std::string& guid);
    IAssetImporter* GetImporterForExtension(const std::string& extension) const;
    uint32_t CalculateFileHash(const std::string& path) const;

    // File watching
    void FileWatchThread();
    void QueueFileChange(const std::string& path, AssetChangeEvent::Type type);

    // Paths
    std::string m_assetRootPath;
    std::string m_cachePath;
    std::string m_databasePath;

    // Asset storage
    std::unordered_map<std::string, AssetMetadata> m_assetsByGUID;
    std::unordered_map<std::string, std::string> m_pathToGUID;

    // Importers
    std::vector<std::unique_ptr<IAssetImporter>> m_importers;
    std::unordered_map<std::string, IAssetImporter*> m_extensionToImporter;

    // Callbacks
    AssetDatabaseCallbacks m_callbacks;

    // State
    std::atomic<bool> m_isRefreshing{false};
    std::atomic<float> m_refreshProgress{0.0f};

    // File watching
    bool m_fileWatchingEnabled = false;
    std::atomic<bool> m_watcherRunning{false};
    std::thread m_watcherThread;
    std::mutex m_pendingChangesMutex;
    std::vector<std::pair<std::string, AssetChangeEvent::Type>> m_pendingChanges;

    // Thread safety
    mutable std::mutex m_mutex;
};

// Asset loader utilities
namespace AssetLoaderUtils {

// Get all files with extension in directory
std::vector<std::string> GetFilesWithExtension(const std::string& directory,
                                                 const std::string& extension,
                                                 bool recursive = true);

// Get all supported asset files in directory
std::vector<std::string> GetAssetFiles(const std::string& directory, bool recursive = true);

// Validate asset path
bool IsValidAssetPath(const std::string& path);

// Normalize path separators
std::string NormalizePath(const std::string& path);

// Get unique filename (append number if exists)
std::string GetUniqueFilename(const std::string& basePath);

} // namespace AssetLoaderUtils

} // namespace Cortex::Utils
