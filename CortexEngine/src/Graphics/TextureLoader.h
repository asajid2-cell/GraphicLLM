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

class TextureLoader {
public:
    // Loads an image from disk and returns RGBA8 pixels with generated mip chain (including base level).
    static Result<std::vector<MipLevel>> LoadImageRGBAWithMips(const std::string& path, bool generateMips = true);
};

} // namespace Cortex::Graphics
