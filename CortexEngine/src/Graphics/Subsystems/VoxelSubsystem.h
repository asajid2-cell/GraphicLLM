#pragma once

#include <cstdint>

#include "Graphics/RendererVoxelState.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex { namespace Scene { class ECS_Registry; } }

namespace Cortex::Graphics {

// Per-frame dependencies for the experimental voxel backend draw.
struct VoxelDrawContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    ID3D12Resource* backBuffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv{};
    uint32_t width = 0;
    uint32_t height = 0;
};

// Owns the dense voxel grid (CPU + GPU), scene voxelization, and the
// experimental fullscreen voxel raymarch draw. Extracted from Renderer.
class VoxelSubsystem {
public:
    void SetBackendEnabled(bool enabled, bool pipelineAvailable);
    [[nodiscard]] bool IsBackendEnabled() const { return m_state.backendEnabled; }
    void MarkGridDirty() { m_state.gridDirty = true; }

    // Builds the grid from the scene (if registry given) then draws the voxel
    // pass. Returns true if a draw was actually issued (so the renderer can
    // mark back-buffer-as-RT and record the frame pass).
    [[nodiscard]] bool Render(Scene::ECS_Registry* registry, const VoxelDrawContext& ctx);

    [[nodiscard]] Result<void> BuildGridFromScene(Scene::ECS_Registry* registry,
                                                  ID3D12Device* device,
                                                  DescriptorHeapManager* descriptorManager);
    [[nodiscard]] Result<void> UploadGridToGPU(ID3D12Device* device,
                                               DescriptorHeapManager* descriptorManager);

    [[nodiscard]] VoxelRenderState& State() { return m_state; }
    [[nodiscard]] const VoxelRenderState& State() const { return m_state; }

private:
    VoxelRenderState m_state;
};

} // namespace Cortex::Graphics
