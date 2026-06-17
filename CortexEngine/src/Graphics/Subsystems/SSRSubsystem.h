#pragma once

#include <cstdint>

#include "Graphics/RendererSSRState.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

// Per-frame dependencies for the immediate SSR draw.
struct SSRRenderContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* ssrPipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;
    bool skipTransitions = false;

    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    ID3D12Resource* normalRoughness = nullptr;
    D3D12_RESOURCE_STATES* normalRoughnessState = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;

    // Visibility-buffer normal-roughness fallback (preferred when present).
    bool vbRenderedThisFrame = false;
    ID3D12Resource* vbNormalRoughness = nullptr;

    DescriptorHandle shadowAndEnvDescriptor{};
};

// Owns the SSR color target + descriptor table and the immediate SSR draw.
// The render-graph SSR orchestration (ExecuteSSRInRenderGraph) stays in Renderer.
class SSRSubsystem {
public:
    void RenderImmediate(const SSRRenderContext& ctx);

    [[nodiscard]] SSRPassState& State() { return m_state; }
    [[nodiscard]] const SSRPassState& State() const { return m_state; }

private:
    SSRPassState m_state;
};

} // namespace Cortex::Graphics
