#include "TextureLoader.h"
#include <spdlog/spdlog.h>
#ifndef CORTEX_STB_IMAGE_IMPLEMENTED
#define CORTEX_STB_IMAGE_IMPLEMENTED
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#endif
#include "llama.cpp/vendor/stb/stb_image.h"

namespace Cortex::Graphics {

static std::vector<uint8_t> Downsample2x2(const MipLevel& src) {
    uint32_t newW = std::max(1u, src.width / 2);
    uint32_t newH = std::max(1u, src.height / 2);
    std::vector<uint8_t> dst(static_cast<size_t>(newW) * newH * 4);

    for (uint32_t y = 0; y < newH; ++y) {
        for (uint32_t x = 0; x < newW; ++x) {
            uint32_t accum[4] = {0, 0, 0, 0};
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    uint32_t srcX = std::min(src.width - 1, x * 2 + dx);
                    uint32_t srcY = std::min(src.height - 1, y * 2 + dy);
                    size_t idx = (static_cast<size_t>(srcY) * src.width + srcX) * 4;
                    accum[0] += src.pixels[idx + 0];
                    accum[1] += src.pixels[idx + 1];
                    accum[2] += src.pixels[idx + 2];
                    accum[3] += src.pixels[idx + 3];
                }
            }
            size_t dstIdx = (static_cast<size_t>(y) * newW + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[dstIdx + c] = static_cast<uint8_t>(accum[c] / 4);
            }
        }
    }
    return dst;
}

Result<std::vector<MipLevel>> TextureLoader::LoadImageRGBAWithMips(const std::string& path, bool generateMips) {
    int width = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!data) {
        return Result<std::vector<MipLevel>>::Err("Failed to load image: " + path);
    }

    std::vector<MipLevel> levels;
    MipLevel base{};
    base.width = static_cast<uint32_t>(width);
    base.height = static_cast<uint32_t>(height);
    base.pixels.assign(data, data + static_cast<size_t>(base.width) * base.height * 4);
    levels.push_back(std::move(base));

    stbi_image_free(data);

    if (generateMips) {
        while (levels.back().width > 1 || levels.back().height > 1) {
            MipLevel next{};
            next.pixels = Downsample2x2(levels.back());
            next.width = std::max(1u, levels.back().width / 2);
            next.height = std::max(1u, levels.back().height / 2);
            levels.push_back(std::move(next));
        }
    }

    spdlog::info("Loaded texture '{}': {}x{} ({} mips)", path, levels.front().width, levels.front().height, levels.size());
    return Result<std::vector<MipLevel>>::Ok(std::move(levels));
}

} // namespace Cortex::Graphics
