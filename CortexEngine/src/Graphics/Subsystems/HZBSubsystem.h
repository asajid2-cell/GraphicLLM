#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "Graphics/RendererHZBState.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

// Per-frame dependencies for HZB (hierarchical-Z) build from the depth buffer.
struct HZBContext {
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;

    // Depth source (mutable state ref for transition tracking).
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthSrv{};

    // Pipelines / signatures.
    ID3D12RootSignature* compactRootSignature = nullptr;
    DX12ComputeRootSignature* fallbackRootSignature = nullptr;
    DX12ComputePipeline* initPipeline = nullptr;
    DX12ComputePipeline* downsamplePipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    uint32_t frameIndex = 0;

    // Camera/frame state recorded into the HZB capture block after a build.
    glm::mat4 captureViewMatrix{1.0f};
    glm::mat4 captureViewProjMatrix{1.0f};
    glm::vec3 captureCameraPosWS{0.0f};
    glm::vec3 captureCameraForwardWS{0.0f};
    float captureNearPlane = 0.0f;
    float captureFarPlane = 0.0f;
    uint64_t captureFrameCounter = 0;
};

// Owns the HZB pyramid resources and the depth-downsample build (immediate +
// render-graph). Extracted from the Renderer god-object.
class HZBSubsystem {
public:
    [[nodiscard]] Result<void> CreateResources(const HZBContext& ctx);
    void BuildFromDepth(const HZBContext& ctx);
    void AddFromDepthPasses(RenderGraph& graph,
                            RGResourceHandle depthHandle,
                            RGResourceHandle hzbHandle,
                            const HZBContext& ctx);

    [[nodiscard]] HZBPassState& State() { return m_state; }
    [[nodiscard]] const HZBPassState& State() const { return m_state; }

private:
    HZBPassState m_state;
};

} // namespace Cortex::Graphics
