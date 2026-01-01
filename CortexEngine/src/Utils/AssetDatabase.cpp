// AssetDatabase.cpp
// Implementation of the asset management system.

#include "AssetDatabase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <iomanip>

namespace Cortex::Utils {

// ============================================================================
// Import Settings Serialization
// ============================================================================

std::string TextureImportSettings::Serialize() const {
    nlohmann::json j;
    j["generateMipmaps"] = generateMipmaps;
    j["sRGB"] = sRGB;
    j["compress"] = compress;
    j["maxSize"] = maxSize;
    j["compressionQuality"] = compressionQuality;
    j["textureType"] = textureType;
    return j.dump();
}

void TextureImportSettings::Deserialize(const std::string& data) {
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        if (j.contains("generateMipmaps")) generateMipmaps = j["generateMipmaps"];
        if (j.contains("sRGB")) sRGB = j["sRGB"];
        if (j.contains("compress")) compress = j["compress"];
        if (j.contains("maxSize")) maxSize = j["maxSize"];
        if (j.contains("compressionQuality")) compressionQuality = j["compressionQuality"];
        if (j.contains("textureType")) textureType = j["textureType"];
    } catch (...) {}
}

std::string MeshImportSettings::Serialize() const {
    nlohmann::json j;
    j["generateLODs"] = generateLODs;
    j["lodLevels"] = lodLevels;
    j["calculateTangents"] = calculateTangents;
    j["weldVertices"] = weldVertices;
    j["scale"] = scale;
    j["importAnimations"] = importAnimations;
    j["importMaterials"] = importMaterials;
    return j.dump();
}

void MeshImportSettings::Deserialize(const std::string& data) {
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        if (j.contains("generateLODs")) generateLODs = j["generateLODs"];
        if (j.contains("lodLevels")) lodLevels = j["lodLevels"];
        if (j.contains("calculateTangents")) calculateTangents = j["calculateTangents"];
        if (j.contains("weldVertices")) weldVertices = j["weldVertices"];
        if (j.contains("scale")) scale = j["scale"];
        if (j.contains("importAnimations")) importAnimations = j["importAnimations"];
        if (j.contains("importMaterials")) importMaterials = j["importMaterials"];
    } catch (...) {}
}

// ============================================================================
// AssetDatabase
// ============================================================================

AssetDatabase::AssetDatabase() {}

AssetDatabase::~AssetDatabase() {
    Shutdown();
}

bool AssetDatabase::Initialize(const std::string& assetRootPath, const std::string& cachePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_assetRootPath = AssetLoaderUtils::NormalizePath(assetRootPath);
    m_cachePath = AssetLoaderUtils::NormalizePath(cachePath);
    m_databasePath = m_cachePath + "/asset_database.json";

    // Create directories if needed
    std::filesystem::create_directories(m_assetRootPath);
    std::filesystem::create_directories(m_cachePath);

    // Load existing database
    LoadDatabase();

    return true;
}

void AssetDatabase::Shutdown() {
    // Stop file watcher
    m_watcherRunning = false;
    if (m_watcherThread.joinable()) {
        m_watcherThread.join();
    }

    // Save database
    SaveDatabase();
}

void AssetDatabase::RegisterImporter(std::unique_ptr<IAssetImporter> importer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& ext : importer->GetSupportedExtensions()) {
        m_extensionToImporter[ext] = importer.get();
    }
    m_importers.push_back(std::move(importer));
}

void AssetDatabase::Refresh() {
    m_isRefreshing = true;
    m_refreshProgress = 0.0f;

    // Clear and rescan
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Don't clear existing assets, just update
    }

    ScanDirectory(m_assetRootPath);

    // Update dependencies
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [guid, metadata] : m_assetsByGUID) {
            UpdateDependencies(guid);
        }
    }

    SaveDatabase();

    m_refreshProgress = 1.0f;
    m_isRefreshing = false;
}

void AssetDatabase::RefreshPath(const std::string& path) {
    std::filesystem::path fsPath(GetAbsolutePath(path));

    if (std::filesystem::is_directory(fsPath)) {
        ScanDirectory(fsPath);
    } else if (std::filesystem::exists(fsPath)) {
        ProcessAssetFile(fsPath);
    }
}

void AssetDatabase::ScanDirectory(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) return;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }

    for (size_t i = 0; i < files.size(); i++) {
        ProcessAssetFile(files[i]);
        m_refreshProgress = static_cast<float>(i + 1) / files.size();
    }
}

