#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Scene {
struct MeshData;
}

namespace Cortex::Graphics::MeshDrawPass {

struct PipelineStateContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle shadowEnvironmentTable{};
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct ObjectMaterialContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS objectConstants = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialConstants = 0;
    DescriptorHandle materialTable{};
};

struct ShadowConstantsContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    D3D12_GPU_VIRTUAL_ADDRESS shadowConstants = 0;
};

struct DrawResult {
    bool submitted = false;
    UINT indexCount = 0;
};

[[nodiscard]] bool BindPipelineState(const PipelineStateContext& context);
[[nodiscard]] bool SwitchPipelineState(ID3D12GraphicsCommandList* commandList,
                                       ID3D12PipelineState* pipelineState);
[[nodiscard]] bool BindBiomeMaterialConstants(ID3D12GraphicsCommandList* commandList,
                                              D3D12_GPU_VIRTUAL_ADDRESS biomeMaterialConstants);
[[nodiscard]] bool BindShadowConstants(const ShadowConstantsContext& context);
[[nodiscard]] bool BindObjectMaterial(const ObjectMaterialContext& context);
[[nodiscard]] DrawResult DrawIndexedMesh(ID3D12GraphicsCommandList* commandList,
                                         const Cortex::Scene::MeshData& mesh);

} // namespace Cortex::Graphics::MeshDrawPass
