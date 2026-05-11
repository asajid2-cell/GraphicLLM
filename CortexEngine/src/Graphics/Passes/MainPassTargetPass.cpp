#include "Graphics/Passes/MainPassTargetPass.h"

namespace Cortex::Graphics::MainPassTargetPass {

namespace {

[[nodiscard]] bool Transition(ID3D12GraphicsCommandList* commandList,
                              ID3D12Resource* resource,
                              D3D12_RESOURCE_STATES* state,
                              D3D12_RESOURCE_STATES after) {
    if (!resource) {
        return true;
    }
    if (!commandList || !state) {
        return false;
    }
    if (*state == after) {
        return true;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = *state;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    *state = after;
    return true;
}

[[nodiscard]] bool BindGraphicsState(const PrepareContext& context) {
    if (!context.commandList ||
        !context.descriptorManager ||
        !context.rootSignature ||
        !context.rootSignature->GetRootSignature() ||
        !context.geometryPipeline ||
        !context.geometryPipeline->GetPipelineState()) {
        return false;
    }

    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    context.commandList->SetPipelineState(context.geometryPipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

} // namespace

bool Prepare(const PrepareContext& context) {
    if (!context.commandList || !context.depthBuffer || !context.depthState || !context.depthDsv.IsValid()) {
        return false;
    }

    if (!Transition(context.commandList, context.depthBuffer, context.depthState, D3D12_RESOURCE_STATE_DEPTH_WRITE) ||
        !Transition(context.commandList, context.rtShadowMask, context.rtShadowMaskState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) ||
        !Transition(context.commandList, context.rtGIColor, context.rtGIColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {};
    UINT numRtvs = 0;
    uint32_t targetWidth = context.backBufferWidth;
    uint32_t targetHeight = context.backBufferHeight;

    if (context.hdrColor) {
        if (!context.hdrState || !context.hdrRtv.IsValid()) {
            return false;
        }
        if (!Transition(context.commandList, context.hdrColor, context.hdrState, D3D12_RESOURCE_STATE_RENDER_TARGET)) {
            return false;
        }
        rtvs[numRtvs++] = context.hdrRtv.cpu;

        const D3D12_RESOURCE_DESC hdrDesc = context.hdrColor->GetDesc();
        targetWidth = static_cast<uint32_t>(hdrDesc.Width);
        targetHeight = hdrDesc.Height;

        if (context.normalRoughness) {
            if (!context.normalRoughnessState || !context.normalRoughnessRtv.IsValid()) {
                return false;
            }
            if (!Transition(context.commandList,
                            context.normalRoughness,
                            context.normalRoughnessState,
                            D3D12_RESOURCE_STATE_RENDER_TARGET)) {
                return false;
            }
            rtvs[numRtvs++] = context.normalRoughnessRtv.cpu;
        }
    } else {
        if (!context.backBuffer || !context.backBufferRtv.IsValid()) {
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
        rtvs[numRtvs++] = context.backBufferRtv.cpu;
    }

    if (numRtvs == 0 || targetWidth == 0 || targetHeight == 0) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = context.depthDsv.cpu;
    context.commandList->OMSetRenderTargets(numRtvs, rtvs, FALSE, &dsv);

    for (UINT i = 0; i < numRtvs; ++i) {
        context.commandList->ClearRenderTargetView(rtvs[i], context.clearColor, 0, nullptr);
    }
    context.commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(targetWidth);
    viewport.Height = static_cast<float>(targetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(targetWidth);
    scissorRect.bottom = static_cast<LONG>(targetHeight);
    context.commandList->RSSetViewports(1, &viewport);
    context.commandList->RSSetScissorRects(1, &scissorRect);

    return BindGraphicsState(context);
}

} // namespace Cortex::Graphics::MainPassTargetPass
