#pragma once

#include "Graphics/BudgetPlanner.h"
#include "Utils/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Cortex::Graphics {

enum class EnvironmentBudgetClass {
    Tiny,
    Small,
    Medium,
    Large
};

struct EnvironmentManifestEntry {
    std::string id;
    std::string displayName;
    std::filesystem::path runtimePath;
    std::filesystem::path sourcePath;
    std::filesystem::path thumbnailPath;
    EnvironmentBudgetClass budgetClass = EnvironmentBudgetClass::Small;
    uint32_t maxRuntimeDimension = 2048;
    bool required = false;
    bool enabled = true;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
};

struct EnvironmentManifest {
    uint32_t schema = 1;
    std::string defaultEnvironment = "studio";
    std::string fallback = "procedural_sky";
    std::vector<EnvironmentManifestEntry> environments;
};

[[nodiscard]] Result<EnvironmentManifest> LoadEnvironmentManifest(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path ResolveEnvironmentAssetPath(const std::filesystem::path& manifestPath,
                                                                const std::filesystem::path& assetPath);
[[nodiscard]] bool IsEnvironmentAllowedForBudget(RendererBudgetProfile profile,
                                                 EnvironmentBudgetClass budgetClass);
[[nodiscard]] const char* ToString(EnvironmentBudgetClass budgetClass);
[[nodiscard]] EnvironmentBudgetClass EnvironmentBudgetClassFromString(const std::string& value);

} // namespace Cortex::Graphics
