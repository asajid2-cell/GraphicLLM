#include "Graphics/TextureAdmission.h"

#include <algorithm>

namespace Cortex::Graphics {

uint64_t EstimateTextureBytes(uint32_t width,
                              uint32_t height,
                              uint32_t mipLevels,
                              DXGI_FORMAT format) {
    if (width == 0 || height == 0 || mipLevels == 0) {
        return 0;
    }

    const auto isBC = [](DXGI_FORMAT fmt) {
        switch (fmt) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
        }
    };

    const auto blockSize = [](DXGI_FORMAT fmt) -> uint32_t {
        switch (fmt) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return 8;
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 16;
        default:
            return 16;
        }
    };

    const auto bytesPerPixel = [](DXGI_FORMAT fmt) -> uint32_t {
        switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 8;
        default:
            return 4;
        }
    };

    uint64_t total = 0;
    uint32_t w = width;
    uint32_t h = height;
    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        if (isBC(format)) {
            const uint32_t bw = (w + 3u) / 4u;
            const uint32_t bh = (h + 3u) / 4u;
            total += static_cast<uint64_t>(bw) * static_cast<uint64_t>(bh) * blockSize(format);
        } else {
            total += static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * bytesPerPixel(format);
        }
        w = std::max(1u, w >> 1);
        h = std::max(1u, h >> 1);
    }
    return total;
}

TextureMipAdmission ChooseTextureMipAdmission(uint32_t width,
                                              uint32_t height,
                                              uint32_t mipLevels,
                                              DXGI_FORMAT format,
                                              const RendererBudgetPlan& budget,
                                              uint64_t residentTextureBytes) {
    TextureMipAdmission plan{};
    plan.width = width;
    plan.height = height;
    plan.fullBytes = EstimateTextureBytes(width, height, mipLevels, format);
    plan.admittedBytes = plan.fullBytes;

    if (width == 0 || height == 0 || mipLevels <= 1) {
        return plan;
    }

    const uint32_t maxDimension = std::max(1u, budget.materialTextureMaxDimension);
    const uint32_t floorDimension = std::max(1u, budget.materialTextureBudgetFloorDimension);
    auto dimsAfterSkip = [&](uint32_t skip) {
        return std::pair<uint32_t, uint32_t>{
            std::max(1u, width >> skip),
            std::max(1u, height >> skip)
        };
    };

    uint32_t firstMip = 0;
    while (firstMip + 1u < mipLevels) {
        const auto [w, h] = dimsAfterSkip(firstMip);
        if (std::max(w, h) <= maxDimension) {
            break;
        }
        ++firstMip;
    }

    const uint64_t textureBudget = budget.textureBudgetBytes;
    const uint64_t remainingBudget =
        (textureBudget > residentTextureBytes) ? (textureBudget - residentTextureBytes) : 0ull;
    while (textureBudget > 0 && firstMip + 1u < mipLevels) {
        const auto [w, h] = dimsAfterSkip(firstMip);
        const uint32_t remainingMips = mipLevels - firstMip;
        const uint64_t admitted = EstimateTextureBytes(w, h, remainingMips, format);
        if (admitted <= remainingBudget) {
            break;
        }

        const auto [nextW, nextH] = dimsAfterSkip(firstMip + 1u);
        if (std::max(nextW, nextH) < floorDimension) {
            break;
        }
        ++firstMip;
    }

    const auto [finalW, finalH] = dimsAfterSkip(firstMip);
    plan.firstMip = firstMip;
    plan.width = finalW;
    plan.height = finalH;
    plan.admittedBytes = EstimateTextureBytes(finalW, finalH, mipLevels - firstMip, format);
    return plan;
}

} // namespace Cortex::Graphics
