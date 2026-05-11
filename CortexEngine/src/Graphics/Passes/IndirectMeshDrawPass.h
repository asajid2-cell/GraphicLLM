#pragma once

#include "Graphics/GPUCulling.h"
#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::IndirectMeshDrawPass {

struct RestoreGraphicsStateContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* pipelineState = nullptr;
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle environmentTable{};
    DescriptorHandle fallbackMaterialTable{};
    D3D12_GPU_VIRTUAL_ADDRESS biomeMaterials = 0;
};

struct ExecuteContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12CommandSignature* commandSignature = nullptr;
    ID3D12Resource* argumentBuffer = nullptr;
    ID3D12Resource* countBuffer = nullptr;
    UINT maxCommands = 0;
};

struct ExecuteResult {
    bool submitted = false;
    UINT maxCommands = 0;
};

[[nodiscard]] bool RestoreGraphicsState(const RestoreGraphicsStateContext& context);
[[nodiscard]] Result<void> ConfigureCullingRootSignature(GPUCullingPipeline* gpuCulling,
                                                         ID3D12RootSignature* rootSignature);
[[nodiscard]] Result<void> PrepareAllCommands(GPUCullingPipeline* gpuCulling,
                                              ID3D12GraphicsCommandList* commandList);
[[nodiscard]] ExecuteResult ExecuteCommands(const ExecuteContext& context);

} // namespace Cortex::Graphics::IndirectMeshDrawPass
