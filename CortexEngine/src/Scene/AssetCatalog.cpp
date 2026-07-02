#include "Scene/AssetCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Scene {

namespace fs = std::filesystem;

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Discover the directory that contains "assets/". The engine usually runs from
// build/bin, so we walk up from the cwd looking for a stable marker. Honors
// CORTEX_ASSET_ROOT as an explicit override.
fs::path DiscoverAssetsRoot() {
    if (const char* env = std::getenv("CORTEX_ASSET_ROOT"); env && *env) {
        return fs::path(env);
    }
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
        if (fs::exists(cur / "assets" / "models" / "kenney_furniture_kit", ec) ||
            fs::exists(cur / "assets" / "final_art" / "asset_registry_v2.json", ec)) {
            return cur;
        }
        if (!cur.has_parent_path()) {
            break;
        }
        cur = cur.parent_path();
    }
    return fs::current_path(ec);
}

std::string ResolveRuntimePath(const fs::path& root, const std::string& rel) {
    fs::path p(rel);
    if (p.is_absolute()) {
        return p.lexically_normal().string();
    }
    return (root / p).lexically_normal().string();
}

// Coarse, general-purpose semantic role(s) inferred from a Kenney asset id so
// the bulk furniture library answers role queries (the registry's own roles are
// scene-specific, e.g. "sideline_equipment_mesh"; these complement them).
std::vector<std::string> InferRolesFromId(const std::string& idLower) {
    struct Kw { const char* needle; const char* role; };
    static const Kw kws[] = {
        {"chair", "seating"},   {"stool", "seating"},      {"bench", "seating"},
        {"sofa", "seating"},    {"couch", "seating"},
        {"table", "surface"},   {"desk", "surface"},       {"counter", "surface"},
        {"bed", "bed"},
        {"cabinet", "storage"}, {"bookcase", "storage"},   {"shelf", "storage"},
        {"dresser", "storage"}, {"wardrobe", "storage"},   {"drawer", "storage"},
        {"lamp", "lighting"},   {"light", "lighting"},     {"ceilingfan", "lighting"},
        {"lantern", "lighting"},
        {"rug", "floor_decor"}, {"plant", "decor"},        {"books", "decor"},
        {"vase", "decor"},      {"painting", "wall_decor"},{"mirror", "wall_decor"},
        {"toilet", "bathroom"}, {"bathtub", "bathroom"},   {"shower", "bathroom"},
        {"bathroom", "bathroom"},
        {"kitchen", "kitchen"}, {"stove", "kitchen"},      {"fridge", "kitchen"},
        {"refrigerator", "kitchen"},                       {"sink", "kitchen"},
        {"television", "electronics"}, {"computer", "electronics"},
        {"speaker", "electronics"},    {"laptop", "electronics"},
        {"door", "architecture"},      {"window", "architecture"},
        {"stairs", "architecture"},
        // Outdoor / nature (naturalistic_showcase).
        {"boulder", "rock"},  {"rock", "rock"},     {"stone", "rock"},
        {"tree", "tree"},     {"trunk", "wood"},    {"stump", "wood"},
        {"branch", "wood"},   {"log", "wood"},      {"driftwood", "wood"},
        {"fern", "foliage"},  {"grass", "foliage"}, {"bush", "foliage"},
        {"rooibos", "foliage"}, {"shrub", "foliage"},
        {"barrel", "prop"},
    };
    std::vector<std::string> roles;
    for (const auto& k : kws) {
        if (idLower.find(k.needle) != std::string::npos) {
            if (std::find(roles.begin(), roles.end(), k.role) == roles.end()) {
                roles.emplace_back(k.role);
            }
        }
    }
    return roles;
}

std::vector<std::string> JsonStringArray(const nlohmann::json& parent, const char* key) {
    std::vector<std::string> out;
    if (parent.contains(key) && parent.at(key).is_array()) {
        for (const auto& v : parent.at(key)) {
            if (v.is_string()) {
                out.push_back(v.get<std::string>());
            }
        }
    }
    return out;
}

} // namespace

