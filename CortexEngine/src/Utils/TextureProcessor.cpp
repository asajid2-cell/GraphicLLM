// TextureProcessor.cpp
// Implementation of texture processing utilities.

#include "TextureProcessor.h"
#include <algorithm>
#include <cmath>
#include <fstream>

// Note: In production, you'd use stb_image for loading and DirectXTex for BC compression
// This implementation provides the interface and basic functionality

namespace Cortex::Utils {

// ============================================================================
// TextureData
// ============================================================================

size_t TextureData::GetMipOffset(uint32_t mipLevel) const {
    if (mipLevel == 0) return 0;

    size_t offset = 0;
    uint32_t w = width, h = height;
    uint32_t bpp = GetBytesPerPixel(format);

    for (uint32_t i = 0; i < mipLevel && (w > 1 || h > 1); i++) {
        if (IsCompressed(format)) {
            uint32_t blockSize = GetBlockSize(format);
            uint32_t blocksX = (w + 3) / 4;
            uint32_t blocksY = (h + 3) / 4;
            offset += blocksX * blocksY * blockSize;
        } else {
            offset += w * h * bpp;
        }
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    return offset;
}

uint32_t TextureData::GetMipWidth(uint32_t mipLevel) const {
    return std::max(1u, width >> mipLevel);
}

uint32_t TextureData::GetMipHeight(uint32_t mipLevel) const {
    return std::max(1u, height >> mipLevel);
}

uint32_t TextureData::GetBytesPerPixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8_UNORM: return 1;
        case TextureFormat::RG8_UNORM: return 2;
        case TextureFormat::RGBA8_UNORM:
        case TextureFormat::RGBA8_SRGB: return 4;
        case TextureFormat::R16_FLOAT: return 2;
        case TextureFormat::RG16_FLOAT: return 4;
        case TextureFormat::RGBA16_FLOAT: return 8;
        case TextureFormat::R32_FLOAT: return 4;
        case TextureFormat::RG32_FLOAT: return 8;
        case TextureFormat::RGBA32_FLOAT: return 16;
        default: return 4;  // Assume RGBA8 for unknown
    }
}

uint32_t TextureData::GetBlockSize(TextureFormat format) {
    switch (format) {
        case TextureFormat::BC1_UNORM:
        case TextureFormat::BC1_SRGB:
        case TextureFormat::BC4_UNORM:
        case TextureFormat::BC4_SNORM:
            return 8;
        case TextureFormat::BC3_UNORM:
        case TextureFormat::BC3_SRGB:
        case TextureFormat::BC5_UNORM:
        case TextureFormat::BC5_SNORM:
        case TextureFormat::BC6H_UF16:
        case TextureFormat::BC6H_SF16:
        case TextureFormat::BC7_UNORM:
        case TextureFormat::BC7_SRGB:
            return 16;
        default: return 0;
    }
}

bool TextureData::IsCompressed(TextureFormat format) {
    return format >= TextureFormat::BC1_UNORM && format <= TextureFormat::BC7_SRGB;
}

// ============================================================================
// TextureProcessor
// ============================================================================

TextureProcessor::TextureProcessor() {}

bool TextureProcessor::LoadTexture(const std::string& path, TextureData& outData) {
    // In production, use stb_image or similar
    // This is a placeholder implementation

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // TODO: Implement actual image loading (PNG, JPG, TGA, HDR, DDS)
    // For now, return false as this requires external libraries

    return false;
}

bool TextureProcessor::SaveTexture(const std::string& path, const TextureData& data) {
    // TODO: Implement actual image saving
    // Would use stb_image_write or similar

    return false;
}

