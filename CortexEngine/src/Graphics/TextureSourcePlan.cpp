#include "Graphics/TextureSourcePlan.h"

#include "Graphics/TextureLoader.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

std::string LowerExtension(const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return {};
    }

    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

DXGI_FORMAT ToDXGI(TextureLoader::CompressedFormat fmt) {
    using F = TextureLoader::CompressedFormat;
    switch (fmt) {
    case F::BC1_UNORM:      return DXGI_FORMAT_BC1_UNORM;
    case F::BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case F::BC3_UNORM:      return DXGI_FORMAT_BC3_UNORM;
    case F::BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case F::BC5_UNORM:      return DXGI_FORMAT_BC5_UNORM;
    case F::BC6H_UF16:      return DXGI_FORMAT_BC6H_UF16;
    case F::BC7_UNORM:      return DXGI_FORMAT_BC7_UNORM;
    case F::BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
    default:                return DXGI_FORMAT_UNKNOWN;
    }
}

TextureMipAdmission MakeAdmission(uint32_t width,
                                  uint32_t height,
                                  uint32_t mipLevels,
                                  DXGI_FORMAT format,
                                  bool budgetMips,
                                  const RendererBudgetPlan& budget,
                                  uint64_t residentTextureBytes) {
    if (budgetMips) {
        return ChooseTextureMipAdmission(width, height, mipLevels, format, budget, residentTextureBytes);
    }

    TextureMipAdmission admission{};
    admission.width = width;
    admission.height = height;
    admission.fullBytes = EstimateTextureBytes(width, height, mipLevels, format);
    admission.admittedBytes = admission.fullBytes;
    return admission;
}

TextureSourcePlan MakePlaceholderPlan(const std::string& path) {
    TextureSourcePlan plan{};
    plan.requestedPath = path;
    plan.sourcePath = path;
    plan.encoding = TextureSourceEncoding::Placeholder;
    plan.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    plan.sourceWidth = 2;
    plan.sourceHeight = 2;
    plan.sourceMipLevels = 1;
    plan.admission.width = 2;
    plan.admission.height = 2;
    plan.admission.fullBytes = EstimateTextureBytes(2, 2, 1, plan.format);
    plan.admission.admittedBytes = plan.admission.fullBytes;
    return plan;
}

Result<TextureSourcePlan> TryCompressedPlan(const std::string& requestedPath,
                                            const std::string& sourcePath,
                                            bool usedSibling,
                                            bool preferCopyQueue,
                                            bool budgetMips,
                                            const RendererBudgetPlan& budget,
                                            uint64_t residentTextureBytes) {
    auto ddsResult = TextureLoader::LoadDDSCompressed(sourcePath);
    if (ddsResult.IsErr()) {
        return Result<TextureSourcePlan>::Err(ddsResult.Error());
    }

    const auto& img = ddsResult.Value();
    const DXGI_FORMAT compressedFormat = ToDXGI(img.format);
    if (compressedFormat == DXGI_FORMAT_UNKNOWN) {
        return Result<TextureSourcePlan>::Err("Unsupported compressed DDS format");
    }

    TextureSourcePlan plan{};
    plan.requestedPath = requestedPath;
    plan.sourcePath = sourcePath;
    plan.encoding = TextureSourceEncoding::DDSCompressed;
    plan.format = compressedFormat;
    plan.sourceWidth = img.width;
    plan.sourceHeight = img.height;
    plan.sourceMipLevels = img.mipLevels;
    plan.usedCompressedSibling = usedSibling;
    plan.preferCopyQueue = preferCopyQueue;
    plan.admission = MakeAdmission(
        img.width,
        img.height,
        img.mipLevels,
        compressedFormat,
        budgetMips,
        budget,
        residentTextureBytes);
    plan.mips.assign(
        img.mipData.begin() + static_cast<std::ptrdiff_t>(plan.admission.firstMip),
        img.mipData.end());
    return Result<TextureSourcePlan>::Ok(std::move(plan));
}

