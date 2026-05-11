#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <cstdint>

namespace Cortex::Graphics::MainPassTargetPass {

struct PrepareContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* geometryPipeline = nullptr;

    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthDsv{};

    ID3D12Resource* rtShadowMask = nullptr;
    D3D12_RESOURCE_STATES* rtShadowMaskState = nullptr;
    ID3D12Resource* rtGIColor = nullptr;
    D3D12_RESOURCE_STATES* rtGIColorState = nullptr;

    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    DescriptorHandle hdrRtv{};
    ID3D12Resource* normalRoughness = nullptr;
    D3D12_RESOURCE_STATES* normalRoughnessState = nullptr;
    DescriptorHandle normalRoughnessRtv{};

    ID3D12Resource* backBuffer = nullptr;
    DescriptorHandle backBufferRtv{};
    uint32_t backBufferWidth = 0;
    uint32_t backBufferHeight = 0;
    bool* backBufferUsedAsRTThisFrame = nullptr;

    float clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
};

[[nodiscard]] bool Prepare(const PrepareContext& context);

} // namespace Cortex::Graphics::MainPassTargetPass