TextureData TextureProcessor::ProcessTexture(const TextureData& input,
                                               const TextureProcessingSettings& settings) {
    TextureData result = input;

    if (m_progressCallback) {
        m_progressCallback(0.0f, "Starting texture processing");
    }

    // Resize if needed
    uint32_t targetWidth = std::min(result.width, settings.maxWidth);
    uint32_t targetHeight = std::min(result.height, settings.maxHeight);

    if (settings.powerOfTwo) {
        // Round up to next power of two
        targetWidth = 1u << static_cast<uint32_t>(std::ceil(std::log2(targetWidth)));
        targetHeight = 1u << static_cast<uint32_t>(std::ceil(std::log2(targetHeight)));
    }

    if (targetWidth != result.width || targetHeight != result.height) {
        if (m_progressCallback) {
            m_progressCallback(0.1f, "Resizing texture");
        }
        result = Resize(result, targetWidth, targetHeight, settings.mipmapFilter);
    }

    // Normal map processing
    if (settings.textureType == TextureType::Normal) {
        if (settings.normalizeNormals) {
            result = NormalizeNormalMap(result);
        }
        if (settings.normalMapFlipY) {
            result = FlipNormalMapY(result);
        }
    }

    // sRGB conversion
    if (settings.inputSRGB && !settings.outputSRGB) {
        ConvertSRGBToLinear(result.pixels, 4);
    } else if (!settings.inputSRGB && settings.outputSRGB) {
        ConvertLinearToSRGB(result.pixels, 4);
    }

    // Generate mipmaps
    if (settings.generateMipmaps) {
        if (m_progressCallback) {
            m_progressCallback(0.3f, "Generating mipmaps");
        }
        result = GenerateMipmaps(result, settings.mipmapFilter);

        if (settings.maxMipLevels > 0 && result.mipLevels > settings.maxMipLevels) {
            result.mipLevels = settings.maxMipLevels;
            // Truncate pixel data
        }
    }

    // Compress
    if (TextureData::IsCompressed(settings.targetFormat)) {
        if (m_progressCallback) {
            m_progressCallback(0.5f, "Compressing texture");
        }
        result = CompressTexture(result, settings.targetFormat, settings.compressionQuality);
    } else if (result.format != settings.targetFormat) {
        result = ConvertFormat(result, settings.targetFormat);
    }

    if (m_progressCallback) {
        m_progressCallback(1.0f, "Processing complete");
    }

    return result;
}

TextureData TextureProcessor::GenerateMipmaps(const TextureData& input, MipmapFilter filter) {
    if (input.width <= 1 && input.height <= 1) {
        return input;
    }

    TextureData result;
    result.width = input.width;
    result.height = input.height;
    result.format = input.format;

    uint32_t channels = TextureData::GetBytesPerPixel(input.format);

    // Calculate number of mip levels
    result.mipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(std::max(input.width, input.height))));

    // Reserve space for all mips
    size_t totalSize = 0;
    uint32_t w = input.width, h = input.height;
    for (uint32_t i = 0; i < result.mipLevels; i++) {
        totalSize += w * h * channels;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    result.pixels.resize(totalSize);

    // Copy mip 0
    std::copy(input.pixels.begin(), input.pixels.end(), result.pixels.begin());

    // Generate remaining mips
    size_t srcOffset = 0;
    size_t dstOffset = input.width * input.height * channels;
    w = input.width;
    h = input.height;

    for (uint32_t mip = 1; mip < result.mipLevels; mip++) {
        uint32_t newW = std::max(1u, w / 2);
        uint32_t newH = std::max(1u, h / 2);

        std::vector<uint8_t> mipData = GenerateMipLevel(
            result.pixels.data() + srcOffset, w, h, channels, filter);

        std::copy(mipData.begin(), mipData.end(), result.pixels.begin() + dstOffset);

        srcOffset = dstOffset;
        dstOffset += newW * newH * channels;
        w = newW;
        h = newH;
    }

    return result;
}

std::vector<uint8_t> TextureProcessor::GenerateMipLevel(const uint8_t* srcPixels,
                                                          uint32_t srcWidth, uint32_t srcHeight,
                                                          uint32_t channels, MipmapFilter filter) {
    uint32_t dstWidth = std::max(1u, srcWidth / 2);
    uint32_t dstHeight = std::max(1u, srcHeight / 2);

    std::vector<uint8_t> result(dstWidth * dstHeight * channels);

    for (uint32_t y = 0; y < dstHeight; y++) {
        for (uint32_t x = 0; x < dstWidth; x++) {
            uint32_t srcX = x * 2;
            uint32_t srcY = y * 2;

            for (uint32_t c = 0; c < channels; c++) {
                // Box filter (simple 2x2 average)
                float sum = 0.0f;
                int count = 0;

                for (int dy = 0; dy < 2 && srcY + dy < srcHeight; dy++) {
                    for (int dx = 0; dx < 2 && srcX + dx < srcWidth; dx++) {
                        uint32_t idx = ((srcY + dy) * srcWidth + (srcX + dx)) * channels + c;
                        sum += srcPixels[idx];
                        count++;
                    }
                }

                uint32_t dstIdx = (y * dstWidth + x) * channels + c;
                result[dstIdx] = static_cast<uint8_t>(sum / count);
            }
        }
    }

    return result;
}

