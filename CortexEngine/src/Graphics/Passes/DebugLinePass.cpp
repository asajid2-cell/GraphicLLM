#include "Graphics/Passes/DebugLinePass.h"

namespace Cortex::Graphics::DebugLinePass {

bool Draw(const DrawContext& context) {
    if (!context.commandList ||
        !context.rootSignature ||
        !context.rootSignature->GetRootSignature() ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        !context.state ||
        !context.state->vertexBuffer ||
        context.objectConstants == 0 ||
        context.vertexCount == 0 ||
        context.vertexBytes == 0) {
        return false;
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->SetGraphicsRootSignature(context.rootSignature->GetRootSignature());
    context.commandList->SetGraphicsRootConstantBufferView(0, context.objectConstants);

    D3D12_VERTEX_BUFFER_VIEW vbv = context.state->VertexBufferView(context.vertexCount, context.vertexBytes);
    context.commandList->IASetVertexBuffers(0, 1, &vbv);
    context.commandList->IASetIndexBuffer(nullptr);
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    context.commandList->DrawInstanced(context.vertexCount, 1, 0, 0);
    return true;
}

} // namespace Cortex::Graphics::DebugLinePass
