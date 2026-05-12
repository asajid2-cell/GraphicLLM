#pragma once

#include "Graphics/RendererParticleState.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RHI/DX12Pipeline.h"

namespace Cortex::Graphics::ParticleGpuLifecyclePass {

struct DispatchContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    DX12ComputePipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ParticleRenderResources* resources = nullptr;
    const ParticleGpuEmitter* emitters = nullptr;
    UINT emitterCount = 0;
    UINT particleCount = 0;
    const ParticleGpuLifecycleConstants* constants = nullptr;
};

struct DispatchResult {
    bool executed = false;
    UINT dispatchGroups = 0;
    UINT uploadBytes = 0;
};

[[nodiscard]] DispatchResult Dispatch(const DispatchContext& context);

} // namespace Cortex::Graphics::ParticleGpuLifecyclePass
