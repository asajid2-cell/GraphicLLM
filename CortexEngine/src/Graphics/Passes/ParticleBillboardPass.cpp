#include "Graphics/Passes/ParticleBillboardPass.h"

namespace Cortex::Graphics::ParticleBillboardPass {

namespace {
struct QuadVertex {
    float px, py, pz;
    float u, v;
};
} // namespace

bool Draw(const DrawContext& context, const TargetBindings& targets) {
    if (!context.commandList ||
        !context.rootSignature ||
        !context.rootSignature->GetRootSignature() ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        !context.descriptorManager ||
        !context.resources ||
        !context.resources->quadVertexBuffer ||
        !context.resources->instanceBuffer ||
        context.instanceCount == 0 ||
        context.instanceBytes == 0 ||
        context.objectConstants == 0 ||
        !targets.hdrColor ||
        !targets.hdrState ||
        !targets.depthBuffer ||
        !targets.depthState) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (*targets.depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = targets.depthBuffer;
        barriers[barrierCount].Transition.StateBefore = *targets.depthState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        *targets.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    if (*targets.hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = targets.hdrColor;
        barriers[barrierCount].Transition.StateBefore = *targets.hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        *targets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }

    context.commandList->OMSetRenderTargets(1, &targets.hdrRtv, FALSE, &targets.depthDsv);
    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());

    if (context.shadowEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowEnvironmentTable.gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetGraphicsRootConstantBufferView(0, context.objectConstants);

    D3D12_VERTEX_BUFFER_VIEW vbViews[2] = {};
    vbViews[0] = context.resources->QuadVertexBufferView(sizeof(QuadVertex), sizeof(QuadVertex) * 4u);
    vbViews[1] = context.resources->InstanceBufferView(context.instanceCount, context.instanceBytes);

    context.commandList->IASetVertexBuffers(0, 2, vbViews);
    context.commandList->IASetIndexBuffer(nullptr);
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context.commandList->DrawInstanced(4, context.instanceCount, 0, 0);
    return true;
}

} // namespace Cortex::Graphics::ParticleBillboardPass
