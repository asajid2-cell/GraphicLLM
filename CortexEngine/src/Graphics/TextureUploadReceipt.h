#pragma once

#include "Graphics/AssetRegistry.h"
#include "Graphics/RHI/BindlessResources.h"
#include "Graphics/RHI/D3D12Includes.h"

#include <cstdint>
#include <string>

namespace Cortex::Graphics {

enum class TextureSourceEncoding : uint32_t {
    Unknown = 0,
    RGBA8,
    DDSCompressed,
    Placeholder,
};

enum class TextureResidencyClass : uint32_t {
    Generic = 0,
    Environment,
    Generated,
    Placeholder,
};

struct TextureUploadReceipt {
    std::string key;
    std::string sourcePath;
    std::string debugName;
    std::string budgetProfile;

    AssetRegistry::TextureKind kind = AssetRegistry::TextureKind::Generic;
    TextureSourceEncoding sourceEncoding = TextureSourceEncoding::Unknown;
    TextureResidencyClass residencyClass = TextureResidencyClass::Generic;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    uint32_t sourceMipLevels = 0;
    uint32_t firstResidentMip = 0;
    uint32_t residentWidth = 0;
    uint32_t residentHeight = 0;
    uint32_t residentMipLevels = 0;

    uint64_t fullGpuBytes = 0;
    uint64_t residentGpuBytes = 0;
    uint64_t residentTextureBytesBefore = 0;
    uint64_t textureBudgetBytes = 0;

    uint32_t bindlessIndex = kInvalidBindlessIndex;
    bool requestedSRGB = false;
    bool usedCompressedSibling = false;
    bool budgetTrimmed = false;
};

} // namespace Cortex::Graphics