TextureData TextureProcessor::CompressTexture(const TextureData& input, TextureFormat targetFormat,
                                                 float quality) {
    // Note: Full BC compression requires a proper library like DirectXTex or bc7enc
    // This is a simplified implementation

    TextureData result;
    result.width = input.width;
    result.height = input.height;
    result.mipLevels = input.mipLevels;
    result.format = targetFormat;

    uint32_t blockSize = TextureData::GetBlockSize(targetFormat);
    if (blockSize == 0) {
        return input;  // Not a compressed format
    }

    // Calculate total size for compressed data
    size_t totalSize = 0;
    uint32_t w = input.width, h = input.height;
    for (uint32_t mip = 0; mip < input.mipLevels; mip++) {
        uint32_t blocksX = (w + 3) / 4;
        uint32_t blocksY = (h + 3) / 4;
        totalSize += blocksX * blocksY * blockSize;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    result.pixels.resize(totalSize);

    // Compress each mip level
    size_t srcOffset = 0;
    size_t dstOffset = 0;
    w = input.width;
    h = input.height;

    for (uint32_t mip = 0; mip < input.mipLevels; mip++) {
        uint32_t blocksX = (w + 3) / 4;
        uint32_t blocksY = (h + 3) / 4;

        for (uint32_t by = 0; by < blocksY; by++) {
            for (uint32_t bx = 0; bx < blocksX; bx++) {
                // Extract 4x4 block
                uint8_t block[64];  // 4x4 RGBA
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        uint32_t px = std::min(bx * 4 + x, w - 1);
                        uint32_t py = std::min(by * 4 + y, h - 1);
                        uint32_t srcIdx = srcOffset + (py * w + px) * 4;
                        uint32_t dstIdx = (y * 4 + x) * 4;

                        for (int c = 0; c < 4; c++) {
                            block[dstIdx + c] = input.pixels[srcIdx + c];
                        }
                    }
                }

                // Compress block
                uint8_t* outBlock = result.pixels.data() + dstOffset;

                switch (targetFormat) {
                    case TextureFormat::BC1_UNORM:
                    case TextureFormat::BC1_SRGB:
                        CompressBC1Block(block, outBlock);
                        break;
                    case TextureFormat::BC3_UNORM:
                    case TextureFormat::BC3_SRGB:
                        CompressBC3Block(block, outBlock);
                        break;
                    case TextureFormat::BC7_UNORM:
                    case TextureFormat::BC7_SRGB:
                        CompressBC7Block(block, outBlock, quality);
                        break;
                    default:
                        // Other formats would go here
                        break;
                }

                dstOffset += blockSize;
            }
        }

        srcOffset += w * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    return result;
}

