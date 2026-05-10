#pragma once

#include <array>
#include <memory>
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct MaterialFallbackTextureState {
    std::shared_ptr<DX12Texture> albedo;
    std::shared_ptr<DX12Texture> normal;
    std::shared_ptr<DX12Texture> metallic;
    std::shared_ptr<DX12Texture> roughness;
    std::array<DescriptorHandle, 4> descriptorTable = {};

    void ResetResources() {
        albedo.reset();
        normal.reset();
        metallic.reset();
        roughness.reset();
        descriptorTable = {};
    }
};

} // namespace Cortex::Graphics
