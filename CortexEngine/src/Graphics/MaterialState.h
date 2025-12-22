#pragma once

#include <array>
#include <memory>
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

class DX12Texture;

struct MaterialGPUState {
    // Texture slots:
    // 0 albedo, 1 normal, 2 metallic, 3 roughness, 4 occlusion, 5 emissive,
    // 6 transmission, 7 clearcoat, 8 clearcoatRoughness, 9 specular, 10 specularColor.
    static constexpr uint32_t kSlotCount = 11;
    std::array<DescriptorHandle, kSlotCount> descriptors{};
    std::array<std::weak_ptr<DX12Texture>, kSlotCount> sourceTextures{};
    bool descriptorsReady = false;
};

} // namespace Cortex::Graphics
