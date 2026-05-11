#include "Graphics/Passes/IndirectMeshDrawPass.h"

namespace Cortex::Graphics::IndirectMeshDrawPass {

bool RestoreGraphicsState(const RestoreGraphicsStateContext& context) {
    if (!context.commandList || !context.rootSignature || !context.pipelineState) {
        return false;
    }

    context.commandList->SetGraphicsRootSignature(context.rootSignature);
    context.commandList->SetPipelineState(context.pipelineState);
    context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (context.cbvSrvUavHeap) {
        ID3D12DescriptorHeap* heaps[] = { context.cbvSrvUavHeap };
        context.commandList->SetDescriptorHeaps(1, heaps);
    }

    if (context.frameConstants != 0) {
        context.commandList->SetGraphicsRootConstantBufferView(1, context.frameConstants);
    }
    if (context.environmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.environmentTable.gpu);
    }
    if (context.fallbackMaterialTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(3, context.fallbackMaterialTable.gpu);
    }
    if (context.biomeMaterials != 0) {
        context.commandList->SetGraphicsRootConstantBufferView(7, context.biomeMaterials);
    }

    return true;
}

ExecuteResult ExecuteCommands(const ExecuteContext& context) {
    ExecuteResult result{};
    result.maxCommands = context.maxCommands;

    if (!context.commandList ||
        !context.commandSignature ||
        !context.argumentBuffer ||
        context.maxCommands == 0) {
        return result;
    }

    context.commandList->ExecuteIndirect(
        context.commandSignature,
        context.maxCommands,
        context.argumentBuffer,
        0,
        context.countBuffer,
        0);

    result.submitted = true;
    return result;
}

} // namespace Cortex::Graphics::IndirectMeshDrawPass