void AssetDatabase::ProcessAssetFile(const std::filesystem::path& filePath) {
    std::string extension = filePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    // Skip meta/import settings files
    if (extension == ".import" || extension == ".meta") {
        return;
    }

    AssetType type = GetAssetTypeFromExtension(extension);
    if (type == AssetType::Unknown) {
        return;  // Not a recognized asset type
    }

    std::string relativePath = GetRelativePath(filePath.string());
    std::string absolutePath = filePath.string();

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if asset already exists
    auto pathIt = m_pathToGUID.find(relativePath);
    if (pathIt != m_pathToGUID.end()) {
        // Update existing asset
        auto& metadata = m_assetsByGUID[pathIt->second];

        auto lastMod = std::filesystem::last_write_time(filePath);
        if (lastMod != metadata.lastModified) {
            // File changed
            metadata.lastModified = lastMod;
            metadata.fileSize = std::filesystem::file_size(filePath);
            metadata.contentHash = CalculateFileHash(absolutePath);
            metadata.status = AssetStatus::Pending;

            if (m_callbacks.onAssetChanged) {
                AssetChangeEvent event;
                event.type = AssetChangeEvent::Type::Modified;
                event.guid = metadata.guid;
                event.path = relativePath;
                m_callbacks.onAssetChanged(event);
            }
        }
        return;
    }

    // Create new asset entry
    AssetMetadata metadata;
    metadata.guid = GenerateGUID();
    metadata.path = relativePath;
    metadata.absolutePath = absolutePath;
    metadata.type = type;
    metadata.status = AssetStatus::Pending;
    metadata.fileSize = std::filesystem::file_size(filePath);
    metadata.lastModified = std::filesystem::last_write_time(filePath);
    metadata.contentHash = CalculateFileHash(absolutePath);
    metadata.importSettingsPath = absolutePath + ".import";
    metadata.cachePath = m_cachePath + "/" + metadata.guid;

    m_assetsByGUID[metadata.guid] = metadata;
    m_pathToGUID[relativePath] = metadata.guid;

    if (m_callbacks.onAssetChanged) {
        AssetChangeEvent event;
        event.type = AssetChangeEvent::Type::Created;
        event.guid = metadata.guid;
        event.path = relativePath;
        m_callbacks.onAssetChanged(event);
    }
}

const AssetMetadata* AssetDatabase::GetAssetByGUID(const std::string& guid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_assetsByGUID.find(guid);
    return (it != m_assetsByGUID.end()) ? &it->second : nullptr;
}

const AssetMetadata* AssetDatabase::GetAssetByPath(const std::string& path) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pathToGUID.find(path);
    if (it != m_pathToGUID.end()) {
        auto assetIt = m_assetsByGUID.find(it->second);
        return (assetIt != m_assetsByGUID.end()) ? &assetIt->second : nullptr;
    }
    return nullptr;
}

std::vector<const AssetMetadata*> AssetDatabase::GetAssetsByType(AssetType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetMetadata*> result;

    for (const auto& [guid, metadata] : m_assetsByGUID) {
        if (metadata.type == type) {
            result.push_back(&metadata);
        }
    }

    return result;
}

std::vector<const AssetMetadata*> AssetDatabase::GetAssetsByLabel(const std::string& label) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetMetadata*> result;

    for (const auto& [guid, metadata] : m_assetsByGUID) {
        if (std::find(metadata.labels.begin(), metadata.labels.end(), label) != metadata.labels.end()) {
            result.push_back(&metadata);
        }
    }

    return result;
}

std::vector<const AssetMetadata*> AssetDatabase::SearchAssets(const std::string& query) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const AssetMetadata*> result;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

    for (const auto& [guid, metadata] : m_assetsByGUID) {
        std::string lowerPath = metadata.path;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

        if (lowerPath.find(lowerQuery) != std::string::npos) {
            result.push_back(&metadata);
        }
    }

    return result;
}

AssetRef AssetDatabase::CreateRef(const std::string& path) const {
    AssetRef ref;
    ref.path = path;

    const AssetMetadata* metadata = GetAssetByPath(path);
    if (metadata) {
        ref.guid = metadata->guid;
    }

    return ref;
}

AssetRef AssetDatabase::CreateRefByGUID(const std::string& guid) const {
    AssetRef ref;
    ref.guid = guid;

    const AssetMetadata* metadata = GetAssetByGUID(guid);
    if (metadata) {
        ref.path = metadata->path;
    }

    return ref;
}

