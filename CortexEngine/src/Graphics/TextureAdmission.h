#pragma once

#include "Graphics/BudgetPlanner.h"
#include "Graphics/RHI/D3D12Includes.h"

#include <cstdint>

namespace Cortex::Graphics {

struct TextureMipAdmission {
    uint32_t firstMip = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t fullBytes = 0;
    uint64_t admittedBytes = 0;
};

[[nodiscard]] uint64_t EstimateTextureBytes(uint32_t width,
                                            uint32_t height,
                                            uint32_t mipLevels,
                                            DXGI_FORMAT format);

[[nodiscard]] TextureMipAdmission ChooseTextureMipAdmission(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    DXGI_FORMAT format,
    const RendererBudgetPlan& budget,
    uint64_t residentTextureBytes);

} // namespace Cortex::Graphics
