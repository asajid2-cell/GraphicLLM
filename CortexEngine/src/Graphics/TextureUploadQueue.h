#pragma once

#include "Graphics/AssetRegistry.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

namespace Cortex::Graphics {

struct TextureUploadQueueStats {
    uint64_t submittedJobs = 0;
    uint64_t completedJobs = 0;
    uint64_t failedJobs = 0;
    uint64_t pendingJobs = 0;
    uint64_t uploadedResidentBytes = 0;
    double totalUploadMs = 0.0;
    double lastUploadMs = 0.0;

    [[nodiscard]] double AverageUploadMs() const {
        return completedJobs > 0
            ? totalUploadMs / static_cast<double>(completedJobs)
            : 0.0;
    }
};

struct TextureUploadJob {
    uint64_t id = 0;
    std::string cacheKey;
    std::string path;
    bool useSRGB = false;
    AssetRegistry::TextureKind kind = AssetRegistry::TextureKind::Generic;
    bool budgetMips = false;
    bool registerAsset = true;
    uint64_t residentTextureBytesBefore = 0;
};

struct TextureUploadQueueState {
    TextureUploadQueueStats stats{};
    std::deque<TextureUploadJob> pendingJobs;
    std::unordered_set<std::string> pendingKeys;
    std::unordered_set<std::string> failedKeys;
    uint64_t nextJobId = 1;
    uint32_t maxJobsPerFrame = 16;

    void RefreshPendingCount() {
        stats.pendingJobs = static_cast<uint64_t>(pendingJobs.size());
    }
};

} // namespace Cortex::Graphics
