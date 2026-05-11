#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::RTReflectionDebugClearPass {

struct ClearContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    ID3D12Resource* reflectionColor = nullptr;
    D3D12_RESOURCE_STATES* reflectionState = nullptr;
    DescriptorHandle shaderVisibleUav{};
    DescriptorHandle cpuUav{};
    int clearMode = 0;
};

[[nodiscard]] bool ClearForDebugView(const ClearContext& context);

} // namespace Cortex::Graphics::RTReflectionDebugClearPass
