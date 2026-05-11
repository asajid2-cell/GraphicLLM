#include "Graphics/Passes/EndFrameShaderResourcePass.h"

#include <vector>

namespace Cortex::Graphics::EndFrameShaderResourcePass {

bool TransitionToPixelShaderResources(const TransitionContext& context) {
    if (!context.commandList) {
        return false;
    }
    if (!context.targets || context.targetCount == 0) {
        return true;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(context.targetCount);
    for (size_t i = 0; i < context.targetCount; ++i) {
        const TransitionTarget& target = context.targets[i];
        if (!target.resource) {
            continue;
        }
        if (!target.state) {
            return false;
        }
        if (*target.state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            continue;
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target.resource;
        barrier.Transition.StateBefore = *target.state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers.push_back(barrier);
        *target.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (!barriers.empty()) {
        context.commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
    return true;
}

} // namespace Cortex::Graphics::EndFrameShaderResourcePass
