#include "Graphics/Passes/MinimalFramePass.h"

namespace Cortex::Graphics::MinimalFramePass {

bool ClearBackBuffer(const ClearContext& context) {
    if (!context.commandList || !context.backBuffer || context.rtv.ptr == 0) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = context.backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    context.commandList->ResourceBarrier(1, &barrier);

    if (context.backBufferUsedAsRTThisFrame) {
        *context.backBufferUsedAsRTThisFrame = true;
    }

    context.commandList->OMSetRenderTargets(1, &context.rtv, FALSE, nullptr);
    context.commandList->ClearRenderTargetView(context.rtv, context.clearColor, 0, nullptr);
    return true;
}

} // namespace Cortex::Graphics::MinimalFramePass
