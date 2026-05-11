#include "Graphics/Passes/ForwardTargetBindingPass.h"

namespace Cortex::Graphics::ForwardTargetBindingPass {

bool BindHdrAndDepthReadOnly(const BindContext& context) {
    if (!context.commandList ||
        !context.hdrColor ||
        !context.hdrState ||
        !context.depthBuffer ||
        !context.depthState ||
        !context.hdrRtv.IsValid() ||
        !context.depthDsv.IsValid()) {
        return false;
    }

    if (*context.hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = context.hdrColor;
        barrier.Transition.StateBefore = *context.hdrState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &barrier);
        *context.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    const bool hasReadOnlyDsv = context.readOnlyDepthDsv.IsValid();
    const D3D12_RESOURCE_STATES desiredDepthState =
        hasReadOnlyDsv ? context.readOnlyDepthState : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (*context.depthState != desiredDepthState) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = context.depthBuffer;
        depthBarrier.Transition.StateBefore = *context.depthState;
        depthBarrier.Transition.StateAfter = desiredDepthState;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &depthBarrier);
        *context.depthState = desiredDepthState;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = context.hdrRtv.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        hasReadOnlyDsv ? context.readOnlyDepthDsv.cpu : context.depthDsv.cpu;
    context.commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    return true;
}

} // namespace Cortex::Graphics::ForwardTargetBindingPass
