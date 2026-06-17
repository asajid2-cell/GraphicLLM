#pragma once

#include <cstdint>
#include <functional>

#include "Graphics/RendererShadowState.h"        // ShadowMapPassState, ShadowCascadeFrameState, k* constants
#include "Graphics/RendererLocalShadowState.h"   // RendererLocalShadowState
#include "Graphics/Renderer_ConstantBuffer.h"    // ConstantBuffer<T>
#include "Graphics/ShaderTypes.h"                // ObjectConstants/MaterialConstants/ShadowConstants
#include "Graphics/MaterialModel.h"              // MaterialTextureFallbacks
#include "Graphics/RendererSceneSnapshot.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex { namespace Scene { class ECS_Registry; struct RenderableComponent; } }

namespace Cortex::Graphics {

class DX12Device;

// Per-frame / creation dependencies for the shadow subsystem. The Renderer
// services it needs (material prep, environment-descriptor-table update) are
// injected as callbacks; the shadow map itself is a producer the rest of the
// renderer reads via m_shadows.Resources().
struct ShadowContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;

    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12DescriptorHeap* cbvSrvUavHeap = nullptr;
    DX12Pipeline* shadow = nullptr;
    DX12Pipeline* shadowDoubleSided = nullptr;
    DX12Pipeline* shadowAlpha = nullptr;
    DX12Pipeline* shadowAlphaDoubleSided = nullptr;
    bool shadowPipelineValid = false;

    ConstantBuffer<ObjectConstants>* objectConstants = nullptr;
    ConstantBuffer<MaterialConstants>* materialConstants = nullptr;
    ConstantBuffer<ShadowConstants>* shadowConstants = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    MaterialTextureFallbacks materialFallbacks{};

    bool skipTransitions = false;
    const RendererSceneSnapshot* sceneSnapshot = nullptr;
    uint64_t renderFrameCounter = 0;
    uint32_t* outShadowDraws = nullptr;

    std::function<void(Scene::RenderableComponent&)> prepareMaterial;
    std::function<void()> updateEnvironmentTable;
};

// Owns the shadow map (cascades + local), cascade frame state, and the shadow
// depth pass + map resource lifecycle.
class ShadowSubsystem {
public:
    [[nodiscard]] Result<void> CreateResources(const ShadowContext& ctx);
    void RecreateForCurrentSize(const ShadowContext& ctx);
    void RenderPass(Scene::ECS_Registry* registry, const ShadowContext& ctx);

    [[nodiscard]] ShadowMapPassState<kShadowArraySize, kShadowCascadeCount>& Resources() { return m_resources; }
    [[nodiscard]] const ShadowMapPassState<kShadowArraySize, kShadowCascadeCount>& Resources() const { return m_resources; }
    [[nodiscard]] RendererLocalShadowState<kMaxShadowedLocalLights>& Local() { return m_local; }
    [[nodiscard]] const RendererLocalShadowState<kMaxShadowedLocalLights>& Local() const { return m_local; }
    [[nodiscard]] ShadowCascadeFrameState<kShadowCascadeCount>& Cascade() { return m_cascade; }
    [[nodiscard]] const ShadowCascadeFrameState<kShadowCascadeCount>& Cascade() const { return m_cascade; }

private:
    ShadowMapPassState<kShadowArraySize, kShadowCascadeCount> m_resources;
    RendererLocalShadowState<kMaxShadowedLocalLights> m_local;
    ShadowCascadeFrameState<kShadowCascadeCount> m_cascade;
};

} // namespace Cortex::Graphics
