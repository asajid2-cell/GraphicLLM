#pragma once

#include <array>
#include <cstdint>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

template <uint32_t BloomLevels, uint32_t BloomDescriptorSlots>
struct BloomPassState {
    ComPtr<ID3D12Resource> texA[BloomLevels];
    ComPtr<ID3D12Resource> texB[BloomLevels];
    DescriptorHandle rtv[BloomLevels][2];
    DescriptorHandle srv[BloomLevels][2];
    DescriptorHandle graphRtv[BloomLevels][2];
    DescriptorHandle graphSrv[BloomLevels][2];
    D3D12_RESOURCE_STATES resourceState[BloomLevels][2] = {};
    uint32_t activeLevels = BloomLevels;
    DescriptorHandle combinedSrv;
    std::array<std::array<DescriptorHandle, BloomDescriptorSlots>, kFrameCount> srvTables{};
    bool srvTableValid = false;
    ID3D12Resource* postProcessOverride = nullptr;
    float intensity = 0.25f;
    float threshold = 1.0f;
    float softKnee = 0.5f;
    float maxContribution = 4.0f;

    void ResetResources() {
        for (uint32_t level = 0; level < BloomLevels; ++level) {
            texA[level].Reset();
            texB[level].Reset();
            resourceState[level][0] = D3D12_RESOURCE_STATE_COMMON;
            resourceState[level][1] = D3D12_RESOURCE_STATE_COMMON;
        }
        activeLevels = BloomLevels;
        srvTableValid = false;
        postProcessOverride = nullptr;
    }
};

} // namespace Cortex::Graphics
