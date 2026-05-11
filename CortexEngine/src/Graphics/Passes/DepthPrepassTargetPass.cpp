#include "Graphics/Passes/DepthPrepassTargetPass.h"

namespace Cortex::Graphics::DepthPrepassTargetPass {

bool BindAndClear(const BindContext& context) {
    if (!context.commandList ||
        !context.depthBuffer ||
        !context.depthState ||
        !context.depthDsv.IsValid()) {
        return false;
    }

    if (!context.skipTransitions && *context.depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = context.depthBuffer;
        depthBarrier.Transition.StateBefore = *context.depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &depthBarrier);
        *context.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = context.depthDsv.cpu;
    context.commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    if (context.clearDepth) {
        context.commandList->ClearDepthStencilView(
            dsv,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);
    }

    const D3D12_RESOURCE_DESC depthDesc = context.depthBuffer->GetDesc();
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(depthDesc.Width);
    viewport.Height = static_cast<float>(depthDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(depthDesc.Width);
    scissorRect.bottom = static_cast<LONG>(depthDesc.Height);

    context.commandList->RSSetViewports(1, &viewport);
    context.commandList->RSSetScissorRects(1, &scissorRect);
    return true;
}

} // namespace Cortex::Graphics::DepthPrepassTargetPass
