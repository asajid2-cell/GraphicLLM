#pragma once

#include <cstdint>

namespace Cortex::Graphics {

// Invalid bindless index sentinel - shaders check for this to use fallback.
inline constexpr uint32_t kInvalidBindlessIndex = 0xFFFFFFFFu;

} // namespace Cortex::Graphics

