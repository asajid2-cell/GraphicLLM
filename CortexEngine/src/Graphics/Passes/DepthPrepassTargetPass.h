#pragma once

#include "Graphics/RendererDepthState.h"
#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

#include <functional>

namespace Cortex::Graphics::DepthPrepassTargetPass {

struct ResourceCreateContext {
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DepthTargetState* depthState = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    float renderScale = 1.0f;
    std::function<void(HRESULT)> reportDeviceRemoved;
};

struct BindContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthDsv{};
    bool skipTransitions = false;
    bool clearDepth = true;
};

[[nodiscard]] Result<void> CreateResources(const ResourceCreateContext& context);
[[nodiscard]] bool BindAndClear(const BindContext& context);

} // namespace Cortex::Graphics::DepthPrepassTargetPass
