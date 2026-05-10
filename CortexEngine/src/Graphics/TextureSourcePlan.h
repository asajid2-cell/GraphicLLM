#pragma once

#include "Graphics/BudgetPlanner.h"
#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/TextureAdmission.h"
#include "Graphics/TextureUploadReceipt.h"
#include "Utils/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Graphics {

struct TextureSourcePlan {
    std::string requestedPath;
    std::string sourcePath;
    TextureSourceEncoding encoding = TextureSourceEncoding::Unknown;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    TextureMipAdmission admission{};
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;
    uint32_t sourceMipLevels = 0;
    bool usedCompressedSibling = false;
    bool preferCopyQueue = false;
    std::vector<std::vector<uint8_t>> mips;
};

[[nodiscard]] Result<TextureSourcePlan> BuildTextureSourcePlan(
    const std::string& path,
    bool useSRGB,
    bool enableCompressedDDS,
    bool budgetMips,
    const RendererBudgetPlan& budget,
    uint64_t residentTextureBytes);

} // namespace Cortex::Graphics
