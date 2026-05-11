#include "Graphics/Passes/VoxelPass.h"

#include "Graphics/Passes/FullscreenPass.h"

namespace Cortex::Graphics::VoxelPass {

bool Draw(const DrawContext& context) {
    if (!context.commandList || !context.rootSignature ||
        !context.rootSignature->GetRootSignature() || !context.pipeline ||
        !context.pipeline->GetPipelineState() || !context.descriptorManager ||
        !context.backBuffer || context.width == 0 || context.height == 0) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = context.backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    context.commandList->ResourceBarrier(1, &barrier);

    context.commandList->OMSetRenderTargets(1, &context.backBufferRtv, FALSE, nullptr);

    const float clearColor[4] = {0.2f, 0.0f, 0.4f, 1.0f};
    context.commandList->ClearRenderTargetView(context.backBufferRtv, clearColor, 0, nullptr);

    FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);

    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    ID3D12DescriptorHeap* heaps[] = {context.descriptorManager->GetCBV_SRV_UAV_Heap()};
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetGraphicsRootConstantBufferView(1, context.frameConstants);

    if (context.voxelGridSrv.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(3, context.voxelGridSrv.gpu);
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

} // namespace Cortex::Graphics::VoxelPass