TextureData TextureProcessor::DecompressTexture(const TextureData& input) {
    if (!TextureData::IsCompressed(input.format)) {
        return input;
    }

    TextureData result;
    result.width = input.width;
    result.height = input.height;
    result.mipLevels = input.mipLevels;
    result.format = TextureFormat::RGBA8_UNORM;

    // Calculate total decompressed size
    size_t totalSize = 0;
    uint32_t w = input.width, h = input.height;
    for (uint32_t mip = 0; mip < input.mipLevels; mip++) {
        totalSize += w * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    result.pixels.resize(totalSize);

    // Decompress each mip level
    uint32_t blockSize = TextureData::GetBlockSize(input.format);
    size_t srcOffset = 0;
    size_t dstOffset = 0;
    w = input.width;
    h = input.height;

    for (uint32_t mip = 0; mip < input.mipLevels; mip++) {
        uint32_t blocksX = (w + 3) / 4;
        uint32_t blocksY = (h + 3) / 4;

        for (uint32_t by = 0; by < blocksY; by++) {
            for (uint32_t bx = 0; bx < blocksX; bx++) {
                uint8_t block[64];  // 4x4 RGBA

                switch (input.format) {
                    case TextureFormat::BC1_UNORM:
                    case TextureFormat::BC1_SRGB:
                        DecompressBC1Block(input.pixels.data() + srcOffset, block);
                        break;
                    case TextureFormat::BC3_UNORM:
                    case TextureFormat::BC3_SRGB:
                        DecompressBC3Block(input.pixels.data() + srcOffset, block);
                        break;
                    case TextureFormat::BC7_UNORM:
                    case TextureFormat::BC7_SRGB:
                        DecompressBC7Block(input.pixels.data() + srcOffset, block);
                        break;
                    default:
                        std::fill(block, block + 64, 255);
                        break;
                }

                // Copy block to result
                for (int y = 0; y < 4 && by * 4 + y < h; y++) {
                    for (int x = 0; x < 4 && bx * 4 + x < w; x++) {
                        uint32_t px = bx * 4 + x;
                        uint32_t py = by * 4 + y;
                        uint32_t dstIdx = dstOffset + (py * w + px) * 4;
                        uint32_t srcIdx = (y * 4 + x) * 4;

                        for (int c = 0; c < 4; c++) {
                            result.pixels[dstIdx + c] = block[srcIdx + c];
                        }
                    }
                }

                srcOffset += blockSize;
            }
        }

        dstOffset += w * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    return result;
}

TextureData TextureProcessor::ConvertFormat(const TextureData& input, TextureFormat targetFormat) {
    // Simple format conversion
    TextureData result = input;
    result.format = targetFormat;
    // TODO: Implement proper channel mapping and conversion
    return result;
}

TextureData TextureProcessor::Resize(const TextureData& input, uint32_t newWidth, uint32_t newHeight,
                                       MipmapFilter filter) {
    if (input.width == newWidth && input.height == newHeight) {
        return input;
    }

    TextureData result;
    result.width = newWidth;
    result.height = newHeight;
    result.format = input.format;
    result.mipLevels = 1;

    uint32_t channels = TextureData::GetBytesPerPixel(input.format);
    result.pixels.resize(newWidth * newHeight * channels);

    // Bilinear interpolation resize
    for (uint32_t y = 0; y < newHeight; y++) {
        for (uint32_t x = 0; x < newWidth; x++) {
            float srcX = static_cast<float>(x) * (input.width - 1) / (newWidth - 1);
            float srcY = static_cast<float>(y) * (input.height - 1) / (newHeight - 1);

            uint32_t x0 = static_cast<uint32_t>(srcX);
            uint32_t y0 = static_cast<uint32_t>(srcY);
            uint32_t x1 = std::min(x0 + 1, input.width - 1);
            uint32_t y1 = std::min(y0 + 1, input.height - 1);

            float fx = srcX - x0;
            float fy = srcY - y0;

            for (uint32_t c = 0; c < channels; c++) {
                float v00 = input.pixels[(y0 * input.width + x0) * channels + c];
                float v10 = input.pixels[(y0 * input.width + x1) * channels + c];
                float v01 = input.pixels[(y1 * input.width + x0) * channels + c];
                float v11 = input.pixels[(y1 * input.width + x1) * channels + c];

                float v = v00 * (1 - fx) * (1 - fy) +
                          v10 * fx * (1 - fy) +
                          v01 * (1 - fx) * fy +
                          v11 * fx * fy;

                result.pixels[(y * newWidth + x) * channels + c] = static_cast<uint8_t>(v);
            }
        }
    }

    return result;
}

TextureData TextureProcessor::ConvertHeightToNormal(const TextureData& heightMap, float strength) {
    TextureData result;
    result.width = heightMap.width;
    result.height = heightMap.height;
    result.format = TextureFormat::RGBA8_UNORM;
    result.mipLevels = 1;
    result.pixels.resize(heightMap.width * heightMap.height * 4);

    auto getHeight = [&](int32_t x, int32_t y) -> float {
        x = std::clamp(x, 0, static_cast<int32_t>(heightMap.width - 1));
        y = std::clamp(y, 0, static_cast<int32_t>(heightMap.height - 1));
        return heightMap.pixels[y * heightMap.width + x] / 255.0f;
    };

    for (uint32_t y = 0; y < heightMap.height; y++) {
        for (uint32_t x = 0; x < heightMap.width; x++) {
            float l = getHeight(x - 1, y);
            float r = getHeight(x + 1, y);
            float t = getHeight(x, y - 1);
            float b = getHeight(x, y + 1);

            glm::vec3 normal;
            normal.x = (l - r) * strength;
            normal.y = (b - t) * strength;
            normal.z = 1.0f;
            normal = glm::normalize(normal);

            // Pack to 0-255 range
            uint32_t idx = (y * result.width + x) * 4;
            result.pixels[idx + 0] = static_cast<uint8_t>((normal.x * 0.5f + 0.5f) * 255);
            result.pixels[idx + 1] = static_cast<uint8_t>((normal.y * 0.5f + 0.5f) * 255);
            result.pixels[idx + 2] = static_cast<uint8_t>((normal.z * 0.5f + 0.5f) * 255);
            result.pixels[idx + 3] = 255;
        }
    }

    return result;
}

TextureData TextureProcessor::NormalizeNormalMap(const TextureData& normalMap) {
    TextureData result = normalMap;

    for (uint32_t i = 0; i < result.width * result.height; i++) {
        glm::vec3 n;
        n.x = result.pixels[i * 4 + 0] / 255.0f * 2.0f - 1.0f;
        n.y = result.pixels[i * 4 + 1] / 255.0f * 2.0f - 1.0f;
        n.z = result.pixels[i * 4 + 2] / 255.0f * 2.0f - 1.0f;

        n = glm::normalize(n);

        result.pixels[i * 4 + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255);
        result.pixels[i * 4 + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255);
        result.pixels[i * 4 + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255);
    }

    return result;
}

TextureData TextureProcessor::FlipNormalMapY(const TextureData& normalMap) {
    TextureData result = normalMap;

    for (uint32_t i = 0; i < result.width * result.height; i++) {
        result.pixels[i * 4 + 1] = 255 - result.pixels[i * 4 + 1];
    }

    return result;
}

bool TextureProcessor::HasAlpha(const TextureData& data, float threshold) const {
    for (uint32_t i = 0; i < data.width * data.height; i++) {
        if (data.pixels[i * 4 + 3] < static_cast<uint8_t>(threshold * 255)) {
            return true;
        }
    }
    return false;
}

float TextureProcessor::CalculateRMSE(const TextureData& a, const TextureData& b) const {
    if (a.width != b.width || a.height != b.height) {
        return FLT_MAX;
    }

    double sum = 0.0;
    uint32_t count = a.width * a.height * 4;

    for (uint32_t i = 0; i < count; i++) {
        double diff = static_cast<double>(a.pixels[i]) - static_cast<double>(b.pixels[i]);
        sum += diff * diff;
    }

    return static_cast<float>(std::sqrt(sum / count));
}

glm::vec4 TextureProcessor::CalculateAverageColor(const TextureData& data) const {
    glm::dvec4 sum(0.0);
    uint32_t count = data.width * data.height;

    for (uint32_t i = 0; i < count; i++) {
        sum.r += data.pixels[i * 4 + 0];
        sum.g += data.pixels[i * 4 + 1];
        sum.b += data.pixels[i * 4 + 2];
        sum.a += data.pixels[i * 4 + 3];
    }

    return glm::vec4(sum / static_cast<double>(count) / 255.0);
}

TextureFormat TextureProcessor::RecommendFormat(const TextureData& data, TextureType type) const {
    switch (type) {
        case TextureType::Normal:
            return TextureFormat::BC5_UNORM;
        case TextureType::Roughness:
        case TextureType::Metallic:
        case TextureType::AO:
        case TextureType::Height:
            return TextureFormat::BC4_UNORM;
        case TextureType::HDR:
            return TextureFormat::BC6H_UF16;
        case TextureType::LUT:
            return TextureFormat::RGBA16_FLOAT;
        case TextureType::Emission:
        case TextureType::Default:
        default:
            if (HasAlpha(data, 0.01f)) {
                return TextureFormat::BC7_SRGB;
            } else {
                return TextureFormat::BC1_SRGB;
            }
    }
}

// Simplified BC1 compression (placeholder - use proper library in production)
void TextureProcessor::CompressBC1Block(const uint8_t* rgba, uint8_t* block) {
    // Find min/max colors
    glm::ivec3 minColor(255), maxColor(0);
    for (int i = 0; i < 16; i++) {
        for (int c = 0; c < 3; c++) {
            minColor[c] = std::min(minColor[c], static_cast<int>(rgba[i * 4 + c]));
            maxColor[c] = std::max(maxColor[c], static_cast<int>(rgba[i * 4 + c]));
        }
    }

    // Pack to 565
    uint16_t color0 = ((maxColor.r >> 3) << 11) | ((maxColor.g >> 2) << 5) | (maxColor.b >> 3);
    uint16_t color1 = ((minColor.r >> 3) << 11) | ((minColor.g >> 2) << 5) | (minColor.b >> 3);

    if (color0 < color1) std::swap(color0, color1);

    block[0] = color0 & 0xFF;
    block[1] = (color0 >> 8) & 0xFF;
    block[2] = color1 & 0xFF;
    block[3] = (color1 >> 8) & 0xFF;

    // Generate indices (simplified)
    uint32_t indices = 0;
    for (int i = 0; i < 16; i++) {
        int r = rgba[i * 4 + 0];
        int g = rgba[i * 4 + 1];
        int b = rgba[i * 4 + 2];

        float d0 = std::abs(r - maxColor.r) + std::abs(g - maxColor.g) + std::abs(b - maxColor.b);
        float d1 = std::abs(r - minColor.r) + std::abs(g - minColor.g) + std::abs(b - minColor.b);

        int idx = (d0 <= d1) ? 0 : 1;
        indices |= (idx << (i * 2));
    }

    block[4] = indices & 0xFF;
    block[5] = (indices >> 8) & 0xFF;
    block[6] = (indices >> 16) & 0xFF;
    block[7] = (indices >> 24) & 0xFF;
}

void TextureProcessor::CompressBC3Block(const uint8_t* rgba, uint8_t* block) {
    // BC3 = BC4 alpha + BC1 color
    // Simplified - compress alpha to first 8 bytes
    uint8_t minA = 255, maxA = 0;
    for (int i = 0; i < 16; i++) {
        minA = std::min(minA, rgba[i * 4 + 3]);
        maxA = std::max(maxA, rgba[i * 4 + 3]);
    }

    block[0] = maxA;
    block[1] = minA;

    // Alpha indices (simplified)
    uint64_t alphaIndices = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t a = rgba[i * 4 + 3];
        int idx = (maxA == minA) ? 0 : static_cast<int>((a - minA) * 7 / (maxA - minA));
        alphaIndices |= (static_cast<uint64_t>(idx & 7) << (i * 3));
    }

    for (int i = 0; i < 6; i++) {
        block[2 + i] = (alphaIndices >> (i * 8)) & 0xFF;
    }

    // Color block (BC1)
    CompressBC1Block(rgba, block + 8);
}

void TextureProcessor::CompressBC4Block(const uint8_t* channel, uint8_t* block) {
    uint8_t minV = 255, maxV = 0;
    for (int i = 0; i < 16; i++) {
        minV = std::min(minV, channel[i]);
        maxV = std::max(maxV, channel[i]);
    }

    block[0] = maxV;
    block[1] = minV;

    uint64_t indices = 0;
    for (int i = 0; i < 16; i++) {
        int idx = (maxV == minV) ? 0 : static_cast<int>((channel[i] - minV) * 7 / (maxV - minV));
        indices |= (static_cast<uint64_t>(idx & 7) << (i * 3));
    }

    for (int i = 0; i < 6; i++) {
        block[2 + i] = (indices >> (i * 8)) & 0xFF;
    }
}

void TextureProcessor::CompressBC5Block(const uint8_t* rg, uint8_t* block) {
    // Two BC4 blocks
    uint8_t redChannel[16], greenChannel[16];
    for (int i = 0; i < 16; i++) {
        redChannel[i] = rg[i * 2 + 0];
        greenChannel[i] = rg[i * 2 + 1];
    }

    CompressBC4Block(redChannel, block);
    CompressBC4Block(greenChannel, block + 8);
}

void TextureProcessor::CompressBC7Block(const uint8_t* rgba, uint8_t* block, float quality) {
    // BC7 is complex - this is a simplified placeholder
    // In production, use bc7enc or ispc_texcomp

    // Just store as BC3 for now
    CompressBC3Block(rgba, block);
}

void TextureProcessor::DecompressBC1Block(const uint8_t* block, uint8_t* rgba) {
    uint16_t color0 = block[0] | (block[1] << 8);
    uint16_t color1 = block[2] | (block[3] << 8);
    uint32_t indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);

    glm::ivec3 c0, c1;
    c0.r = ((color0 >> 11) & 0x1F) << 3;
    c0.g = ((color0 >> 5) & 0x3F) << 2;
    c0.b = (color0 & 0x1F) << 3;

    c1.r = ((color1 >> 11) & 0x1F) << 3;
    c1.g = ((color1 >> 5) & 0x3F) << 2;
    c1.b = (color1 & 0x1F) << 3;

    glm::ivec3 palette[4];
    palette[0] = c0;
    palette[1] = c1;
    if (color0 > color1) {
        palette[2] = (c0 * 2 + c1) / 3;
        palette[3] = (c0 + c1 * 2) / 3;
    } else {
        palette[2] = (c0 + c1) / 2;
        palette[3] = glm::ivec3(0);
    }

    for (int i = 0; i < 16; i++) {
        int idx = (indices >> (i * 2)) & 3;
        rgba[i * 4 + 0] = palette[idx].r;
        rgba[i * 4 + 1] = palette[idx].g;
        rgba[i * 4 + 2] = palette[idx].b;
        rgba[i * 4 + 3] = (color0 <= color1 && idx == 3) ? 0 : 255;
    }
}

