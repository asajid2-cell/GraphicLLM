#pragma once

#include <array>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct LocalReflectionRadianceDescriptorTables {
    std::array<std::array<DescriptorHandle, 7>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 1>, kFrameCount> uavTables{};
    bool valid = false;

    void Reset() {
        valid = false;
        for (auto& table : srvTables) {
            for (auto& handle : table) {
                handle = {};
            }
        }
        for (auto& table : uavTables) {
            for (auto& handle : table) {
                handle = {};
            }
        }
    }
};

struct LocalReflectionRadianceState {
    LocalReflectionRadianceDescriptorTables descriptors;
};

} // namespace Cortex::Graphics
