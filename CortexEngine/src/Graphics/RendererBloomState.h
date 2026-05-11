#pragma once

#include <array>
#include <cstdint>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

template <uint32_t BloomLevels>
struct BloomPyramidResources {
    ComPtr<ID3D12Resource> texA[BloomLevels];
    ComPtr<ID3D12Resource> texB[BloomLevels];
    DescriptorHandle rtv[BloomLevels][2];
    DescriptorHandle srv[BloomLevels][2];
    DescriptorHandle graphRtv[BloomLevels][2];
    DescriptorHandle graphSrv[BloomLevels][2];
    D3D12_RESOURCE_STATES resourceState[BloomLevels][2] = {};
    uint32_t activeLevels = BloomLevels;
    DescriptorHandle combinedSrv;
    ID3D12Resource* postProcessOverride = nullptr;

    void ResetResources() {
        for (uint32_t level = 0; level < BloomLevels; ++level) {
            texA[level].Reset();
            texB[level].Reset();
            resourceState[level][0] = D3D12_RESOURCE_STATE_COMMON;
            resourceState[level][1] = D3D12_RESOURCE_STATE_COMMON;
        }
        activeLevels = BloomLevels;
        combinedSrv = {};
        postProcessOverride = nullptr;
    }
};

template <uint32_t BloomDescriptorSlots>
struct BloomDescriptorTables {
    std::array<std::array<DescriptorHandle, BloomDescriptorSlots>, kFrameCount> srvTables{};
    bool srvTableValid = false;

    void Reset() {
        srvTableValid = false;
    }
};

struct BloomPassControls {
    float intensity = 0.25f;
    float threshold = 1.0f;
    float softKnee = 0.5f;
    float maxContribution = 4.0f;
};

template <uint32_t BloomLevels, uint32_t BloomDescriptorSlots>
struct BloomPassState {
    BloomPassControls controls;
    BloomPyramidResources<BloomLevels> resources;
    BloomDescriptorTables<BloomDescriptorSlots> descriptors;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
