#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Cortex {

struct VisualValidationStats {
    bool valid = false;
    std::string reason;
    int32_t width = 0;
    int32_t height = 0;
    double avgLuma = 0.0;
    double nonBlackRatio = 0.0;
    double colorfulRatio = 0.0;
    double saturatedRatio = 0.0;
    double nearWhiteRatio = 0.0;
    double darkDetailRatio = 0.0;
    double centerAvgLuma = 0.0;
};

[[nodiscard]] VisualValidationStats AnalyzeBMP(const std::filesystem::path& path);
[[nodiscard]] nlohmann::json VisualStatsToJson(const VisualValidationStats& stats);

} // namespace Cortex