bool AssetDatabase::ImportAsset(const std::string& path, const AssetImportSettings* settings) {
    const AssetMetadata* metadata = GetAssetByPath(path);
    if (!metadata) {
        // Try to find by absolute path
        ProcessAssetFile(GetAbsolutePath(path));
        metadata = GetAssetByPath(path);
        if (!metadata) return false;
    }

    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    IAssetImporter* importer = GetImporterForExtension(extension);
    if (!importer) {
        return false;
    }

    // Get or create import settings
    std::unique_ptr<AssetImportSettings> defaultSettings;
    if (!settings) {
        defaultSettings = importer->CreateDefaultSettings();
        settings = defaultSettings.get();
    }

    // Update status
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assetsByGUID[metadata->guid].status = AssetStatus::Importing;
    }

    // Run import
    bool success = importer->Import(metadata->absolutePath, metadata->cachePath, settings);

    // Update status
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& asset = m_assetsByGUID[metadata->guid];
        asset.status = success ? AssetStatus::Ready : AssetStatus::Error;
        asset.isCached = success;
        asset.importTime = std::chrono::system_clock::now();

        if (!success) {
            asset.lastError = "Import failed";
            if (m_callbacks.onError) {
                m_callbacks.onError(path, asset.lastError);
            }
        }
    }

    // Generate thumbnail
    if (success) {
        std::string thumbnailPath = m_cachePath + "/thumbnails/" + metadata->guid + ".png";
        std::filesystem::create_directories(std::filesystem::path(thumbnailPath).parent_path());
        importer->GenerateThumbnail(metadata->absolutePath, thumbnailPath);
    }

    // Update dependencies
    UpdateDependencies(metadata->guid);

    // Fire event
    if (m_callbacks.onAssetChanged) {
        AssetChangeEvent event;
        event.type = AssetChangeEvent::Type::Reimported;
        event.guid = metadata->guid;
        event.path = path;
        m_callbacks.onAssetChanged(event);
    }

    return success;
}

bool AssetDatabase::ReimportAsset(const std::string& guid) {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    if (!metadata) return false;

    // Load saved import settings
    auto settings = GetImportSettings(guid);

    return ImportAsset(metadata->path, settings.get());
}

void AssetDatabase::ReimportAll() {
    std::vector<std::string> guids;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [guid, metadata] : m_assetsByGUID) {
            guids.push_back(guid);
        }
    }

    for (const auto& guid : guids) {
        ReimportAsset(guid);
    }
}

std::unique_ptr<AssetImportSettings> AssetDatabase::GetImportSettings(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    if (!metadata) return nullptr;

    // Check for import settings file
    if (std::filesystem::exists(metadata->importSettingsPath)) {
        std::ifstream file(metadata->importSettingsPath);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Create appropriate settings based on asset type
        std::unique_ptr<AssetImportSettings> settings;
        switch (metadata->type) {
            case AssetType::Texture:
                settings = std::make_unique<TextureImportSettings>();
                break;
            case AssetType::Mesh:
                settings = std::make_unique<MeshImportSettings>();
                break;
            default:
                return nullptr;
        }

        if (settings) {
            settings->Deserialize(content);
        }
        return settings;
    }

    // Return default settings
    std::string extension = std::filesystem::path(metadata->path).extension().string();
    IAssetImporter* importer = GetImporterForExtension(extension);
    if (importer) {
        return importer->CreateDefaultSettings();
    }

    return nullptr;
}

bool AssetDatabase::SaveImportSettings(const std::string& guid, const AssetImportSettings& settings) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_assetsByGUID.find(guid);
    if (it == m_assetsByGUID.end()) return false;

    std::ofstream file(it->second.importSettingsPath);
    if (!file.is_open()) return false;

    file << settings.Serialize();
    return true;
}

std::vector<std::string> AssetDatabase::GetDependencies(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    return metadata ? metadata->dependencies : std::vector<std::string>();
}

std::vector<std::string> AssetDatabase::GetDependents(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    return metadata ? metadata->dependents : std::vector<std::string>();
}

void AssetDatabase::AddLabel(const std::string& guid, const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_assetsByGUID.find(guid);
    if (it != m_assetsByGUID.end()) {
        auto& labels = it->second.labels;
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
            labels.push_back(label);
        }
    }
}

void AssetDatabase::RemoveLabel(const std::string& guid, const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_assetsByGUID.find(guid);
    if (it != m_assetsByGUID.end()) {
        auto& labels = it->second.labels;
        labels.erase(std::remove(labels.begin(), labels.end(), label), labels.end());
    }
}

