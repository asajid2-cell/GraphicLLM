#include "Graphics/Passes/MeshDrawPass.h"

#include "Graphics/ShaderTypes.h"
#include "Scene/Components.h"

namespace Cortex::Graphics::MeshDrawPass {

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
