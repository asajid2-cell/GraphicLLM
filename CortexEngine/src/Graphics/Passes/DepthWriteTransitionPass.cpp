#include "Graphics/Passes/DepthWriteTransitionPass.h"

namespace Cortex::Graphics::DepthWriteTransitionPass {

bool TransitionToDepthWrite(const TransitionContext& context) {
    if (!context.commandList || !context.depthBuffer || !context.depthState) {
        return false;
    }

    if (*context.depthState == D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        return true;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = context.depthBuffer;
    barrier.Transition.StateBefore = *context.depthState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    context.commandList->ResourceBarrier(1, &barrier);
    *context.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    return true;
}

} // namespace Cortex::Graphics::DepthWriteTransitionPass