std::vector<std::string> AssetDatabase::GetLabels(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    return metadata ? metadata->labels : std::vector<std::string>();
}

void AssetDatabase::EnableFileWatching(bool enable) {
    if (enable == m_fileWatchingEnabled) return;

    m_fileWatchingEnabled = enable;

    if (enable) {
        m_watcherRunning = true;
        m_watcherThread = std::thread(&AssetDatabase::FileWatchThread, this);
    } else {
        m_watcherRunning = false;
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
    }
}

void AssetDatabase::ProcessFileChanges() {
    std::vector<std::pair<std::string, AssetChangeEvent::Type>> changes;

    {
        std::lock_guard<std::mutex> lock(m_pendingChangesMutex);
        changes = std::move(m_pendingChanges);
        m_pendingChanges.clear();
    }

    for (const auto& [path, type] : changes) {
        switch (type) {
            case AssetChangeEvent::Type::Created:
            case AssetChangeEvent::Type::Modified:
                RefreshPath(path);
                break;
            case AssetChangeEvent::Type::Deleted: {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto pathIt = m_pathToGUID.find(path);
                if (pathIt != m_pathToGUID.end()) {
                    m_assetsByGUID[pathIt->second].status = AssetStatus::Deleted;
                }
                break;
            }
            default:
                break;
        }
    }
}

std::string AssetDatabase::GetCachedAssetPath(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    return metadata ? metadata->cachePath : "";
}

bool AssetDatabase::IsCacheValid(const std::string& guid) const {
    const AssetMetadata* metadata = GetAssetByGUID(guid);
    if (!metadata || !metadata->isCached) return false;

    // Check if cache file exists
    if (!std::filesystem::exists(metadata->cachePath)) return false;

    // Check if source is newer than cache
    auto cacheTime = std::filesystem::last_write_time(metadata->cachePath);
    return cacheTime >= metadata->lastModified;
}

void AssetDatabase::InvalidateCache(const std::string& guid) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_assetsByGUID.find(guid);
    if (it != m_assetsByGUID.end()) {
        it->second.isCached = false;
        if (std::filesystem::exists(it->second.cachePath)) {
            std::filesystem::remove_all(it->second.cachePath);
        }
    }
}

void AssetDatabase::ClearCache() {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& [guid, metadata] : m_assetsByGUID) {
        metadata.isCached = false;
    }

    // Clear cache directory (preserve database file)
    for (const auto& entry : std::filesystem::directory_iterator(m_cachePath)) {
        if (entry.path().filename() != "asset_database.json") {
            std::filesystem::remove_all(entry.path());
        }
    }
}

size_t AssetDatabase::GetCacheSize() const {
    size_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(m_cachePath)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    return total;
}

std::string AssetDatabase::GenerateGUID() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dist;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << dist(gen);
    ss << std::setw(4) << (dist(gen) & 0xFFFF);
    ss << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000);  // Version 4
    ss << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000);  // Variant
    ss << std::setw(12) << (static_cast<uint64_t>(dist(gen)) << 32 | dist(gen));

    return ss.str();
}

AssetType AssetDatabase::GetAssetTypeFromExtension(const std::string& extension) {
    static std::unordered_map<std::string, AssetType> extensionMap = {
        // Textures
        {".png", AssetType::Texture}, {".jpg", AssetType::Texture}, {".jpeg", AssetType::Texture},
        {".tga", AssetType::Texture}, {".bmp", AssetType::Texture}, {".dds", AssetType::Texture},
        {".hdr", AssetType::Texture}, {".exr", AssetType::Texture},

        // Meshes
        {".obj", AssetType::Mesh}, {".fbx", AssetType::Mesh}, {".gltf", AssetType::Mesh},
        {".glb", AssetType::Mesh}, {".dae", AssetType::Mesh},

        // Materials
        {".mat", AssetType::Material}, {".material", AssetType::Material},

        // Shaders
        {".hlsl", AssetType::Shader}, {".glsl", AssetType::Shader}, {".vert", AssetType::Shader},
        {".frag", AssetType::Shader}, {".comp", AssetType::Shader},

        // Audio
        {".wav", AssetType::Audio}, {".mp3", AssetType::Audio}, {".ogg", AssetType::Audio},
        {".flac", AssetType::Audio},

        // Animation
        {".anim", AssetType::Animation},

        // Prefabs
        {".prefab", AssetType::Prefab},

        // Scenes
        {".scene", AssetType::Scene},

        // Scripts
        {".lua", AssetType::Script},

        // Fonts
        {".ttf", AssetType::Font}, {".otf", AssetType::Font},

        // Config
        {".json", AssetType::Config}, {".xml", AssetType::Config}, {".yaml", AssetType::Config}
    };

    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = extensionMap.find(ext);
    return (it != extensionMap.end()) ? it->second : AssetType::Unknown;
}

