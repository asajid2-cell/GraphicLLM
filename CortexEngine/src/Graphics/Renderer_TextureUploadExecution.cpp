#include "Renderer.h"

#include "Graphics/TextureSourcePlan.h"

#include <chrono>
#include <string>

namespace Cortex::Graphics {

void Renderer::RecordTextureUploadResult(const TextureSourcePlan& plan,
                                         double uploadMs,
                                         bool succeeded) {
    auto& stats = m_assetRuntime.textureUploads.queue.stats;
    if (stats.pendingJobs > 0) {
        --stats.pendingJobs;
    }

    stats.lastUploadMs = uploadMs;
    if (succeeded) {
        ++stats.completedJobs;
        stats.uploadedResidentBytes += plan.admission.admittedBytes;
        stats.totalUploadMs += uploadMs;
    } else {
        ++stats.failedJobs;
    }
}

Result<DX12Texture> Renderer::ExecuteTextureUpload(const TextureSourcePlan& plan) {
    ++m_assetRuntime.textureUploads.queue.stats.submittedJobs;
    ++m_assetRuntime.textureUploads.queue.stats.pendingJobs;

    const auto started = std::chrono::steady_clock::now();
    auto finish = [&](bool succeeded) {
        const auto finished = std::chrono::steady_clock::now();
        const double uploadMs =
            std::chrono::duration<double, std::milli>(finished - started).count();
        RecordTextureUploadResult(plan, uploadMs, succeeded);
    };

    DX12Texture texture;

    if (plan.encoding == TextureSourceEncoding::DDSCompressed) {
        auto initCompressed = texture.InitializeFromCompressedMipChain(
            m_services.device->GetDevice(),
            plan.preferCopyQueue && m_services.uploadQueue ? m_services.uploadQueue->GetCommandQueue() : nullptr,
            m_services.commandQueue->GetCommandQueue(),
            plan.mips,
            plan.admission.width,
            plan.admission.height,
            plan.format,
            plan.sourcePath);
        if (initCompressed.IsErr()) {
            finish(false);
            return Result<DX12Texture>::Err(
                "Failed to initialize compressed texture '" + plan.sourcePath + "': " +
                initCompressed.Error());
        }
        finish(true);
        return Result<DX12Texture>::Ok(std::move(texture));
    }

    if (plan.encoding == TextureSourceEncoding::RGBA8) {
        auto initResult = texture.InitializeFromMipChain(
            m_services.device->GetDevice(),
            nullptr,
            m_services.commandQueue->GetCommandQueue(),
            plan.mips,
            plan.admission.width,
            plan.admission.height,
            plan.format,
            plan.sourcePath);
        if (initResult.IsErr()) {
            finish(false);
            return Result<DX12Texture>::Err(initResult.Error());
        }
        finish(true);
        return Result<DX12Texture>::Ok(std::move(texture));
    }

    if (plan.encoding == TextureSourceEncoding::Placeholder) {
        float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        auto placeholderResult = DX12Texture::CreatePlaceholder(
            m_services.device->GetDevice(),
            nullptr,
            m_services.commandQueue->GetCommandQueue(),
            2,
            2,
            white);
        if (placeholderResult.IsErr()) {
            finish(false);
            return Result<DX12Texture>::Err(
                "Failed to create placeholder texture for '" + plan.requestedPath + "': " +
                placeholderResult.Error());
        }
        finish(true);
        return Result<DX12Texture>::Ok(std::move(placeholderResult).Value());
    }

    finish(false);
    return Result<DX12Texture>::Err(
        "Unsupported texture source plan for '" + plan.requestedPath + "'");
}

} // namespace Cortex::Graphics
