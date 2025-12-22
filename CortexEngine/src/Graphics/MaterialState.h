#pragma once

#include <array>
#include <memory>
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

class DX12Texture;

struct MaterialGPUState {
    // Texture slots:
    // 0 albedo, 1 normal, 2 metallic, 3 roughness, 4 occlusion, 5 emissive.
    std::array<DescriptorHandle, 6> descriptors{};
    std::array<std::weak_ptr<DX12Texture>, 6> sourceTextures{};
    bool descriptorsReady = false;
};

} // namespace Cortex::Graphics
