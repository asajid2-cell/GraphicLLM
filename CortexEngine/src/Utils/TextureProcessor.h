#pragma once

// TextureProcessor.h
// Texture processing utilities: compression, mipmap generation, format conversion.
// Supports BC1-BC7 compression, normal map processing, and atlas packing.

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <array>

namespace Cortex::Utils {

// Texture format enumeration
enum class TextureFormat : uint8_t {
    Unknown = 0,

    // Uncompressed formats
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA8_SRGB,
    R16_FLOAT,
    RG16_FLOAT,
    RGBA16_FLOAT,
    R32_FLOAT,
    RG32_FLOAT,
    RGBA32_FLOAT,

    // Block compressed formats
    BC1_UNORM,          // RGB (1-bit alpha) - 4:1 compression
    BC1_SRGB,
    BC3_UNORM,          // RGBA with full alpha - 4:1 compression
    BC3_SRGB,
    BC4_UNORM,          // Single channel (grayscale) - 2:1 compression
    BC4_SNORM,
    BC5_UNORM,          // Two channels (normal maps) - 2:1 compression
    BC5_SNORM,
    BC6H_UF16,          // HDR RGB - 6:1 compression
    BC6H_SF16,
    BC7_UNORM,          // High quality RGBA - 3:1 compression
    BC7_SRGB,
};

// Texture type hints for processing
enum class TextureType : uint8_t {
    Default,            // Standard color texture
    Normal,             // Normal map (special compression/conversion)
    Roughness,          // Single-channel roughness
    Metallic,           // Single-channel metallic
    AO,                 // Ambient occlusion
    Height,             // Height/displacement map
    Emission,           // Emissive (sRGB)
    HDR,                // High dynamic range
    LUT                 // Lookup table (no compression)
};

// Mipmap filtering modes
enum class MipmapFilter : uint8_t {
    Box,                // Simple box filter (average)
    Triangle,           // Bilinear
    Lanczos,            // High quality
    Kaiser              // Kaiser-windowed sinc
};

// Raw texture data
struct TextureData {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;             // For 3D textures
    uint32_t arraySize = 1;         // For texture arrays
    uint32_t mipLevels = 1;
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    bool isCubemap = false;

    // Get byte offset to specific mip level
    size_t GetMipOffset(uint32_t mipLevel) const;

    // Get dimensions at mip level
    uint32_t GetMipWidth(uint32_t mipLevel) const;
    uint32_t GetMipHeight(uint32_t mipLevel) const;

    // Get bytes per pixel for format
    static uint32_t GetBytesPerPixel(TextureFormat format);
    static uint32_t GetBlockSize(TextureFormat format);
    static bool IsCompressed(TextureFormat format);
};

// Texture processing settings
struct TextureProcessingSettings {
    // Target format
    TextureFormat targetFormat = TextureFormat::BC7_SRGB;
    TextureType textureType = TextureType::Default;

    // Mipmap settings
    bool generateMipmaps = true;
    MipmapFilter mipmapFilter = MipmapFilter::Lanczos;
    uint32_t maxMipLevels = 0;      // 0 = full chain

    // Quality settings (0-1)
    float compressionQuality = 0.8f;

    // Normal map specific
    bool normalMapFlipY = false;    // OpenGL vs DirectX convention
    bool normalizeNormals = true;

    // Alpha handling
    bool premultiplyAlpha = false;
    float alphaThreshold = 0.5f;    // For BC1 alpha cutout

    // Size adjustments
    bool powerOfTwo = true;
    uint32_t maxWidth = 4096;
    uint32_t maxHeight = 4096;

    // sRGB handling
    bool inputSRGB = true;
    bool outputSRGB = true;
};

// Atlas packing result
struct AtlasRect {
    uint32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    uint32_t textureIndex = 0;      // Original texture index
};

struct TextureAtlas {
    TextureData texture;
    std::vector<AtlasRect> rects;
    float packingEfficiency = 0.0f; // 0-1

    // Get UV coordinates for a rect
    glm::vec4 GetUVRect(uint32_t textureIndex) const;
};

// Progress callback
using TextureProgressCallback = std::function<void(float progress, const std::string& status)>;

// Texture processor class
class TextureProcessor {
public:
    TextureProcessor();
    ~TextureProcessor() = default;

    // Set progress callback
    void SetProgressCallback(TextureProgressCallback callback) { m_progressCallback = callback; }

    // Load texture from file
    bool LoadTexture(const std::string& path, TextureData& outData);

    // Save texture to file
    bool SaveTexture(const std::string& path, const TextureData& data);

    // Process texture with settings
    TextureData ProcessTexture(const TextureData& input, const TextureProcessingSettings& settings);

    // Generate mipmaps
    TextureData GenerateMipmaps(const TextureData& input, MipmapFilter filter = MipmapFilter::Lanczos);

    // Compress texture
    TextureData CompressTexture(const TextureData& input, TextureFormat targetFormat, float quality = 0.8f);

