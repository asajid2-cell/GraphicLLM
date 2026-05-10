#include "Core/VisualValidation.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <vector>

namespace Cortex {

namespace {
uint16_t ReadLE16(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 1 >= bytes.size()) {
        return 0;
    }
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t ReadLE32(const std::vector<uint8_t>& bytes, size_t offset) {
    if (offset + 3 >= bytes.size()) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

int32_t ReadLEI32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<int32_t>(ReadLE32(bytes, offset));
}
} // namespace

VisualValidationStats AnalyzeBMP(const std::filesystem::path& path) {
    VisualValidationStats stats;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        stats.reason = "open_failed";
        return stats;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 54) {
        stats.reason = "file_too_small";
        return stats;
    }
    if (bytes[0] != 'B' || bytes[1] != 'M') {
        stats.reason = "not_bmp";
        return stats;
    }

    const uint32_t dataOffset = ReadLE32(bytes, 10);
    const uint32_t dibSize = ReadLE32(bytes, 14);
    const int32_t width = ReadLEI32(bytes, 18);
    const int32_t signedHeight = ReadLEI32(bytes, 22);
    const uint16_t planes = ReadLE16(bytes, 26);
    const uint16_t bpp = ReadLE16(bytes, 28);
    const uint32_t compression = ReadLE32(bytes, 30);
    if (dibSize < 40 || width <= 0 || signedHeight == 0 || planes != 1) {
        stats.reason = "unsupported_header";
        return stats;
    }
    if ((bpp != 24 && bpp != 32) || compression != 0u) {
        stats.reason = "unsupported_format";
        return stats;
    }

    const int32_t height = std::abs(signedHeight);
    const size_t bytesPerPixel = static_cast<size_t>(bpp / 8);
    const size_t rowStride = ((static_cast<size_t>(width) * bytesPerPixel + 3u) / 4u) * 4u;
    const size_t requiredSize = static_cast<size_t>(dataOffset) + rowStride * static_cast<size_t>(height);
    if (dataOffset >= bytes.size() || requiredSize > bytes.size()) {
        stats.reason = "truncated_pixels";
        return stats;
    }

    double lumaTotal = 0.0;
    uint64_t nonBlack = 0;
    uint64_t colorful = 0;
    uint64_t saturated = 0;
    uint64_t nearWhite = 0;
    uint64_t darkDetail = 0;
    double centerLumaTotal = 0.0;
    uint64_t centerPixelCount = 0;
    const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    for (int32_t y = 0; y < height; ++y) {
        const size_t row = static_cast<size_t>(dataOffset) + static_cast<size_t>(y) * rowStride;
        for (int32_t x = 0; x < width; ++x) {
            const size_t p = row + static_cast<size_t>(x) * bytesPerPixel;
            const double b = static_cast<double>(bytes[p + 0]);
            const double g = static_cast<double>(bytes[p + 1]);
            const double r = static_cast<double>(bytes[p + 2]);
            const double luma = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
            lumaTotal += luma;
            if (r > 4.0 || g > 4.0 || b > 4.0) {
                ++nonBlack;
            }
            const double maxChannel = std::max(r, std::max(g, b));
            const double minChannel = std::min(r, std::min(g, b));
            if ((maxChannel - minChannel) > 10.0) {
                ++colorful;
            }
            if (maxChannel >= 250.0) {
                ++saturated;
            }
            if (r >= 235.0 && g >= 235.0 && b >= 235.0) {
                ++nearWhite;
            }
            if (luma > 8.0 && luma < 80.0) {
                ++darkDetail;
            }
            const int32_t centerMinX = width / 4;
            const int32_t centerMaxX = width - centerMinX;
            const int32_t centerMinY = height / 5;
            const int32_t centerMaxY = height - centerMinY;
            if (x >= centerMinX && x < centerMaxX &&
                y >= centerMinY && y < centerMaxY) {
                centerLumaTotal += luma;
                ++centerPixelCount;
            }
        }
    }

    stats.valid = true;
    stats.width = width;
    stats.height = height;
    stats.avgLuma = pixelCount > 0 ? (lumaTotal / static_cast<double>(pixelCount)) : 0.0;
    stats.nonBlackRatio = pixelCount > 0 ? (static_cast<double>(nonBlack) / static_cast<double>(pixelCount)) : 0.0;
    stats.colorfulRatio = pixelCount > 0 ? (static_cast<double>(colorful) / static_cast<double>(pixelCount)) : 0.0;
    stats.saturatedRatio = pixelCount > 0 ? (static_cast<double>(saturated) / static_cast<double>(pixelCount)) : 0.0;
    stats.nearWhiteRatio = pixelCount > 0 ? (static_cast<double>(nearWhite) / static_cast<double>(pixelCount)) : 0.0;
    stats.darkDetailRatio = pixelCount > 0 ? (static_cast<double>(darkDetail) / static_cast<double>(pixelCount)) : 0.0;
    stats.centerAvgLuma = centerPixelCount > 0 ? (centerLumaTotal / static_cast<double>(centerPixelCount)) : 0.0;
    return stats;
}

nlohmann::json VisualStatsToJson(const VisualValidationStats& stats) {
    return {
        {"valid", stats.valid},
        {"reason", stats.reason},
        {"width", stats.width},
        {"height", stats.height},
        {"avg_luma", stats.avgLuma},
        {"nonblack_ratio", stats.nonBlackRatio},
        {"colorful_ratio", stats.colorfulRatio},
        {"saturated_ratio", stats.saturatedRatio},
        {"near_white_ratio", stats.nearWhiteRatio},
        {"dark_detail_ratio", stats.darkDetailRatio},
        {"center_avg_luma", stats.centerAvgLuma}
    };
}

} // namespace Cortex
