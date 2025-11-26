#include "TextureLoader.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#ifndef CORTEX_STB_IMAGE_IMPLEMENTED
#define CORTEX_STB_IMAGE_IMPLEMENTED
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#endif
#include "llama.cpp/vendor/stb/stb_image.h"

// TinyEXR can optionally use the zlib routines from stb_image/stb_image_write.
// We only need EXR *reading*, so it is safe to provide a minimal stub for the
// compression function (used only when writing EXR files).
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality) {
    if (out_len) {
        *out_len = 0;
    }
    return nullptr;
}

// Configure TinyEXR to use stb's zlib implementation instead of miniz to avoid
// external miniz header dependencies.
#define TINYEXR_USE_MINIZ   0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

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

    std::vector<MipLevel> levels;
    MipLevel base{};

    // Detect file format by extension
    bool isHDR = false;
    bool isEXR = false;
    {
        if (path.size() >= 4) {
            std::string ext = path.substr(path.size() - 4);
            for (char& c : ext) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext == ".hdr") {
                isHDR = true;
            } else if (ext == ".exr") {
                isEXR = true;
            }
        }
        if (!isHDR && !isEXR && stbi_is_hdr(path.c_str())) {
            isHDR = true;
        }
    }

    // OpenEXR path: load via TinyEXR
    if (isEXR) {
        float* rgba = nullptr;
        const char* err = nullptr;

        int ret = LoadEXR(&rgba, &width, &height, path.c_str(), &err);
        if (ret != TINYEXR_SUCCESS) {
            std::string errorMsg = err ? std::string(err) : "Unknown error";
            if (err) {
                FreeEXRErrorMessage(err);
            }
            return Result<std::vector<MipLevel>>::Err("Failed to load EXR image: " + path + " (" + errorMsg + ")");
        }

        base.width = static_cast<uint32_t>(width);
        base.height = static_cast<uint32_t>(height);
        base.pixels.resize(static_cast<size_t>(base.width) * base.height * 4);

        const size_t pixelCount = static_cast<size_t>(base.width) * base.height;

        auto tonemap = [](float v) -> float {
            v = std::max(v, 0.0f);
            return v / (1.0f + v);
        };

        for (size_t i = 0; i < pixelCount; ++i) {
            float r = tonemap(rgba[i * 4 + 0]);
            float g = tonemap(rgba[i * 4 + 1]);
            float b = tonemap(rgba[i * 4 + 2]);
            float a = std::clamp(rgba[i * 4 + 3], 0.0f, 1.0f);

            base.pixels[i * 4 + 0] = static_cast<uint8_t>(std::round(std::clamp(r, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 1] = static_cast<uint8_t>(std::round(std::clamp(g, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 2] = static_cast<uint8_t>(std::round(std::clamp(b, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 3] = static_cast<uint8_t>(std::round(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        }

        free(rgba);
    }
    // Radiance HDR path: Load using stb_image
    else if (isHDR) {
        float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);
        if (!data) {
            return Result<std::vector<MipLevel>>::Err("Failed to load HDR image: " + path);
        }

        base.width = static_cast<uint32_t>(width);
        base.height = static_cast<uint32_t>(height);
        base.pixels.resize(static_cast<size_t>(base.width) * base.height * 4);

        const size_t pixelCount = static_cast<size_t>(base.width) * base.height;

        // Tonemap and convert to 8-bit RGBA
        auto tonemap = [](float v) -> float {
            v = std::max(v, 0.0f);
            return v / (1.0f + v);
        };

        for (size_t i = 0; i < pixelCount; ++i) {
            float r = tonemap(data[i * 4 + 0]);
            float g = tonemap(data[i * 4 + 1]);
            float b = tonemap(data[i * 4 + 2]);
            float a = std::clamp(data[i * 4 + 3], 0.0f, 1.0f);

            base.pixels[i * 4 + 0] = static_cast<uint8_t>(std::round(std::clamp(r, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 1] = static_cast<uint8_t>(std::round(std::clamp(g, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 2] = static_cast<uint8_t>(std::round(std::clamp(b, 0.0f, 1.0f) * 255.0f));
            base.pixels[i * 4 + 3] = static_cast<uint8_t>(std::round(std::clamp(a, 0.0f, 1.0f) * 255.0f));
        }

        stbi_image_free(data);
    } else {
        // LDR path (PNG/JPEG/etc.): standard 8-bit load.
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!data) {
            return Result<std::vector<MipLevel>>::Err("Failed to load image: " + path);
        }

        base.width = static_cast<uint32_t>(width);
        base.height = static_cast<uint32_t>(height);
        base.pixels.assign(data, data + static_cast<size_t>(base.width) * base.height * 4);

        stbi_image_free(data);
    }

    levels.push_back(std::move(base));

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