void TextureProcessor::DecompressBC3Block(const uint8_t* block, uint8_t* rgba) {
    // Decompress color
    DecompressBC1Block(block + 8, rgba);

    // Decompress alpha
    uint8_t alpha0 = block[0];
    uint8_t alpha1 = block[1];

    uint8_t alphaPalette[8];
    alphaPalette[0] = alpha0;
    alphaPalette[1] = alpha1;
    if (alpha0 > alpha1) {
        for (int i = 2; i < 8; i++) {
            alphaPalette[i] = ((8 - i) * alpha0 + (i - 1) * alpha1) / 7;
        }
    } else {
        for (int i = 2; i < 6; i++) {
            alphaPalette[i] = ((6 - i) * alpha0 + (i - 1) * alpha1) / 5;
        }
        alphaPalette[6] = 0;
        alphaPalette[7] = 255;
    }

    uint64_t alphaIndices = 0;
    for (int i = 0; i < 6; i++) {
        alphaIndices |= static_cast<uint64_t>(block[2 + i]) << (i * 8);
    }

    for (int i = 0; i < 16; i++) {
        int idx = (alphaIndices >> (i * 3)) & 7;
        rgba[i * 4 + 3] = alphaPalette[idx];
    }
}

void TextureProcessor::DecompressBC4Block(const uint8_t* block, uint8_t* channel) {
    uint8_t val0 = block[0];
    uint8_t val1 = block[1];

    uint8_t palette[8];
    palette[0] = val0;
    palette[1] = val1;
    if (val0 > val1) {
        for (int i = 2; i < 8; i++) {
            palette[i] = ((8 - i) * val0 + (i - 1) * val1) / 7;
        }
    } else {
        for (int i = 2; i < 6; i++) {
            palette[i] = ((6 - i) * val0 + (i - 1) * val1) / 5;
        }
        palette[6] = 0;
        palette[7] = 255;
    }

    uint64_t indices = 0;
    for (int i = 0; i < 6; i++) {
        indices |= static_cast<uint64_t>(block[2 + i]) << (i * 8);
    }

    for (int i = 0; i < 16; i++) {
        int idx = (indices >> (i * 3)) & 7;
        channel[i] = palette[idx];
    }
}

