#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

#include <memory>

namespace Cortex::Graphics {

class DX12Texture;

namespace RTReflectionDispatchPass {

struct ResourceStateRef {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES* state = nullptr;
};

struct PrepareContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ResourceStateRef depth;
    ResourceStateRef normalRoughness;
    ResourceStateRef reflectionOutput;
    bool transitionNormal = true;
};

struct DebugClearContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12DescriptorHeap* descriptorHeap = nullptr;
    ResourceStateRef reflectionOutput;
    DescriptorHandle shaderVisibleUav{};
    DescriptorHandle cpuUav{};
    int clearMode = 0;
};

[[nodiscard]] bool PrepareInputsAndOutput(const PrepareContext& context);
[[nodiscard]] bool ClearOutputForDebugView(const DebugClearContext& context);
void EnsureTextureNonPixelReadable(ID3D12GraphicsCommandList* commandList,
                                   const std::shared_ptr<DX12Texture>& texture);
[[nodiscard]] bool FinalizeOutputWrites(ID3D12GraphicsCommandList* commandList,
                                         ID3D12Resource* reflectionOutput);

} // namespace RTReflectionDispatchPass
} // namespace Cortex::Graphics
