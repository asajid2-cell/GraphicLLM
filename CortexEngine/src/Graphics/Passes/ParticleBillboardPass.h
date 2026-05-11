#pragma once

#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Graphics/RendererParticleState.h"

namespace Cortex::Graphics::ParticleBillboardPass {

struct TargetBindings {
    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv{};

    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv{};
};

struct DrawContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DescriptorHandle shadowEnvironmentTable{};
    D3D12_GPU_VIRTUAL_ADDRESS objectConstants = 0;
    const ParticleRenderResources* resources = nullptr;
    UINT instanceCount = 0;
    UINT instanceBytes = 0;
};

[[nodiscard]] bool Draw(const DrawContext& context, const TargetBindings& targets);

} // namespace Cortex::Graphics::ParticleBillboardPass