void TextureProcessor::DecompressBC5Block(const uint8_t* block, uint8_t* rg) {
    uint8_t redChannel[16], greenChannel[16];
    DecompressBC4Block(block, redChannel);
    DecompressBC4Block(block + 8, greenChannel);

    for (int i = 0; i < 16; i++) {
        rg[i * 2 + 0] = redChannel[i];
        rg[i * 2 + 1] = greenChannel[i];
    }
}

void TextureProcessor::DecompressBC7Block(const uint8_t* block, uint8_t* rgba) {
    // BC7 decompression is complex - placeholder
    DecompressBC3Block(block, rgba);
}

float TextureProcessor::LinearToSRGB(float linear) const {
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    }
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

float TextureProcessor::SRGBToLinear(float srgb) const {
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    }
    return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

void TextureProcessor::ConvertLinearToSRGB(std::vector<uint8_t>& pixels, uint32_t channels) {
    for (size_t i = 0; i < pixels.size(); i += channels) {
        for (uint32_t c = 0; c < std::min(3u, channels); c++) {
            float linear = pixels[i + c] / 255.0f;
            pixels[i + c] = static_cast<uint8_t>(LinearToSRGB(linear) * 255.0f);
        }
    }
}

void TextureProcessor::ConvertSRGBToLinear(std::vector<uint8_t>& pixels, uint32_t channels) {
    for (size_t i = 0; i < pixels.size(); i += channels) {
        for (uint32_t c = 0; c < std::min(3u, channels); c++) {
            float srgb = pixels[i + c] / 255.0f;
            pixels[i + c] = static_cast<uint8_t>(SRGBToLinear(srgb) * 255.0f);
        }
    }
}

