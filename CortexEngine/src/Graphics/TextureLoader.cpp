#include "TextureLoader.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>
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

namespace {

// DDS structures based on the DirectX 9/10 DDS spec. Only the fields required
// for basic BCn 2D textures are modeled here.

constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "

struct DDS_PIXELFORMAT {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDS_HEADER {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DDS_HEADER_DXT10 {
    uint32_t dxgiFormat;        // DXGI_FORMAT
    uint32_t resourceDimension; // D3D10_RESOURCE_DIMENSION
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};

enum DDSFlags : uint32_t {
    DDS_FOURCC = 0x00000004u,
};

// FOURCC helpers
constexpr uint32_t MakeFourCC(char c0, char c1, char c2, char c3) {
    return static_cast<uint32_t>(static_cast<uint8_t>(c0)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c1)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c2)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c3)) << 24);
}

} // namespace

Result<CompressedImage> TextureLoader::LoadDDSCompressed(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Result<CompressedImage>::Err("Failed to open DDS file: " + path);
    }

    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file || magic != DDS_MAGIC) {
        return Result<CompressedImage>::Err("Invalid or non-DDS file: " + path);
    }

    DDS_HEADER header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.size != sizeof(DDS_HEADER) || header.ddspf.size != sizeof(DDS_PIXELFORMAT)) {
        return Result<CompressedImage>::Err("Invalid DDS header in: " + path);
    }

    // Determine if we have a DX10 header.
    bool hasDX10 = false;
    DDS_HEADER_DXT10 headerDX10{};
    if (header.ddspf.flags & DDS_FOURCC) {
        if (header.ddspf.fourCC == MakeFourCC('D', 'X', '1', '0')) {
            hasDX10 = true;
            file.read(reinterpret_cast<char*>(&headerDX10), sizeof(headerDX10));
            if (!file) {
                return Result<CompressedImage>::Err("Failed to read DDS DX10 header: " + path);
            }
        }
    }

    CompressedFormat format = CompressedFormat::Unknown;

    if (hasDX10) {
        // Map a small subset of DXGI formats used in the offline pipeline.
        switch (headerDX10.dxgiFormat) {
        case 71: /* DXGI_FORMAT_BC1_UNORM */
            format = CompressedFormat::BC1_UNORM;
            break;
        case 72: /* DXGI_FORMAT_BC1_UNORM_SRGB */
            format = CompressedFormat::BC1_UNORM_SRGB;
            break;
        case 77: /* DXGI_FORMAT_BC3_UNORM */
            format = CompressedFormat::BC3_UNORM;
            break;
        case 78: /* DXGI_FORMAT_BC3_UNORM_SRGB */
            format = CompressedFormat::BC3_UNORM_SRGB;
            break;
        case 83: /* DXGI_FORMAT_BC5_UNORM */
            format = CompressedFormat::BC5_UNORM;
            break;
        case 95: /* DXGI_FORMAT_BC6H_UF16 */
            format = CompressedFormat::BC6H_UF16;
            break;
        case 98: /* DXGI_FORMAT_BC7_UNORM */
            format = CompressedFormat::BC7_UNORM;
            break;
        case 99: /* DXGI_FORMAT_BC7_UNORM_SRGB */
            format = CompressedFormat::BC7_UNORM_SRGB;
            break;
        default:
            return Result<CompressedImage>::Err("Unsupported BC format in DDS (DX10) for: " + path);
        }
    } else {
        // Legacy fourCC mapping for common BCn formats.
        if (!(header.ddspf.flags & DDS_FOURCC)) {
            return Result<CompressedImage>::Err("DDS lacks FOURCC/DX10 header: " + path);
        }
        switch (header.ddspf.fourCC) {
        case MakeFourCC('D', 'X', 'T', '1'):
            format = CompressedFormat::BC1_UNORM;
            break;
        case MakeFourCC('D', 'X', 'T', '3'):
            format = CompressedFormat::BC3_UNORM;
            break;
        case MakeFourCC('D', 'X', 'T', '5'):
            format = CompressedFormat::BC3_UNORM;
            break;
        case MakeFourCC('A', 'T', 'I', '2'): // BC5_UNORM
        case MakeFourCC('B', 'C', '5', 'U'):
            format = CompressedFormat::BC5_UNORM;
            break;
        default:
            return Result<CompressedImage>::Err("Unsupported DDS FOURCC for compressed texture: " + path);
        }
    }

    if (format == CompressedFormat::Unknown) {
        return Result<CompressedImage>::Err("Unrecognized compressed DDS format: " + path);
    }

    const uint32_t width = header.width;
    const uint32_t height = header.height;
    uint32_t mipCount = header.mipMapCount ? header.mipMapCount : 1u;

    // Read the remainder of the file into memory
    file.seekg(0, std::ios::end);
    std::streamoff fileEnd = file.tellg();
    std::streamoff dataStart = hasDX10
        ? static_cast<std::streamoff>(sizeof(uint32_t) + sizeof(DDS_HEADER) + sizeof(DDS_HEADER_DXT10))
        : static_cast<std::streamoff>(sizeof(uint32_t) + sizeof(DDS_HEADER));
    if (fileEnd <= dataStart) {
        return Result<CompressedImage>::Err("DDS file has no image data: " + path);
    }
    const size_t dataSize = static_cast<size_t>(fileEnd - dataStart);
    std::vector<uint8_t> buffer(dataSize);
    file.seekg(dataStart, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(dataSize));
    if (!file) {
        return Result<CompressedImage>::Err("Failed to read DDS image data: " + path);
    }

    // Derive per-mip sizes using BC block layout. This keeps the loader
    // independent of header.pitchOrLinearSize quirks.
    const uint32_t blockBytes =
        (format == CompressedFormat::BC1_UNORM ||
         format == CompressedFormat::BC1_UNORM_SRGB)
        ? 8u : 16u;

    CompressedImage img;
    img.width = width;
    img.height = height;
    img.mipLevels = mipCount;
    img.format = format;
    img.mipData.reserve(mipCount);

    size_t offset = 0;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        uint32_t blocksWide = std::max(1u, (mipWidth + 3u) / 4u);
        uint32_t blocksHigh = std::max(1u, (mipHeight + 3u) / 4u);
        size_t mipSize = static_cast<size_t>(blocksWide) * blocksHigh * blockBytes;

        if (offset + mipSize > buffer.size()) {
            return Result<CompressedImage>::Err("DDS image data truncated for mip " + std::to_string(mip) + ": " + path);
        }

        std::vector<uint8_t> mipBytes(mipSize);
        std::memcpy(mipBytes.data(), buffer.data() + offset, mipSize);
        img.mipData.push_back(std::move(mipBytes));

        offset += mipSize;
        mipWidth = std::max(1u, mipWidth / 2u);
        mipHeight = std::max(1u, mipHeight / 2u);
    }

    spdlog::info("Loaded compressed DDS '{}' ({}x{}, {} mips)", path, img.width, img.height, img.mipLevels);
    return Result<CompressedImage>::Ok(std::move(img));
}

} // namespace Cortex::Graphics
