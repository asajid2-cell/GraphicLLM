#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RendererParticleState.h"

namespace Cortex::Graphics::ParticleGpuPreparePass {

struct PrepareContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    DX12ComputePipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ParticleRenderResources* resources = nullptr;
    const ParticleGpuSource* sources = nullptr;
    UINT sourceCount = 0;
    const ParticleGpuPrepareConstants* constants = nullptr;
};

struct PrepareResult {
    bool executed = false;
    UINT dispatchGroups = 0;
    UINT sourceBytes = 0;
};

[[nodiscard]] PrepareResult Dispatch(const PrepareContext& context);

} // namespace Cortex::Graphics::ParticleGpuPreparePass