glm::vec4 TextureAtlas::GetUVRect(uint32_t textureIndex) const {
    for (const auto& rect : rects) {
        if (rect.textureIndex == textureIndex) {
            float u0 = static_cast<float>(rect.x) / texture.width;
            float v0 = static_cast<float>(rect.y) / texture.height;
            float u1 = static_cast<float>(rect.x + rect.width) / texture.width;
            float v1 = static_cast<float>(rect.y + rect.height) / texture.height;
            return glm::vec4(u0, v0, u1, v1);
        }
    }
    return glm::vec4(0, 0, 1, 1);
}

// ============================================================================
// CubemapUtils
// ============================================================================

namespace CubemapUtils {

void DirectionToFaceUV(const glm::vec3& dir, CubeFace& face, glm::vec2& uv) {
    glm::vec3 absDir = glm::abs(dir);

    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        face = dir.x > 0 ? CubeFace::PositiveX : CubeFace::NegativeX;
        float invMa = 1.0f / absDir.x;
        uv.x = dir.x > 0 ? -dir.z * invMa : dir.z * invMa;
        uv.y = -dir.y * invMa;
    } else if (absDir.y >= absDir.z) {
        face = dir.y > 0 ? CubeFace::PositiveY : CubeFace::NegativeY;
        float invMa = 1.0f / absDir.y;
        uv.x = dir.x * invMa;
        uv.y = dir.y > 0 ? dir.z * invMa : -dir.z * invMa;
    } else {
        face = dir.z > 0 ? CubeFace::PositiveZ : CubeFace::NegativeZ;
        float invMa = 1.0f / absDir.z;
        uv.x = dir.z > 0 ? dir.x * invMa : -dir.x * invMa;
        uv.y = -dir.y * invMa;
    }

