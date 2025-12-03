#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct MipLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8
};

// Minimal description of a pre-compressed texture (BCn / BC6H). The loader
// produces tightly packed mip levels without expanding to RGBA8 so the
// renderer can upload the BC blocks directly to the GPU.
enum class CompressedFormat {
    Unknown = 0,
    BC1_UNORM,
    BC1_UNORM_SRGB,
    BC3_UNORM,
    BC3_UNORM_SRGB,
    BC5_UNORM,
    BC6H_UF16,
    BC7_UNORM,
    BC7_UNORM_SRGB,
};

struct CompressedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 0;
    CompressedFormat format = CompressedFormat::Unknown;
    // One entry per mip level, base level first. Each mip is tightly packed
    // BC blocks with no per-row padding; callers are expected to use
    // GetCopyableFootprints to compute GPU footprints.
    std::vector<std::vector<uint8_t>> mipData;
};

class TextureLoader {
public:
    // Expose the compressed-format enum through the loader type so callers
    // can refer to TextureLoader::CompressedFormat without depending on
    // the global alias in this header.
    using CompressedFormat = Cortex::Graphics::CompressedFormat;

    // Loads an image from disk and returns RGBA8 pixels with generated mip chain (including base level).
    static Result<std::vector<MipLevel>> LoadImageRGBAWithMips(const std::string& path, bool generateMips = true);

    // Load a DDS file that contains pre-compressed BCn / BC6H data
    // (BC1/BC3/BC5/BC6H/BC7). This function does not expand to RGBA8; instead
    // it returns the raw compressed mip data so the caller can upload it
    // directly.
    static Result<CompressedImage> LoadDDSCompressed(const std::string& path);
};

} // namespace Cortex::Graphics
