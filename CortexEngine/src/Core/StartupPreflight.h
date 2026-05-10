#pragma once

#include "Core/Engine.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Cortex {

struct StartupPreflightIssue {
    enum class Severity {
        Info,
        Warning,
        Error
    };

    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    std::filesystem::path path;
    std::string fallback;
};

struct StartupPreflightResult {
    bool ran = false;
    bool canLaunch = true;
    bool usedSafeMode = false;
    bool dxrRequested = false;
    bool environmentManifestPresent = false;
    bool environmentFallbackAvailable = false;
    bool configProfileAvailable = true;
    std::string configProfile = "default";
    std::filesystem::path workingDirectory;
    std::vector<StartupPreflightIssue> issues;

    [[nodiscard]] uint32_t WarningCount() const;
    [[nodiscard]] uint32_t ErrorCount() const;
};

[[nodiscard]] const char* ToString(StartupPreflightIssue::Severity severity);
[[nodiscard]] StartupPreflightResult RunStartupPreflight(const EngineConfig& config);
void StoreStartupPreflightResult(StartupPreflightResult result);
[[nodiscard]] const StartupPreflightResult& GetStartupPreflightResult();

} // namespace Cortex
