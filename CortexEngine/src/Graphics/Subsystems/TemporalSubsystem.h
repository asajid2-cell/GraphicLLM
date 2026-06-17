#pragma once

#include <cstdint>
#include <functional>

#include "Graphics/RendererTemporalScreenState.h" // TemporalScreenPassState
#include "Graphics/RendererTemporalState.h"        // TemporalAAState
#include "Graphics/TemporalManager.h"              // TemporalManager, TemporalHistoryId
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex { namespace Scene { class ECS_Registry; } }

namespace Cortex::Graphics {

class DX12Device;
class VisibilityBufferRenderer;
struct RendererVisibilityBufferState;

// Per-frame dependencies for motion vectors + temporal AA. The temporal-history
// MANAGER (shared with RT) and the visibility buffer (for VB motion vectors) are
// injected; this subsystem owns the screen + AA temporal state.
struct TemporalContext {
    DX12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* taaPipeline = nullptr;
    DX12Pipeline* motionVectorsPipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;
    bool hasWindow = false;

    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    ID3D12Resource* normalRoughness = nullptr;
    D3D12_RESOURCE_STATES* normalRoughnessState = nullptr;
    DescriptorHandle shadowEnvironmentTable{};

    // Temporal rejection mask (owned by Renderer, read by the TAA resolve).
    ID3D12Resource* maskTexture = nullptr;
    D3D12_RESOURCE_STATES* maskState = nullptr;

    // Shared history manager (also used by RT) + frame counter.
    TemporalManager* historyManager = nullptr;
    uint64_t renderFrameCounter = 0;

    // Visibility-buffer motion-vector service.
    VisibilityBufferRenderer* visibilityBuffer = nullptr;
    const RendererVisibilityBufferState* vbState = nullptr;

    // Renderer service: refresh the TAA resolve descriptor table.
    std::function<void()> updateResolveTable;
};

// Owns motion-vector + TAA screen state and AA controls, and runs the motion
// vector + temporal AA passes.
class TemporalSubsystem {
public:
    void RenderMotionVectors(const TemporalContext& ctx);
    void RenderTAA(const TemporalContext& ctx);

    void InvalidateTAAHistory(TemporalManager& mgr, uint64_t frameCounter, const char* reason);
    void MarkTAAHistoryValid(TemporalManager& mgr, uint64_t frameCounter);

    [[nodiscard]] TemporalScreenPassState& ScreenState() { return m_screen; }
    [[nodiscard]] const TemporalScreenPassState& ScreenState() const { return m_screen; }
    [[nodiscard]] TemporalAAState& AAState() { return m_aa; }
    [[nodiscard]] const TemporalAAState& AAState() const { return m_aa; }

private:
    [[nodiscard]] bool SeedTAAHistory(bool skipTransitions);
    [[nodiscard]] bool ResolveTAAIntermediate(bool skipTransitions);
    [[nodiscard]] bool CopyTAAIntermediateToHDR(bool skipTransitions);
    [[nodiscard]] bool CopyHDRToTAAHistory(bool skipTransitions);

    const TemporalContext* m_ctx = nullptr; // transient, valid during Render*()
    TemporalScreenPassState m_screen;
    TemporalAAState m_aa;
};

} // namespace Cortex::Graphics
