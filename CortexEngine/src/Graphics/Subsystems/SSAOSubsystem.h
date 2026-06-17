#pragma once

#include <cstdint>

#include "Graphics/RendererSSAOState.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

// Per-frame dependencies for the SSAO immediate (non-render-graph) draws.
struct SSAORenderContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* graphicsRootSignature = nullptr;
    DX12Pipeline* ssaoPipeline = nullptr;
    DX12ComputePipeline* ssaoComputePipeline = nullptr;
    ID3D12RootSignature* compactComputeRootSignature = nullptr; // may be null
    DX12ComputeRootSignature* computeRootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;
    bool skipTransitions = false;
    // Depth source (foreign; mutable state ref for transitions).
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    bool depthSrvValid = false;
};

// Owns the SSAO target + descriptor tables and the immediate graphics/compute
// SSAO draws. The render-graph SSAO orchestration stays in Renderer (it owns
// the frame graph + stats) but reads this subsystem's State().
class SSAOSubsystem {
public:
    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            uint32_t width,
                                            uint32_t height);
    void RenderImmediate(const SSAORenderContext& ctx);
    void RenderAsync(const SSAORenderContext& ctx);

    [[nodiscard]] SSAOPassState& State() { return m_state; }
    [[nodiscard]] const SSAOPassState& State() const { return m_state; }

private:
    SSAOPassState m_state;
};

} // namespace Cortex::Graphics