Result<TextureSourcePlan> BuildRGBAPlan(const std::string& path,
                                         bool useSRGB,
                                         bool budgetMips,
                                         const RendererBudgetPlan& budget,
                                         uint64_t residentTextureBytes) {
    auto imageResult = TextureLoader::LoadImageRGBAWithMips(path, true);
    if (imageResult.IsErr()) {
        return Result<TextureSourcePlan>::Err(imageResult.Error());
    }

    auto& levels = imageResult.Value();
    const DXGI_FORMAT rgbaFormat =
        useSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    const uint32_t sourceWidth = levels.front().width;
    const uint32_t sourceHeight = levels.front().height;
    const uint32_t sourceMipLevels = static_cast<uint32_t>(levels.size());
    const TextureMipAdmission admission = MakeAdmission(
        sourceWidth,
        sourceHeight,
        sourceMipLevels,
        rgbaFormat,
        budgetMips,
        budget,
        residentTextureBytes);

    TextureSourcePlan plan{};
    plan.requestedPath = path;
    plan.sourcePath = path;
    plan.encoding = TextureSourceEncoding::RGBA8;
    plan.format = rgbaFormat;
    plan.sourceWidth = sourceWidth;
    plan.sourceHeight = sourceHeight;
    plan.sourceMipLevels = sourceMipLevels;
    plan.admission = admission;
    plan.mips.reserve(levels.size() - admission.firstMip);
    for (auto it = levels.begin() + static_cast<std::ptrdiff_t>(admission.firstMip);
         it != levels.end();
         ++it) {
        plan.mips.push_back(std::move(it->pixels));
    }
    return Result<TextureSourcePlan>::Ok(std::move(plan));
}

} // namespace

Result<TextureSourcePlan> BuildTextureSourcePlan(const std::string& path,
                                                 bool useSRGB,
                                                 bool enableCompressedDDS,
                                                 bool budgetMips,
                                                 const RendererBudgetPlan& budget,
                                                 uint64_t residentTextureBytes) {
    const std::string ext = LowerExtension(path);

    if (enableCompressedDDS && ext == ".dds") {
        auto planResult = TryCompressedPlan(
            path,
            path,
            false,
            true,
            budgetMips,
            budget,
            residentTextureBytes);
        if (planResult.IsOk()) {
            auto plan = std::move(planResult).Value();
            if (plan.admission.firstMip > 0) {
                spdlog::info(
                    "Texture '{}' admitted at mip {} ({}x{}, {:.2f} MB -> {:.2f} MB, profile={})",
                    path,
                    plan.admission.firstMip,
                    plan.admission.width,
                    plan.admission.height,
                    static_cast<double>(plan.admission.fullBytes) / (1024.0 * 1024.0),
                    static_cast<double>(plan.admission.admittedBytes) / (1024.0 * 1024.0),
                    budget.profileName);
            }
            return Result<TextureSourcePlan>::Ok(std::move(plan));
        }

        spdlog::warn("Failed to load compressed DDS '{}': {}", path, planResult.Error());
        return Result<TextureSourcePlan>::Ok(MakePlaceholderPlan(path));
    }

    if (enableCompressedDDS) {
        std::filesystem::path original(path);
        std::filesystem::path sibling = original;
        sibling.replace_extension(".dds");
        if (std::filesystem::exists(sibling)) {
            const std::string siblingStr = sibling.string();
            auto planResult = TryCompressedPlan(
                path,
                siblingStr,
                true,
                false,
                budgetMips,
                budget,
                residentTextureBytes);
            if (planResult.IsOk()) {
                auto plan = std::move(planResult).Value();
                spdlog::info("Loaded compressed sibling '{}' for texture '{}'", siblingStr, path);
                if (plan.admission.firstMip > 0) {
                    spdlog::info(
                        "Texture '{}' admitted at mip {} via sibling ({}x{}, {:.2f} MB -> {:.2f} MB, profile={})",
                        path,
                        plan.admission.firstMip,
                        plan.admission.width,
                        plan.admission.height,
                        static_cast<double>(plan.admission.fullBytes) / (1024.0 * 1024.0),
                        static_cast<double>(plan.admission.admittedBytes) / (1024.0 * 1024.0),
                        budget.profileName);
                }
                return Result<TextureSourcePlan>::Ok(std::move(plan));
            }

            spdlog::warn(
                "Failed to load compressed sibling '{}' for '{}': {}; falling back to RGBA path",
                siblingStr,
                path,
                planResult.Error());
        }
    }

    if (ext == ".dds") {
        return Result<TextureSourcePlan>::Ok(MakePlaceholderPlan(path));
    }

    auto rgbaResult = BuildRGBAPlan(path, useSRGB, budgetMips, budget, residentTextureBytes);
    if (rgbaResult.IsOk()) {
        auto plan = std::move(rgbaResult).Value();
        if (plan.admission.firstMip > 0) {
            spdlog::info(
                "Texture '{}' admitted at mip {} ({}x{}, {:.2f} MB -> {:.2f} MB, profile={})",
                path,
                plan.admission.firstMip,
                plan.admission.width,
                plan.admission.height,
                static_cast<double>(plan.admission.fullBytes) / (1024.0 * 1024.0),
                static_cast<double>(plan.admission.admittedBytes) / (1024.0 * 1024.0),
                budget.profileName);
        }
        return Result<TextureSourcePlan>::Ok(std::move(plan));
    }
    return rgbaResult;
}

} // namespace Cortex::Graphics