    uv = uv * 0.5f + 0.5f;
}

glm::vec3 FaceUVToDirection(CubeFace face, const glm::vec2& uv) {
    glm::vec2 st = uv * 2.0f - 1.0f;

    switch (face) {
        case CubeFace::PositiveX: return glm::normalize(glm::vec3(1, -st.y, -st.x));
        case CubeFace::NegativeX: return glm::normalize(glm::vec3(-1, -st.y, st.x));
        case CubeFace::PositiveY: return glm::normalize(glm::vec3(st.x, 1, st.y));
        case CubeFace::NegativeY: return glm::normalize(glm::vec3(st.x, -1, -st.y));
        case CubeFace::PositiveZ: return glm::normalize(glm::vec3(st.x, -st.y, 1));
        case CubeFace::NegativeZ: return glm::normalize(glm::vec3(-st.x, -st.y, -1));
        default: return glm::vec3(0, 0, 1);
    }
}

} // namespace CubemapUtils

// ============================================================================
// BRDFUtils
// ============================================================================

namespace BRDFUtils {

TextureData GenerateBRDFLUT(uint32_t size) {
    TextureData result;
    result.width = size;
    result.height = size;
    result.format = TextureFormat::RG16_FLOAT;
    result.mipLevels = 1;
    result.pixels.resize(size * size * 4);  // RG16 = 4 bytes per pixel

    // TODO: Implement actual BRDF integration
    // This requires importance sampling of the GGX BRDF

    return result;
}

} // namespace BRDFUtils

} // namespace Cortex::Utils
