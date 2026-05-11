#pragma once

#include "Graphics/RHI/D3D12Includes.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics::ShadowTargetPass {

struct TransitionContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* shadowMap = nullptr;
    D3D12_RESOURCE_STATES* resourceState = nullptr;
    bool* initializedForEditor = nullptr;
    bool skipTransitions = false;
};

struct SliceContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHandle dsv{};
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
};

[[nodiscard]] bool TransitionToDepthWrite(const TransitionContext& context);
[[nodiscard]] bool BindAndClearSlice(const SliceContext& context);
[[nodiscard]] bool TransitionToShaderResource(const TransitionContext& context);

} // namespace Cortex::Graphics::ShadowTargetPass
