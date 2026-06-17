#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

class DX12Texture;

struct MaterialGPUState {
    // Texture slots:
    // 0 albedo, 1 normal, 2 metallic, 3 roughness, 4 occlusion, 5 emissive,
    // 6 transmission, 7 clearcoat, 8 clearcoatRoughness, 9 specular, 10 specularColor.
    static constexpr uint32_t kSlotCount = 11;
    // The common graphics root signature exposes t0-t13 at root slot 3.
    // Material semantics currently use 11 slots, but the shader-visible table
    // must cover the full root range so validation/GPU execution never walks
    // past the allocated range.
    static constexpr uint32_t kDescriptorCount = 14;
    std::array<DescriptorHandle, kDescriptorCount> descriptors{};
    std::array<std::weak_ptr<DX12Texture>, kSlotCount> sourceTextures{};
    std::array<uint64_t, kSlotCount> boundResourceSignatures{};
    bool descriptorsAllocated = false;
    bool descriptorsReady = false;
};

} // namespace Cortex::Graphics
