#include "Graphics/Passes/MeshDrawPass.h"

#include "Graphics/ShaderTypes.h"
#include "Scene/Components.h"

namespace Cortex::Graphics::MeshDrawPass {

bool BindPipelineState(const PipelineStateContext& context) {
    if (!context.commandList || !context.rootSignature || !context.pipelineState) {
        return false;
    }

    if (context.cbvSrvUavHeap) {
        ID3D12DescriptorHeap* heaps[] = { context.cbvSrvUavHeap };
        context.commandList->SetDescriptorHeaps(1, heaps);
    }

    context.commandList->SetGraphicsRootSignature(context.rootSignature);
    context.commandList->SetPipelineState(context.pipelineState);

    if (context.frameConstants != 0) {
        context.commandList->SetGraphicsRootConstantBufferView(1, context.frameConstants);
    }

    if (context.shadowEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowEnvironmentTable.gpu);
    }

    context.commandList->IASetPrimitiveTopology(context.topology);
    return true;
}

bool BindObjectMaterial(const ObjectMaterialContext& context) {
    if (!context.commandList || context.objectConstants == 0) {
        return false;
    }

    context.commandList->SetGraphicsRootConstantBufferView(0, context.objectConstants);
    if (context.materialConstants != 0) {
        context.commandList->SetGraphicsRootConstantBufferView(2, context.materialConstants);
    }
    if (context.materialTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(3, context.materialTable.gpu);
    }
    return true;
}

DrawResult DrawIndexedMesh(ID3D12GraphicsCommandList* commandList,
                           const Cortex::Scene::MeshData& mesh) {
    DrawResult result{};
    if (!commandList ||
        !mesh.gpuBuffers ||
        !mesh.gpuBuffers->vertexBuffer ||
        !mesh.gpuBuffers->indexBuffer ||
        mesh.indices.empty()) {
        return result;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = mesh.gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
    vbv.SizeInBytes = static_cast<UINT>(mesh.positions.size() * sizeof(Vertex));
    vbv.StrideInBytes = sizeof(Vertex);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = mesh.gpuBuffers->indexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes = static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t));
    ibv.Format = DXGI_FORMAT_R32_UINT;

    const UINT indexCount = static_cast<UINT>(mesh.indices.size());
    commandList->IASetVertexBuffers(0, 1, &vbv);
    commandList->IASetIndexBuffer(&ibv);
    commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

    result.submitted = true;
    result.indexCount = indexCount;
    return result;
}

} // namespace Cortex::Graphics::MeshDrawPass
