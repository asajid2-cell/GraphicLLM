#include "Graphics/EnvironmentManifest.h"

#include <exception>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace Cortex::Graphics {

namespace {

[[nodiscard]] std::string OptionalString(const nlohmann::json& j,
                                         const char* key,
                                         const std::string& fallback = {}) {
    if (!j.contains(key) || !j.at(key).is_string()) {
        return fallback;
    }
    return j.at(key).get<std::string>();
}

[[nodiscard]] float OptionalFloat(const nlohmann::json& j, const char* key, float fallback) {
    if (!j.contains(key) || !j.at(key).is_number()) {
        return fallback;
    }
    return j.at(key).get<float>();
}

[[nodiscard]] uint32_t OptionalUInt(const nlohmann::json& j, const char* key, uint32_t fallback) {
    if (!j.contains(key) || !j.at(key).is_number_integer()) {
        return fallback;
    }
    const int value = j.at(key).get<int>();
    if (value < 0) {
        return fallback;
    }
    return static_cast<uint32_t>(value);
}

[[nodiscard]] bool OptionalBool(const nlohmann::json& j, const char* key, bool fallback) {
    if (!j.contains(key) || !j.at(key).is_boolean()) {
        return fallback;
    }
    return j.at(key).get<bool>();
}

} // namespace

EnvironmentBudgetClass EnvironmentBudgetClassFromString(const std::string& value) {
    if (value == "tiny") {
        return EnvironmentBudgetClass::Tiny;
    }
    if (value == "medium") {
        return EnvironmentBudgetClass::Medium;
    }
    if (value == "large") {
        return EnvironmentBudgetClass::Large;
    }
    return EnvironmentBudgetClass::Small;
}

const char* ToString(EnvironmentBudgetClass budgetClass) {
    switch (budgetClass) {
    case EnvironmentBudgetClass::Tiny: return "tiny";
    case EnvironmentBudgetClass::Small: return "small";
    case EnvironmentBudgetClass::Medium: return "medium";
    case EnvironmentBudgetClass::Large: return "large";
    }
    return "small";
}

bool IsEnvironmentAllowedForBudget(RendererBudgetProfile profile,
                                   EnvironmentBudgetClass budgetClass) {
    switch (profile) {
    case RendererBudgetProfile::UltraLow2GB:
        return budgetClass == EnvironmentBudgetClass::Tiny ||
               budgetClass == EnvironmentBudgetClass::Small;
    case RendererBudgetProfile::Low4GB:
        return budgetClass != EnvironmentBudgetClass::Large;
    case RendererBudgetProfile::Balanced8GB:
    case RendererBudgetProfile::High:
        return true;
    }
    return budgetClass != EnvironmentBudgetClass::Large;
}

std::filesystem::path ResolveEnvironmentAssetPath(const std::filesystem::path& manifestPath,
                                                  const std::filesystem::path& assetPath) {
    if (assetPath.empty() || assetPath.is_absolute()) {
        return assetPath;
    }
    return manifestPath.parent_path() / assetPath;
}

Result<EnvironmentManifest> LoadEnvironmentManifest(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<EnvironmentManifest>::Err("Failed to open environment manifest: " + path.string());
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        return Result<EnvironmentManifest>::Err("Failed to parse environment manifest: " + std::string(e.what()));
    }

    if (!root.is_object()) {
        return Result<EnvironmentManifest>::Err("Environment manifest root must be an object");
    }

    EnvironmentManifest manifest{};
    manifest.schema = OptionalUInt(root, "schema", 1);
    if (manifest.schema != 1) {
        return Result<EnvironmentManifest>::Err("Unsupported environment manifest schema: " +
                                                std::to_string(manifest.schema));
    }

    manifest.defaultEnvironment = OptionalString(root, "default", manifest.defaultEnvironment);
    manifest.fallback = OptionalString(root, "fallback", manifest.fallback);

    if (!root.contains("environments") || !root.at("environments").is_array()) {
        return Result<EnvironmentManifest>::Err("Environment manifest requires an environments array");
    }

    for (const auto& item : root.at("environments")) {
        if (!item.is_object()) {
            return Result<EnvironmentManifest>::Err("Environment manifest entry must be an object");
        }

        EnvironmentManifestEntry entry{};
        entry.id = OptionalString(item, "id");
        if (entry.id.empty()) {
            return Result<EnvironmentManifest>::Err("Environment manifest entry missing id");
        }

        entry.displayName = OptionalString(item, "display_name", entry.id);
        entry.runtimePath = OptionalString(item, "runtime_path");
        entry.sourcePath = OptionalString(item, "source_path");
        entry.thumbnailPath = OptionalString(item, "thumbnail");
        entry.budgetClass = EnvironmentBudgetClassFromString(OptionalString(item, "budget_class", "small"));
        entry.maxRuntimeDimension = OptionalUInt(item, "max_runtime_dimension", entry.maxRuntimeDimension);
        entry.required = OptionalBool(item, "required", false);
        entry.enabled = OptionalBool(item, "enabled", true);
        entry.defaultDiffuseIntensity = OptionalFloat(item, "default_diffuse", entry.defaultDiffuseIntensity);
        entry.defaultSpecularIntensity = OptionalFloat(item, "default_specular", entry.defaultSpecularIntensity);

        if (entry.enabled && entry.runtimePath.empty() && entry.id != manifest.fallback) {
            return Result<EnvironmentManifest>::Err("Environment manifest entry '" + entry.id +
                                                    "' is enabled but has no runtime_path");
        }

        manifest.environments.push_back(std::move(entry));
    }

    return Result<EnvironmentManifest>::Ok(std::move(manifest));
}

} // namespace Cortex::Graphics
