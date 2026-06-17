#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

#include "Graphics/RendererDebugLineState.h"
#include "Graphics/RHI/DX12Pipeline.h"

namespace Cortex::Graphics {

// Per-frame dependencies the debug-line subsystem needs from the renderer.
// Raw D3D12 handles + small service callbacks keep this decoupled from Renderer.
struct DebugLineDrawContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* backBuffer = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    bool deviceRemoved = false;

    // Allocates identity object constants from the renderer's per-frame ring,
    // invoked at draw time to preserve allocation ordering.
    std::function<D3D12_GPU_VIRTUAL_ADDRESS()> allocObjectConstants;
    std::function<void()> waitForGpu;
    std::function<void(const char* label, HRESULT hr)> reportDeviceRemoved;

    // Frame-contract draw counters (incremented on a successful draw).
    uint32_t* outDrawCount = nullptr;
    uint32_t* outVertexCount = nullptr;
};

// Owns debug-line vertex accumulation, GPU buffer lifecycle, and rendering.
// Extracted from the Renderer god-object; Renderer keeps thin forwarders.
class DebugLineSubsystem {
public:
    void AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);
    void Clear();
    void Render(const DebugLineDrawContext& ctx);

    [[nodiscard]] DebugLineRenderState& State() { return m_state; }
    [[nodiscard]] const DebugLineRenderState& State() const { return m_state; }

private:
    DebugLineRenderState m_state;
};

} // namespace Cortex::Graphics
