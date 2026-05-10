#pragma once

#include "Graphics/RHI/DX12Texture.h"
#include "Graphics/TextureUploadReceipt.h"

#include <string>

namespace Cortex::Graphics {

// Move-only handoff from texture upload/creation to publication. The current
// path fills this synchronously; a future streaming path can enqueue the same
// payload and publish it once the GPU upload fence completes.
struct TextureUploadTicket {
    DX12Texture texture;
    TextureUploadReceipt receipt;
    std::string publishContext;
    bool registerAsset = false;
};

} // namespace Cortex::Graphics
