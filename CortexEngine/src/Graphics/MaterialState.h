#pragma once

#include <array>
#include <memory>
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

class DX12Texture;

struct MaterialGPUState {
    std::array<DescriptorHandle, 4> descriptors{};
    std::array<std::weak_ptr<DX12Texture>, 4> sourceTextures{};
    bool descriptorsReady = false;
};

} // namespace Cortex::Graphics
