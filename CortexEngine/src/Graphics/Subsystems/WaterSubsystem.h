#pragma once

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

#include "Graphics/Renderer_DiagnosticsTypes.h" // RendererWaterState
#include "Graphics/RendererSceneSnapshot.h"     // RendererSceneSnapshot
#include "Graphics/ShaderTypes.h"               // ObjectConstants, MaterialConstants
#include "Graphics/RHI/DescriptorHeap.h"

namespace Cortex { namespace Scene { class ECS_Registry; struct RenderableComponent; } }

namespace Cortex::Graphics {

// Per-frame dependencies for the forward water/liquid overlay pass. Renderer
// services it depends on (material prep, constant allocation) are injected as
// callbacks so the subsystem stays decoupled from the Renderer god-object.
struct WaterRenderContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12PipelineState* waterPipelineState = nullptr; // null if water pipeline unavailable
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle shadowEnvironmentTable{};

    ID3D12Resource* hdrColor = nullptr;
    D3D12_RESOURCE_STATES* hdrState = nullptr;
    DescriptorHandle hdrRtv{};
    ID3D12Resource* depthBuffer = nullptr;
    D3D12_RESOURCE_STATES* depthState = nullptr;
    DescriptorHandle depthDsv{};
    DescriptorHandle readOnlyDepthDsv{};

    glm::mat4 viewProjectionNoJitter{1.0f};

    // Scene snapshot (current frame's). If null/stale the subsystem rebuilds one.
    const RendererSceneSnapshot* sceneSnapshot = nullptr;
    uint64_t renderFrameCounter = 0;

    DescriptorHandle materialFallbackTable{};

    // Injected Renderer services.
    std::function<void(Scene::RenderableComponent&)> prepareMaterial;
    std::function<D3D12_GPU_VIRTUAL_ADDRESS(const ObjectConstants&)> allocObjectConstants;
    std::function<D3D12_GPU_VIRTUAL_ADDRESS(const MaterialConstants&)> allocMaterialConstants;

    uint32_t* outWaterDraws = nullptr;
};

// Owns water/liquid CPU params and the forward liquid overlay draw.
class WaterSubsystem {
public:
    void Render(Scene::ECS_Registry* registry, const WaterRenderContext& ctx);

    [[nodiscard]] RendererWaterState& State() { return m_state; }
    [[nodiscard]] const RendererWaterState& State() const { return m_state; }

private:
    RendererWaterState m_state;
};

} // namespace Cortex::Graphics