std::string AssetDatabase::GetAssetTypeName(AssetType type) {
    switch (type) {
        case AssetType::Texture: return "Texture";
        case AssetType::Mesh: return "Mesh";
        case AssetType::Material: return "Material";
        case AssetType::Shader: return "Shader";
        case AssetType::Audio: return "Audio";
        case AssetType::Animation: return "Animation";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Scene: return "Scene";
        case AssetType::Script: return "Script";
        case AssetType::Font: return "Font";
        case AssetType::Config: return "Config";
        default: return "Unknown";
    }
}

uint32_t AssetDatabase::GetAssetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32_t>(m_assetsByGUID.size());
}

uint32_t AssetDatabase::GetAssetCount(AssetType type) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t count = 0;
    for (const auto& [guid, metadata] : m_assetsByGUID) {
        if (metadata.type == type) count++;
    }
    return count;
}

size_t AssetDatabase::GetTotalAssetSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t total = 0;
    for (const auto& [guid, metadata] : m_assetsByGUID) {
        total += metadata.fileSize;
    }
    return total;
}

std::string AssetDatabase::GetRelativePath(const std::string& absolutePath) const {
    std::filesystem::path absPath(absolutePath);
    std::filesystem::path rootPath(m_assetRootPath);

    auto relPath = std::filesystem::relative(absPath, rootPath);
    return AssetLoaderUtils::NormalizePath(relPath.string());
}

std::string AssetDatabase::GetAbsolutePath(const std::string& relativePath) const {
    return AssetLoaderUtils::NormalizePath(m_assetRootPath + "/" + relativePath);
}

void AssetDatabase::LoadDatabase() {
    if (!std::filesystem::exists(m_databasePath)) return;

    try {
        std::ifstream file(m_databasePath);
        nlohmann::json j;
        file >> j;

        for (const auto& assetJson : j["assets"]) {
            AssetMetadata metadata;
            metadata.guid = assetJson.value("guid", "");
            metadata.path = assetJson.value("path", "");
            metadata.absolutePath = GetAbsolutePath(metadata.path);
            metadata.type = static_cast<AssetType>(assetJson.value("type", 0));
            metadata.status = static_cast<AssetStatus>(assetJson.value("status", 0));
            metadata.fileSize = assetJson.value("fileSize", 0);
            metadata.contentHash = assetJson.value("contentHash", 0);
            metadata.isCached = assetJson.value("isCached", false);
            metadata.cachePath = assetJson.value("cachePath", "");

            if (assetJson.contains("labels")) {
                metadata.labels = assetJson["labels"].get<std::vector<std::string>>();
            }
            if (assetJson.contains("dependencies")) {
                metadata.dependencies = assetJson["dependencies"].get<std::vector<std::string>>();
            }

            m_assetsByGUID[metadata.guid] = metadata;
            m_pathToGUID[metadata.path] = metadata.guid;
        }
    } catch (...) {
        // Database corrupted, will be regenerated
    }
}

void AssetDatabase::SaveDatabase() {
    std::lock_guard<std::mutex> lock(m_mutex);

    nlohmann::json j;
    j["version"] = 1;

    nlohmann::json assets = nlohmann::json::array();
    for (const auto& [guid, metadata] : m_assetsByGUID) {
        nlohmann::json assetJson;
        assetJson["guid"] = metadata.guid;
        assetJson["path"] = metadata.path;
        assetJson["type"] = static_cast<int>(metadata.type);
        assetJson["status"] = static_cast<int>(metadata.status);
        assetJson["fileSize"] = metadata.fileSize;
        assetJson["contentHash"] = metadata.contentHash;
        assetJson["isCached"] = metadata.isCached;
        assetJson["cachePath"] = metadata.cachePath;
        assetJson["labels"] = metadata.labels;
        assetJson["dependencies"] = metadata.dependencies;
        assets.push_back(assetJson);
    }
    j["assets"] = assets;

    std::ofstream file(m_databasePath);
    file << std::setw(2) << j;
}

