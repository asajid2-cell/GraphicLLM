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

Result<void> ConfigureCullingRootSignature(GPUCullingPipeline* gpuCulling,
                                           ID3D12RootSignature* rootSignature) {
    if (!gpuCulling) {
        return Result<void>::Ok();
    }
    if (!rootSignature) {
        return Result<void>::Err("ConfigureCullingRootSignature requires a valid root signature");
    }

    return gpuCulling->SetGraphicsRootSignature(rootSignature);
}

Result<void> PrepareAllCommands(GPUCullingPipeline* gpuCulling,
                                ID3D12GraphicsCommandList* commandList) {
    if (!gpuCulling) {
        return Result<void>::Err("PrepareAllCommands requires a valid GPU culling pipeline");
    }
    if (!commandList) {
        return Result<void>::Err("PrepareAllCommands requires a valid command list");
    }

    return gpuCulling->PrepareAllCommandsForExecuteIndirect(commandList);
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
