#pragma once

#include <cstdint>

#include "Graphics/RendererBloomState.h" // BloomPassState + kBloomLevels/kBloomDescriptorSlots
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

class DX12Device;

// Per-frame dependencies for the immediate (non-render-graph) bloom chain.
struct BloomContext {
    DX12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12Pipeline* downsample = nullptr;
    DX12Pipeline* blurH = nullptr;
    DX12Pipeline* blurV = nullptr;
    DX12Pipeline* composite = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;
    uint32_t internalWidth = 0;
    uint32_t internalHeight = 0;
    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    bool hdrSrvValid = false;
    ID3D12Resource* materialAwareNormalRoughness = nullptr;
    ID3D12Resource* materialAwareMaterialExt2 = nullptr;
};

// Owns the bloom pyramid resources and the immediate downsample/blur/composite
// chain. The render-graph bloom paths (ExecuteBloomInRenderGraph + the fused
// end-frame-graph bloom) stay in Renderer and read this subsystem's State().
class BloomSubsystem {
public:
    [[nodiscard]] Result<void> CreateResources(const BloomContext& ctx);
    void Render(const BloomContext& ctx);

    [[nodiscard]] BloomPassState<kBloomLevels, kBloomDescriptorSlots>& State() { return m_state; }
    [[nodiscard]] const BloomPassState<kBloomLevels, kBloomDescriptorSlots>& State() const { return m_state; }

private:
    [[nodiscard]] bool PrepareState();
    [[nodiscard]] bool BindSRV(DescriptorHandle source, const char* label, uint32_t tableSlot);
    [[nodiscard]] bool BindTexture(ID3D12Resource* source, DXGI_FORMAT format, const char* label, uint32_t tableSlot);
    [[nodiscard]] bool DownsampleBase(bool skipTransitions);
    [[nodiscard]] bool DownsampleLevel(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool BlurHorizontal(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool BlurVertical(uint32_t level, bool skipTransitions);
    [[nodiscard]] bool Composite(bool skipTransitions);
    [[nodiscard]] bool CopyCompositeToCombined(bool skipTransitions);

    const BloomContext* m_ctx = nullptr; // transient, valid only during Render()
    BloomPassState<kBloomLevels, kBloomDescriptorSlots> m_state;
};

} // namespace Cortex::Graphics