void AssetDatabase::UpdateDependencies(const std::string& guid) {
    auto it = m_assetsByGUID.find(guid);
    if (it == m_assetsByGUID.end()) return;

    std::string extension = std::filesystem::path(it->second.path).extension().string();
    IAssetImporter* importer = GetImporterForExtension(extension);
    if (!importer) return;

    // Get new dependencies
    auto newDeps = importer->GetDependencies(it->second.absolutePath);

    // Clear old dependent references
    for (const auto& oldDep : it->second.dependencies) {
        auto depIt = m_assetsByGUID.find(oldDep);
        if (depIt != m_assetsByGUID.end()) {
            auto& dependents = depIt->second.dependents;
            dependents.erase(std::remove(dependents.begin(), dependents.end(), guid), dependents.end());
        }
    }

    // Set new dependencies
    it->second.dependencies = newDeps;

    // Add new dependent references
    for (const auto& newDep : newDeps) {
        auto depIt = m_assetsByGUID.find(newDep);
        if (depIt != m_assetsByGUID.end()) {
            depIt->second.dependents.push_back(guid);
        }
    }
}

IAssetImporter* AssetDatabase::GetImporterForExtension(const std::string& extension) const {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = m_extensionToImporter.find(ext);
    return (it != m_extensionToImporter.end()) ? it->second : nullptr;
}

uint32_t AssetDatabase::CalculateFileHash(const std::string& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return 0;

    uint32_t hash = 2166136261u;  // FNV-1a
    char buffer[4096];

    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        for (std::streamsize i = 0; i < file.gcount(); i++) {
            hash ^= static_cast<uint8_t>(buffer[i]);
            hash *= 16777619u;
        }
    }

    return hash;
}

void AssetDatabase::FileWatchThread() {
    // Simple polling-based file watcher
    // In production, use OS-specific file watching APIs

    std::unordered_map<std::string, std::filesystem::file_time_type> lastModTimes;

    while (m_watcherRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!std::filesystem::exists(m_assetRootPath)) continue;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(m_assetRootPath)) {
            if (!entry.is_regular_file()) continue;

            std::string path = GetRelativePath(entry.path().string());
            auto lastMod = std::filesystem::last_write_time(entry.path());

            auto it = lastModTimes.find(path);
            if (it == lastModTimes.end()) {
                lastModTimes[path] = lastMod;
                QueueFileChange(path, AssetChangeEvent::Type::Created);
            } else if (it->second != lastMod) {
                lastModTimes[path] = lastMod;
                QueueFileChange(path, AssetChangeEvent::Type::Modified);
            }
        }
    }
}

void AssetDatabase::QueueFileChange(const std::string& path, AssetChangeEvent::Type type) {
    std::lock_guard<std::mutex> lock(m_pendingChangesMutex);
    m_pendingChanges.emplace_back(path, type);
}

// ============================================================================
// AssetLoaderUtils
// ============================================================================

namespace AssetLoaderUtils {

std::vector<std::string> GetFilesWithExtension(const std::string& directory,
                                                 const std::string& extension,
                                                 bool recursive) {
    std::vector<std::string> result;

    if (!std::filesystem::exists(directory)) return result;

    auto iterator = recursive
        ? std::filesystem::recursive_directory_iterator(directory)
        : std::filesystem::recursive_directory_iterator(directory);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == extension) {
                result.push_back(entry.path().string());
            }
        }
    }

    return result;
}

std::vector<std::string> GetAssetFiles(const std::string& directory, bool recursive) {
    std::vector<std::string> result;

    if (!std::filesystem::exists(directory)) return result;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (AssetDatabase::GetAssetTypeFromExtension(ext) != AssetType::Unknown) {
                result.push_back(entry.path().string());
            }
        }
    }

    return result;
}

bool IsValidAssetPath(const std::string& path) {
    // Check for invalid characters
    static const std::string invalidChars = "<>:\"|?*";
    for (char c : path) {
        if (invalidChars.find(c) != std::string::npos) {
            return false;
        }
    }
    return !path.empty();
}

std::string NormalizePath(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');

    // Remove trailing slash
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

std::string GetUniqueFilename(const std::string& basePath) {
    if (!std::filesystem::exists(basePath)) {
        return basePath;
    }

    std::filesystem::path path(basePath);
    std::string stem = path.stem().string();
    std::string ext = path.extension().string();
    std::string parent = path.parent_path().string();

    int counter = 1;
    std::string newPath;
    do {
        newPath = parent + "/" + stem + "_" + std::to_string(counter) + ext;
        counter++;
    } while (std::filesystem::exists(newPath));

    return newPath;
}

} // namespace AssetLoaderUtils

} // namespace Cortex::Utils
