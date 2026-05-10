#pragma once

#include <array>
#include <cstdint>

#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct UploadCommandPoolState {
    static constexpr uint32_t kPoolSize = 4;

    std::array<ComPtr<ID3D12CommandAllocator>, kPoolSize> commandAllocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, kPoolSize> commandLists;
    uint32_t allocatorIndex = 0;
    std::array<uint64_t, kPoolSize> fences{};
    uint64_t pendingFence = 0;

    void ResetFenceBookkeeping() {
        fences.fill(0);
        pendingFence = 0;
    }

    void ResetResources() {
        for (auto& allocator : commandAllocators) {
            allocator.Reset();
        }
        for (auto& list : commandLists) {
            list.Reset();
        }
        allocatorIndex = 0;
        ResetFenceBookkeeping();
    }
};

} // namespace Cortex::Graphics