void AssetCatalog::AddAsset(CatalogAsset asset) {
    const std::string key = ToLower(asset.id);
    if (key.empty()) {
        return;
    }
    auto it = m_byId.find(key);
    if (it == m_byId.end()) {
        m_byId.emplace(key, m_assets.size());
        m_assets.push_back(std::move(asset));
        return;
    }

    // Merge into the existing entry: a registry path wins over a scan path, and
    // roles/families are unioned so an asset carries both the registry's
    // scene-specific roles and the inferred general ones.
    CatalogAsset& existing = m_assets[it->second];
    if (!existing.fromRegistry && asset.fromRegistry) {
        existing.runtimeAssetPath = asset.runtimeAssetPath;
        existing.sourceClass = asset.sourceClass;
        existing.fromRegistry = true;
    }
    auto mergeVec = [](std::vector<std::string>& dst, const std::vector<std::string>& src) {
        for (const auto& s : src) {
            if (std::find(dst.begin(), dst.end(), s) == dst.end()) {
                dst.push_back(s);
            }
        }
    };
    mergeVec(existing.semanticRoles, asset.semanticRoles);
    mergeVec(existing.sceneFamilies, asset.sceneFamilies);
}

void AssetCatalog::Index() {
    m_byRole.clear();
    m_byFamily.clear();
    for (std::size_t i = 0; i < m_assets.size(); ++i) {
        for (const auto& role : m_assets[i].semanticRoles) {
            m_byRole[ToLower(role)].push_back(i);
        }
        for (const auto& fam : m_assets[i].sceneFamilies) {
            m_byFamily[ToLower(fam)].push_back(i);
        }
    }
}

