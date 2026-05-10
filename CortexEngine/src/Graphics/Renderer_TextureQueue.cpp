#include "Renderer.h"

#include "Graphics/TextureSourcePlan.h"
#include "Graphics/TextureUploadTicket.h"

#include <algorithm>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

bool Renderer::TryGetCachedTexture(const std::string& path,
                                   bool useSRGB,
                                   AssetRegistry::TextureKind kind,
                                   std::shared_ptr<DX12Texture>& outTexture) const {
    const std::string cacheKey = BuildTextureCacheKey(path, useSRGB, kind);
    auto it = m_assetRuntime.textureCache.find(cacheKey);
    if (it == m_assetRuntime.textureCache.end()) {
        return false;
    }

    outTexture = it->second;
    return static_cast<bool>(outTexture);
}

bool Renderer::IsTextureUploadPending(const std::string& path,
                                      bool useSRGB,
                                      AssetRegistry::TextureKind kind) const {
    const std::string cacheKey = BuildTextureCacheKey(path, useSRGB, kind);
    return m_assetRuntime.textureUploads.queue.pendingKeys.find(cacheKey) != m_assetRuntime.textureUploads.queue.pendingKeys.end();
}

bool Renderer::IsTextureUploadFailed(const std::string& path,
                                     bool useSRGB,
                                     AssetRegistry::TextureKind kind) const {
    const std::string cacheKey = BuildTextureCacheKey(path, useSRGB, kind);
    return m_assetRuntime.textureUploads.queue.failedKeys.find(cacheKey) != m_assetRuntime.textureUploads.queue.failedKeys.end();
}

Result<void> Renderer::QueueTextureUploadFromFile(const std::string& path,
                                                  bool useSRGB,
                                                  AssetRegistry::TextureKind kind) {
    if (path.empty()) {
        return Result<void>::Err("Empty texture path");
    }

    if (!m_services.device || !m_services.commandQueue || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer is not initialized");
    }

    const std::string cacheKey = BuildTextureCacheKey(path, useSRGB, kind);
    if (m_assetRuntime.textureCache.find(cacheKey) != m_assetRuntime.textureCache.end()) {
        return Result<void>::Ok();
    }
    if (m_assetRuntime.textureUploads.queue.pendingKeys.find(cacheKey) != m_assetRuntime.textureUploads.queue.pendingKeys.end()) {
        return Result<void>::Ok();
    }
    if (m_assetRuntime.textureUploads.queue.failedKeys.find(cacheKey) != m_assetRuntime.textureUploads.queue.failedKeys.end()) {
        return Result<void>::Ok();
    }

    const auto memoryBefore = m_assetRuntime.registry.GetMemoryBreakdown();
    TextureUploadJob job{};
    job.id = m_assetRuntime.textureUploads.queue.nextJobId++;
    job.cacheKey = cacheKey;
    job.path = path;
    job.useSRGB = useSRGB;
    job.kind = kind;
    job.budgetMips = kind == AssetRegistry::TextureKind::Generic;
    job.registerAsset = true;
    job.residentTextureBytesBefore =
        (kind == AssetRegistry::TextureKind::Environment)
            ? memoryBefore.environmentBytes
            : memoryBefore.textureBytes;

    m_assetRuntime.textureUploads.queue.pendingJobs.push_back(std::move(job));
    m_assetRuntime.textureUploads.queue.pendingKeys.insert(cacheKey);
    m_assetRuntime.textureUploads.queue.RefreshPendingCount();
    return Result<void>::Ok();
}

void Renderer::ProcessTextureUploadJobsPerFrame(uint32_t maxJobs) {
    if (m_frameLifecycle.deviceRemoved || maxJobs == 0) {
        return;
    }

    uint32_t processed = 0;
    constexpr bool kEnableCompressedDDS = true;
    while (!m_assetRuntime.textureUploads.queue.pendingJobs.empty() && processed < maxJobs) {
        TextureUploadJob job = std::move(m_assetRuntime.textureUploads.queue.pendingJobs.front());
        m_assetRuntime.textureUploads.queue.pendingJobs.pop_front();
        m_assetRuntime.textureUploads.queue.pendingKeys.erase(job.cacheKey);
        m_assetRuntime.textureUploads.queue.RefreshPendingCount();

        if (m_assetRuntime.textureCache.find(job.cacheKey) != m_assetRuntime.textureCache.end()) {
            ++processed;
            continue;
        }

        auto planResult = BuildTextureSourcePlan(
            job.path,
            job.useSRGB,
            kEnableCompressedDDS,
            job.budgetMips,
            m_framePlanning.budgetPlan,
            job.residentTextureBytesBefore);
        if (planResult.IsErr()) {
            ++m_assetRuntime.textureUploads.queue.stats.submittedJobs;
            ++m_assetRuntime.textureUploads.queue.stats.failedJobs;
            m_assetRuntime.textureUploads.queue.failedKeys.insert(job.cacheKey);
            spdlog::warn("Texture upload job {} failed planning '{}': {}",
                         job.id,
                         job.path,
                         planResult.Error());
            ++processed;
            continue;
        }

        auto plan = std::move(planResult).Value();
        auto uploadResult = ExecuteTextureUpload(plan);
        if (uploadResult.IsErr()) {
            m_assetRuntime.textureUploads.queue.failedKeys.insert(job.cacheKey);
            spdlog::warn("Texture upload job {} failed uploading '{}': {}",
                         job.id,
                         job.path,
                         uploadResult.Error());
            ++processed;
            continue;
        }

        TextureUploadTicket ticket{};
        ticket.texture = std::move(uploadResult).Value();
        ticket.receipt = BuildTextureReceiptFromPlan(
            plan,
            job.path,
            job.useSRGB,
            job.kind,
            job.residentTextureBytesBefore);
        ticket.publishContext = "queued texture '" + job.path + "'";
        ticket.registerAsset = job.registerAsset;

        auto publishResult = PublishTexture(std::move(ticket));
        if (publishResult.IsErr()) {
            ++m_assetRuntime.textureUploads.queue.stats.failedJobs;
            m_assetRuntime.textureUploads.queue.failedKeys.insert(job.cacheKey);
            spdlog::warn("Texture upload job {} failed publishing '{}': {}",
                         job.id,
                         job.path,
                         publishResult.Error());
            ++processed;
            continue;
        }

        m_assetRuntime.textureCache[job.cacheKey] = publishResult.Value();
        ++processed;
    }

    m_assetRuntime.textureUploads.queue.RefreshPendingCount();
}

} // namespace Cortex::Graphics
