#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct VoxelRenderState {
    bool backendEnabled = false;

    std::vector<uint32_t> gridCPU;
    uint32_t gridDim = 384;
    bool gridDirty = true;
    ComPtr<ID3D12Resource> gridBuffer;
    DescriptorHandle gridSRV{};

    std::unordered_map<std::string, uint8_t> materialIds;
    uint8_t nextMaterialId = 1;

    void ResetGrid() {
        gridCPU.clear();
        gridDirty = true;
        gridBuffer.Reset();
        gridSRV = {};
        materialIds.clear();
        nextMaterialId = 1;
    }

    void ResetMaterialPalette() {
        materialIds.clear();
        nextMaterialId = 1;
    }
};

} // namespace Cortex::Graphics
