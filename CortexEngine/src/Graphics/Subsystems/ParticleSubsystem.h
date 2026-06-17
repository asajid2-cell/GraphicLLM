#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

#include "Graphics/RendererParticleState.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex { namespace Scene { class ECS_Registry; } }

namespace Cortex::Graphics {

// Per-frame dependencies for the GPU particle billboard pass.
struct ParticleRenderContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    bool deviceRemoved = false;

    DX12Pipeline* particlePipeline = nullptr;
    DX12ComputePipeline* particleLifecycleCompute = nullptr;
    ID3D12RootSignature* compactComputeRootSignature = nullptr;
    DX12RootSignature* rootSignature = nullptr;

    glm::mat4 viewProjectionNoJitter{1.0f};
    float time = 0.0f;
    glm::vec4 cameraPosition{0.0f};

    // HDR + depth targets (mutable state refs for transitions).
    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv{};
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv{};
    DescriptorHandle shadowEnvironmentTable{};

    // Allocates identity object constants from the renderer's ring at draw time.
    std::function<D3D12_GPU_VIRTUAL_ADDRESS()> allocObjectConstants;
    std::function<void()> waitForGpu;
    std::function<void(const char* label, HRESULT hr)> reportDeviceRemoved;

    uint32_t* outParticleDraws = nullptr;
    uint32_t* outParticleInstances = nullptr;
};

// Owns particle GPU buffers/lifecycle state and the billboard draw.
class ParticleSubsystem {
public:
    void Render(Scene::ECS_Registry* registry, const ParticleRenderContext& ctx);

    [[nodiscard]] ParticleRenderState& State() { return m_state; }
    [[nodiscard]] const ParticleRenderState& State() const { return m_state; }

private:
    ParticleRenderState m_state;
};

} // namespace Cortex::Graphics
