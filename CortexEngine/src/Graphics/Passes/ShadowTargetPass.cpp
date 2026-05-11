#include "Graphics/Passes/ShadowTargetPass.h"

namespace Cortex::Graphics::ShadowTargetPass {

namespace {

[[nodiscard]] bool TransitionShadowMap(const TransitionContext& context,
                                       D3D12_RESOURCE_STATES after) {
    if (!context.commandList || !context.shadowMap || !context.resourceState) {
        return false;
    }

    if (context.skipTransitions) {
        return true;
    }

    if (*context.resourceState != after) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = context.shadowMap;
        barrier.Transition.StateBefore = *context.resourceState;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &barrier);
        *context.resourceState = after;
    }
    return true;
}

} // namespace

bool TransitionToDepthWrite(const TransitionContext& context) {
    if (!TransitionShadowMap(context, D3D12_RESOURCE_STATE_DEPTH_WRITE)) {
        return false;
    }
    if (!context.skipTransitions && context.initializedForEditor) {
        *context.initializedForEditor = true;
    }
    return true;
}

bool BindAndClearSlice(const SliceContext& context) {
    if (!context.commandList || !context.dsv.IsValid()) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = context.dsv.cpu;
    context.commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    context.commandList->ClearDepthStencilView(
        dsv,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr);
    context.commandList->RSSetViewports(1, &context.viewport);
    context.commandList->RSSetScissorRects(1, &context.scissor);
    return true;
}

bool TransitionToShaderResource(const TransitionContext& context) {
    return TransitionShadowMap(context, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

} // namespace Cortex::Graphics::ShadowTargetPass