Result<void> AssetCatalog::Load(const fs::path& assetsRoot) {
    m_loaded = false;
    m_assets.clear();
    m_byId.clear();
    m_byRole.clear();
    m_byFamily.clear();

    m_assetsRoot = assetsRoot.empty() ? DiscoverAssetsRoot() : assetsRoot;
    std::error_code ec;

    // 1) Curated, richly-tagged registry (optional — a missing/invalid registry
    //    is non-fatal; the Kenney scan still yields a usable library).
    const fs::path regPath = m_assetsRoot / "assets" / "final_art" / "asset_registry_v2.json";
    std::ifstream in(regPath);
    if (in) {
        nlohmann::json root;
        try {
            in >> root;
            if (root.is_object() && root.contains("assets") && root.at("assets").is_array()) {
                for (const auto& a : root.at("assets")) {
                    if (!a.is_object() || !a.contains("id") || !a.contains("runtime_asset")) {
                        continue;
                    }
                    CatalogAsset asset;
                    asset.id = a.at("id").get<std::string>();
                    asset.runtimeAssetPath = ResolveRuntimePath(m_assetsRoot, a.at("runtime_asset").get<std::string>());
                    asset.semanticRoles = JsonStringArray(a, "semantic_roles");
                    asset.sceneFamilies = JsonStringArray(a, "scene_families");
                    if (a.contains("source_class") && a.at("source_class").is_string()) {
                        asset.sourceClass = a.at("source_class").get<std::string>();
                    }
                    asset.fromRegistry = true;
                    if (!fs::exists(asset.runtimeAssetPath, ec)) {
                        spdlog::warn("AssetCatalog: registry asset '{}' missing on disk: {}", asset.id,
                                     asset.runtimeAssetPath);
                        continue;
                    }
                    AddAsset(std::move(asset));
                }
            } else {
                spdlog::warn("AssetCatalog: registry {} has no 'assets' array", regPath.string());
            }
        } catch (const std::exception& e) {
            spdlog::warn("AssetCatalog: failed to parse registry {}: {}", regPath.string(), e.what());
        }
    } else {
        spdlog::info("AssetCatalog: no registry at {} (scanning Kenney kit only)", regPath.string());
    }

    // 2) Full CC0 furniture library scan. The folder name is the semantic id.
    const fs::path kenney = m_assetsRoot / "assets" / "models" / "kenney_furniture_kit";
    if (fs::is_directory(kenney, ec)) {
        for (const auto& entry : fs::directory_iterator(kenney, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            const fs::path gltf = entry.path() / (name + ".gltf");
            if (!fs::exists(gltf, ec)) {
                continue;
            }
            CatalogAsset asset;
            asset.id = name;
            asset.runtimeAssetPath = gltf.lexically_normal().string();
            asset.semanticRoles = InferRolesFromId(ToLower(name));
            asset.sourceClass = "cc0_curated_library";
            asset.fromRegistry = false;
            AddAsset(std::move(asset));
        }
    } else {
        spdlog::info("AssetCatalog: no Kenney kit at {}", kenney.string());
    }

    // 3) Nature/outdoor pack (naturalistic_showcase): <name>/<name>_1k.gltf,
    //    falling back to any .gltf in the folder. Folder name is the id; roles
    //    are inferred (rock/wood/tree/foliage/prop) so outdoor recipes resolve
    //    them the same way as furniture.
    const fs::path nature = m_assetsRoot / "assets" / "models" / "naturalistic_showcase";
    if (fs::is_directory(nature, ec)) {
        for (const auto& entry : fs::directory_iterator(nature, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            fs::path gltf = entry.path() / (name + "_1k.gltf");
            if (!fs::exists(gltf, ec)) {
                gltf.clear();
                for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
                    if (f.path().extension() == ".gltf") {
                        gltf = f.path();
                        break;
                    }
                }
            }
            if (gltf.empty() || !fs::exists(gltf, ec)) {
                continue;
            }
            CatalogAsset asset;
            asset.id = name;
            asset.runtimeAssetPath = gltf.lexically_normal().string();
            asset.semanticRoles = InferRolesFromId(ToLower(name));
            asset.sourceClass = "naturalistic_showcase";
            asset.fromRegistry = false;
            AddAsset(std::move(asset));
        }
    }

    // 4) High-poly Khronos glTF Sample Assets furniture: <name>/<name>.gltf,
    //    falling back to any .gltf in the folder. Folder name is the id.
    const fs::path khronosFurniture = m_assetsRoot / "assets" / "models" / "khronos_furniture";
    if (fs::is_directory(khronosFurniture, ec)) {
        for (const auto& entry : fs::directory_iterator(khronosFurniture, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            fs::path gltf = entry.path() / (name + ".gltf");
            if (!fs::exists(gltf, ec)) {
                gltf.clear();
                for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
                    if (f.path().extension() == ".gltf") {
                        gltf = f.path();
                        break;
                    }
                }
            }
            if (gltf.empty() || !fs::exists(gltf, ec)) {
                continue;
            }
            CatalogAsset asset;
            asset.id = name;
            asset.runtimeAssetPath = gltf.lexically_normal().string();
            asset.semanticRoles = InferRolesFromId(ToLower(name));
            asset.sourceClass = "khronos_gltf_sample_assets";
            asset.fromRegistry = false;
            AddAsset(std::move(asset));
        }
    }

    // 5) High-poly Sketchfab furniture: <name>/scene.gltf, falling back to any
    //    .gltf in the folder. Folder name is the id.
    const fs::path sketchfabFurniture = m_assetsRoot / "assets" / "models" / "sketchfab_furniture";
    if (fs::is_directory(sketchfabFurniture, ec)) {
        for (const auto& entry : fs::directory_iterator(sketchfabFurniture, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            fs::path gltf = entry.path() / "scene.gltf";
            if (!fs::exists(gltf, ec)) {
                gltf.clear();
                for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
                    if (f.path().extension() == ".gltf") {
                        gltf = f.path();
                        break;
                    }
                }
            }
            if (gltf.empty() || !fs::exists(gltf, ec)) {
                continue;
            }
            CatalogAsset asset;
            asset.id = name;
            asset.runtimeAssetPath = gltf.lexically_normal().string();
            asset.semanticRoles = InferRolesFromId(ToLower(name));
            asset.sourceClass = "sketchfab_furniture";
            asset.fromRegistry = false;
            AddAsset(std::move(asset));
        }
    }

    // 6) Kenney Nature Kit (CC0, baked to the loader format by tools/bake_flat_gltf.py):
    //    <name>/<name>.gltf, falling back to any .gltf. Folder name is the id.
    // 7) Fetched/generated landing zone (Sketchfab search-fetches + procgen output,
    //    normalized by tools/asset_fetch.py / tools/procgen.py): any .gltf in <name>/.
    const std::pair<const char*, const char*> scannedPacks[] = {
        {"kenney_nature_kit", "kenney_nature_kit"},
        {"fetched", "fetched"},
    };
    for (const auto& [dirName, sourceClass] : scannedPacks) {
        const fs::path packDir = m_assetsRoot / "assets" / "models" / dirName;
        if (!fs::is_directory(packDir, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(packDir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory(ec)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            fs::path gltf = entry.path() / (name + ".gltf");
            if (!fs::exists(gltf, ec)) {
                gltf.clear();
                for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
                    if (f.path().extension() == ".gltf") {
                        gltf = f.path();
                        break;
                    }
                }
            }
            if (gltf.empty() || !fs::exists(gltf, ec)) {
                continue;
            }
            CatalogAsset asset;
            asset.id = name;
            asset.runtimeAssetPath = gltf.lexically_normal().string();
            asset.semanticRoles = InferRolesFromId(ToLower(name));
            asset.sourceClass = sourceClass;
            asset.fromRegistry = false;
            AddAsset(std::move(asset));
        }
    }

    Index();
    m_loaded = true;
    spdlog::info("AssetCatalog: loaded {} assets ({} roles, {} scene families) from {}", m_assets.size(),
                 m_byRole.size(), m_byFamily.size(), m_assetsRoot.string());
    if (m_assets.empty()) {
        return Result<void>::Err("AssetCatalog loaded zero assets from " + m_assetsRoot.string());
    }
    return Result<void>::Ok();
}

const CatalogAsset* AssetCatalog::FindById(const std::string& id) const {
    auto it = m_byId.find(ToLower(id));
    if (it == m_byId.end()) {
        return nullptr;
    }
    return &m_assets[it->second];
}

std::optional<std::string> AssetCatalog::ResolvePath(const std::string& key, std::uint32_t variantSeed) const {
    const std::string klow = ToLower(key);
    if (klow.empty()) {
        return std::nullopt;
    }

    auto pickFrom = [&](const std::vector<std::size_t>& idxs) -> std::optional<std::string> {
        if (idxs.empty()) {
            return std::nullopt;
        }
        return m_assets[idxs[variantSeed % idxs.size()]].runtimeAssetPath;
    };

    // 1) exact id
    if (const CatalogAsset* a = FindById(key)) {
        return a->runtimeAssetPath;
    }
    // 2) semantic role
    if (auto it = m_byRole.find(klow); it != m_byRole.end()) {
        return pickFrom(it->second);
    }
    // 3) scene family
    if (auto it = m_byFamily.find(klow); it != m_byFamily.end()) {
        return pickFrom(it->second);
    }
    // 4) fuzzy substring on id (either direction)
    std::vector<std::size_t> hits;
    for (std::size_t i = 0; i < m_assets.size(); ++i) {
        const std::string idl = ToLower(m_assets[i].id);
        if (idl.find(klow) != std::string::npos || klow.find(idl) != std::string::npos) {
            hits.push_back(i);
        }
    }
    return pickFrom(hits);
}

std::vector<const CatalogAsset*> AssetCatalog::ByRole(const std::string& role) const {
    std::vector<const CatalogAsset*> out;
    if (auto it = m_byRole.find(ToLower(role)); it != m_byRole.end()) {
        out.reserve(it->second.size());
        for (std::size_t idx : it->second) {
            out.push_back(&m_assets[idx]);
        }
    }
    return out;
}

std::vector<const CatalogAsset*> AssetCatalog::BySceneFamily(const std::string& family) const {
    std::vector<const CatalogAsset*> out;
    if (auto it = m_byFamily.find(ToLower(family)); it != m_byFamily.end()) {
        out.reserve(it->second.size());
        for (std::size_t idx : it->second) {
            out.push_back(&m_assets[idx]);
        }
    }
    return out;
}

} // namespace Cortex::Scene
