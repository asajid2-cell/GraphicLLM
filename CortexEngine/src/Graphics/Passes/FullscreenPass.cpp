#include "FullscreenPass.h"

namespace Cortex::Graphics::FullscreenPass {

void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList,
                           uint32_t width,
                           uint32_t height) {
    if (!commandList || width == 0 || height == 0) {
        return;
    }

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor{};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(width);
    scissor.bottom = static_cast<LONG>(height);

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
}

void SetViewportAndScissor(ID3D12GraphicsCommandList* commandList,
                           ID3D12Resource* resource) {
    if (!commandList || !resource) {
        return;
    }

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    SetViewportAndScissor(commandList,
                          static_cast<uint32_t>(desc.Width),
                          static_cast<uint32_t>(desc.Height));
}

bool BindGraphicsState(const GraphicsStateContext& context) {
    if (!context.commandList || !context.descriptorManager || !context.rootSignature ||
        !context.rootSignature->GetRootSignature() || !context.frameConstants) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetGraphicsRootConstantBufferView(1, context.frameConstants);
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return true;
}

void DrawTriangle(ID3D12GraphicsCommandList* commandList) {
    if (!commandList) {
        return;
    }
    commandList->DrawInstanced(3, 1, 0, 0);
}

} // namespace Cortex::Graphics::FullscreenPass