    // Decompress texture
    TextureData DecompressTexture(const TextureData& input);

    // Convert format
    TextureData ConvertFormat(const TextureData& input, TextureFormat targetFormat);

    // Resize texture
    TextureData Resize(const TextureData& input, uint32_t newWidth, uint32_t newHeight,
                        MipmapFilter filter = MipmapFilter::Lanczos);

    // Normal map operations
    TextureData ConvertHeightToNormal(const TextureData& heightMap, float strength = 1.0f);
    TextureData NormalizeNormalMap(const TextureData& normalMap);
    TextureData FlipNormalMapY(const TextureData& normalMap);

    // Combine textures
    TextureData CombineChannels(const TextureData& r, const TextureData& g,
                                 const TextureData& b, const TextureData& a);

    // Extract channel
    TextureData ExtractChannel(const TextureData& input, uint32_t channel);

    // Atlas packing
    TextureAtlas CreateAtlas(const std::vector<TextureData>& textures,
                              uint32_t maxSize = 4096, uint32_t padding = 2);

    // Utility operations
    TextureData FlipVertical(const TextureData& input);
    TextureData FlipHorizontal(const TextureData& input);
    TextureData Rotate90(const TextureData& input, bool clockwise = true);

    // Analysis
    bool HasAlpha(const TextureData& data, float threshold = 0.01f) const;
    float CalculateRMSE(const TextureData& a, const TextureData& b) const;
    glm::vec4 CalculateAverageColor(const TextureData& data) const;

    // Recommend format based on content
    TextureFormat RecommendFormat(const TextureData& data, TextureType type) const;

private:
    // Internal mipmap generation
    std::vector<uint8_t> GenerateMipLevel(const uint8_t* srcPixels,
                                           uint32_t srcWidth, uint32_t srcHeight,
                                           uint32_t channels, MipmapFilter filter);

    // BC compression helpers
    void CompressBC1Block(const uint8_t* rgba, uint8_t* block);
    void CompressBC3Block(const uint8_t* rgba, uint8_t* block);
    void CompressBC4Block(const uint8_t* channel, uint8_t* block);
    void CompressBC5Block(const uint8_t* rg, uint8_t* block);
    void CompressBC7Block(const uint8_t* rgba, uint8_t* block, float quality);

    // BC decompression helpers
    void DecompressBC1Block(const uint8_t* block, uint8_t* rgba);
    void DecompressBC3Block(const uint8_t* block, uint8_t* rgba);
    void DecompressBC4Block(const uint8_t* block, uint8_t* channel);
    void DecompressBC5Block(const uint8_t* block, uint8_t* rg);
    void DecompressBC7Block(const uint8_t* block, uint8_t* rgba);

    // sRGB conversion
    float LinearToSRGB(float linear) const;
    float SRGBToLinear(float srgb) const;
    void ConvertLinearToSRGB(std::vector<uint8_t>& pixels, uint32_t channels);
    void ConvertSRGBToLinear(std::vector<uint8_t>& pixels, uint32_t channels);

    // Atlas packing algorithm (Guillotine)
    struct PackNode {
        uint32_t x = 0, y = 0;
        uint32_t width = 0, height = 0;
        bool used = false;
    };
    bool PackRectangle(std::vector<PackNode>& nodes, uint32_t width, uint32_t height,
                        uint32_t& outX, uint32_t& outY);

    TextureProgressCallback m_progressCallback;
};

// Cubemap utilities
namespace CubemapUtils {

// Face indices
enum class CubeFace : uint8_t {
    PositiveX = 0,  // Right
    NegativeX = 1,  // Left
    PositiveY = 2,  // Top
    NegativeY = 3,  // Bottom
    PositiveZ = 4,  // Front
    NegativeZ = 5   // Back
};

// Convert equirectangular to cubemap
TextureData EquirectToCubemap(const TextureData& equirect, uint32_t faceSize);

// Convert cubemap to equirectangular
TextureData CubemapToEquirect(const TextureData& cubemap, uint32_t width, uint32_t height);

// Generate irradiance map from cubemap
TextureData GenerateIrradianceMap(const TextureData& cubemap, uint32_t faceSize);

// Generate prefiltered environment map
TextureData GeneratePrefilteredEnvMap(const TextureData& cubemap, uint32_t faceSize, uint32_t mipLevels);

// Sample direction to cubemap face and UV
void DirectionToFaceUV(const glm::vec3& dir, CubeFace& face, glm::vec2& uv);

// Cubemap face UV to direction
glm::vec3 FaceUVToDirection(CubeFace face, const glm::vec2& uv);

} // namespace CubemapUtils

// BRDF LUT generation
namespace BRDFUtils {

// Generate BRDF lookup texture for split-sum approximation
TextureData GenerateBRDFLUT(uint32_t size = 512);

} // namespace BRDFUtils

} // namespace Cortex::Utils
