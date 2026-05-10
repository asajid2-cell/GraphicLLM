#pragma once

#include <cstdint>
#include <dxgiformat.h>

namespace Cortex::Graphics {

[[nodiscard]] uint32_t BytesPerPixelForContract(DXGI_FORMAT format);
[[nodiscard]] const char* ExpectedReadStateClass(const char* name);
[[nodiscard]] const char* ExpectedWriteStateClass(const char* name);

} // namespace Cortex::Graphics
