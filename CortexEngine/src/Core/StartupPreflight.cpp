#include "Core/StartupPreflight.h"

#include "Graphics/EnvironmentManifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Cortex {
namespace {

StartupPreflightResult g_result{};
std::mutex g_resultMutex;

void AddIssue(StartupPreflightResult& result,
              StartupPreflightIssue::Severity severity,
              std::string code,
              std::string message,
              std::filesystem::path path = {},
              std::string fallback = {}) {
    StartupPreflightIssue issue{};
    issue.severity = severity;
    issue.code = std::move(code);
    issue.message = std::move(message);
    issue.path = std::move(path);
    issue.fallback = std::move(fallback);
    result.issues.push_back(std::move(issue));
    if (severity == StartupPreflightIssue::Severity::Error) {
        result.canLaunch = false;
    }
}

void CheckDirectory(StartupPreflightResult& result,
                    const char* code,
                    const std::filesystem::path& path,
                    bool required,
                    const char* fallback = "") {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return;
    }
    AddIssue(result,
             required ? StartupPreflightIssue::Severity::Error : StartupPreflightIssue::Severity::Warning,
             code,
             required ? "Required directory is missing" : "Optional directory is missing",
             path,
             fallback ? fallback : "");
}

void CheckFile(StartupPreflightResult& result,
               const char* code,
               const std::filesystem::path& path,
               bool required,
               const char* fallback = "") {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        return;
    }
    AddIssue(result,
             required ? StartupPreflightIssue::Severity::Error : StartupPreflightIssue::Severity::Warning,
             code,
             required ? "Required file is missing" : "Optional file is missing",
             path,
             fallback ? fallback : "");
}

bool EnvTruthy(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) {
        return false;
    }
    std::string value = raw;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value != "0" && value != "false" && value != "off" && value != "no";
}

} // namespace

uint32_t StartupPreflightResult::WarningCount() const {
    return static_cast<uint32_t>(std::count_if(issues.begin(), issues.end(), [](const StartupPreflightIssue& issue) {
        return issue.severity == StartupPreflightIssue::Severity::Warning;
    }));
}

uint32_t StartupPreflightResult::ErrorCount() const {
    return static_cast<uint32_t>(std::count_if(issues.begin(), issues.end(), [](const StartupPreflightIssue& issue) {
        return issue.severity == StartupPreflightIssue::Severity::Error;
    }));
}

const char* ToString(StartupPreflightIssue::Severity severity) {
    switch (severity) {
    case StartupPreflightIssue::Severity::Info: return "info";
    case StartupPreflightIssue::Severity::Warning: return "warning";
    case StartupPreflightIssue::Severity::Error: return "error";
    default: return "unknown";
    }
}

StartupPreflightResult RunStartupPreflight(const EngineConfig& config) {
    StartupPreflightResult result{};
    result.ran = true;
    result.dxrRequested = config.enableRayTracing;
    result.configProfile =
        config.qualityMode == EngineConfig::QualityMode::Conservative ? "conservative" : "default";
    result.usedSafeMode = config.qualityMode == EngineConfig::QualityMode::Conservative ||
                          EnvTruthy("CORTEX_FORCE_SAFE_MODE");
    result.workingDirectory = std::filesystem::current_path();

    const std::filesystem::path assetsDir = "assets";
    const std::filesystem::path shadersDir = assetsDir / "shaders";
    const std::filesystem::path configDir = assetsDir / "config";
    const std::filesystem::path environmentManifest = Graphics::DefaultEnvironmentManifestPath();
    const std::filesystem::path showcaseConfig = configDir / "showcase_scenes.json";
    const std::filesystem::path renderQualityConfig = configDir / "render_quality.json";

    CheckDirectory(result, "WORKING_DIRECTORY", result.workingDirectory, true);
    CheckDirectory(result, "SHADER_DIRECTORY_MISSING", shadersDir, true);
    CheckDirectory(result, "ASSET_DIRECTORY_MISSING", assetsDir, false, "procedural placeholders");
    CheckDirectory(result, "CONFIG_DIRECTORY_MISSING", configDir, false, "built-in defaults");
    CheckFile(result, "SHOWCASE_CONFIG_MISSING", showcaseConfig, false, "built-in scene presets");
    CheckFile(result, "RENDER_QUALITY_CONFIG_MISSING", renderQualityConfig, false, "renderer defaults");

    std::error_code ec;
    result.environmentManifestPresent = std::filesystem::is_regular_file(environmentManifest, ec);
    if (result.environmentManifestPresent) {
        auto manifestResult = Graphics::LoadEnvironmentManifest(environmentManifest);
        if (manifestResult.IsErr()) {
            AddIssue(result,
                     StartupPreflightIssue::Severity::Warning,
                     "ENVIRONMENT_MANIFEST_INVALID",
                     manifestResult.Error(),
                     environmentManifest,
                     "legacy HDR/EXR scan or procedural sky");
        } else {
            const auto& manifest = manifestResult.Value();
            result.environmentFallbackAvailable = !manifest.fallback.empty();
            bool defaultFound = false;
            bool requiredMissing = false;
            for (const auto& entry : manifest.environments) {
                defaultFound = defaultFound || entry.id == manifest.defaultEnvironment;
                if (!entry.enabled || entry.runtimePath.empty()) {
                    continue;
                }
                const auto runtimePath = Graphics::ResolveEnvironmentAssetPath(environmentManifest, entry.runtimePath);
                if (!std::filesystem::is_regular_file(runtimePath, ec)) {
                    if (entry.required) {
                        requiredMissing = true;
                    }
                    AddIssue(result,
                             entry.required ? StartupPreflightIssue::Severity::Warning
                                            : StartupPreflightIssue::Severity::Info,
                             entry.required ? "REQUIRED_ENVIRONMENT_ASSET_MISSING"
                                            : "OPTIONAL_ENVIRONMENT_ASSET_MISSING",
                             "Environment runtime asset is missing",
                             runtimePath,
                             manifest.fallback);
                }
            }
            if (!defaultFound) {
                AddIssue(result,
                         StartupPreflightIssue::Severity::Warning,
                         "DEFAULT_ENVIRONMENT_NOT_DECLARED",
                         "Environment manifest default is not present in the environments list",
                         environmentManifest,
                         manifest.fallback);
            }
            if (requiredMissing) {
                result.usedSafeMode = true;
            }
        }
    } else {
        AddIssue(result,
                 StartupPreflightIssue::Severity::Warning,
                 "ENVIRONMENT_MANIFEST_MISSING",
                 "Environment manifest is missing",
                 environmentManifest,
                 "legacy HDR/EXR scan or procedural sky");
    }

    spdlog::info("Startup preflight: passed={} safe_mode={} profile={} warnings={} errors={}",
                 result.canLaunch,
                 result.usedSafeMode,
                 result.configProfile,
                 result.WarningCount(),
                 result.ErrorCount());
    for (const auto& issue : result.issues) {
        spdlog::log(issue.severity == StartupPreflightIssue::Severity::Error
                        ? spdlog::level::err
                        : issue.severity == StartupPreflightIssue::Severity::Warning
                              ? spdlog::level::warn
                              : spdlog::level::info,
                    "Startup preflight {} {} path='{}' fallback='{}'",
                    ToString(issue.severity),
                    issue.code,
                    issue.path.string(),
                    issue.fallback);
    }

    return result;
}

void StoreStartupPreflightResult(StartupPreflightResult result) {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    g_result = std::move(result);
}

const StartupPreflightResult& GetStartupPreflightResult() {
    std::lock_guard<std::mutex> lock(g_resultMutex);
    return g_result;
}

} // namespace Cortex
