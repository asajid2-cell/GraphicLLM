#pragma once

#include <array>
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct MaterialGPUState {
    std::array<DescriptorHandle, 4> descriptors{};
    bool descriptorsReady = false;
};

} // namespace Cortex::Graphics
