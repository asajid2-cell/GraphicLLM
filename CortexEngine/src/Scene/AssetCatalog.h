#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Utils/Result.h"

namespace Cortex::Scene {

// One catalogued, loadable 3D asset resolved to a real glTF on disk. This is the
// unit the scene creator places instead of a primitive stand-in.
struct CatalogAsset {
    std::string id;                          // semantic id, e.g. "chair", "bench", "kitchen_faucet_gooseneck"
    std::string runtimeAssetPath;            // resolved (cwd-usable) .gltf path
    std::vector<std::string> semanticRoles;  // e.g. "seating", "faucet_mesh"
    std::vector<std::string> sceneFamilies;  // e.g. "home_kitchen_lantern"
    std::string sourceClass;                 // cc0_curated_library / engine_generated_fidelity_mesh / ...
    bool fromRegistry = false;               // true if it came from asset_registry_v2.json (rich tags)
};

// A real, queryable, tagged asset library — the foundation of the scene creator.
// Two merged sources:
//   1) assets/final_art/asset_registry_v2.json  — richly tagged (semantic_roles +
//      scene_families), the curated subset.
//   2) a directory scan of assets/models/kenney_furniture_kit/<name>/<name>.gltf
//      — the full ~140-mesh CC0 furniture library; the folder name IS the
//      semantic id ("chair", "bookcaseOpen", ...). A small keyword map assigns a
//      coarse role ("seating", "storage", ...) so role queries cover the bulk too.
//
// This replaces hardcoded sample-model names + primitive stand-ins: a scene
// command/recipe asks for a real mesh by id, semantic role, or scene family and
// gets back a loadable glTF path (consumed by Utils::LoadGLTFMesh).
class AssetCatalog {
public:
    // Loads the registry + scans the Kenney kit. `assetsRoot` is the directory
    // that CONTAINS "assets/" (the repo root). If empty, it is discovered: the
    // CORTEX_ASSET_ROOT env var, else by walking up from the current working
    // directory (build/bin -> repo root) looking for the assets marker.
    Result<void> Load(const std::filesystem::path& assetsRoot = {});

    [[nodiscard]] bool IsLoaded() const { return m_loaded; }
    [[nodiscard]] std::size_t Size() const { return m_assets.size(); }

    // Exact id lookup (case-insensitive), e.g. "chair", "bench".
    [[nodiscard]] const CatalogAsset* FindById(const std::string& id) const;

    // Resolve a free-form key to ONE asset's glTF path. Match order: exact id,
    // semantic role, scene family, then fuzzy substring on id. `variantSeed`
    // rotates among equally-good matches so repeated placements vary. Returns
    // nullopt when nothing matches (caller decides the fallback).
    [[nodiscard]] std::optional<std::string> ResolvePath(const std::string& key,
                                                         std::uint32_t variantSeed = 0) const;

    // All assets carrying a semantic role / scene family (for layout recipes).
    [[nodiscard]] std::vector<const CatalogAsset*> ByRole(const std::string& role) const;
    [[nodiscard]] std::vector<const CatalogAsset*> BySceneFamily(const std::string& family) const;

    [[nodiscard]] const std::vector<CatalogAsset>& All() const { return m_assets; }

    // The resolved repo root used for asset paths (for diagnostics).
    [[nodiscard]] const std::filesystem::path& AssetsRoot() const { return m_assetsRoot; }

private:
    void AddAsset(CatalogAsset asset); // de-dupes by id (registry entry wins)
    void Index();

    bool m_loaded = false;
    std::filesystem::path m_assetsRoot;
    std::vector<CatalogAsset> m_assets;
    std::unordered_map<std::string, std::size_t> m_byId;                 // lower(id) -> index
    std::unordered_map<std::string, std::vector<std::size_t>> m_byRole;  // lower(role) -> indices
    std::unordered_map<std::string, std::vector<std::size_t>> m_byFamily;
};

} // namespace Cortex::Scene
