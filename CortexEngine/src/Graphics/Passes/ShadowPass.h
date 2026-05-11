#pragma once

#include "Graphics/MaterialModel.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RendererSceneSnapshot.h"
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/ShaderTypes.h"
#include "Graphics/Passes/MeshDrawPass.h"
#include "Graphics/Passes/ShadowTargetPass.h"

#include <functional>
#include <span>

namespace Cortex::Graphics::ShadowPass {

struct DrawContext {
    ShadowTargetPass::TransitionContext target;
    std::span<const DescriptorHandle> dsvs;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    MeshDrawPass::PipelineStateContext pipeline;
    DX12Pipeline* shadow = nullptr;
    DX12Pipeline* shadowDoubleSided = nullptr;
    DX12Pipeline* shadowAlpha = nullptr;
    DX12Pipeline* shadowAlphaDoubleSided = nullptr;
    const RendererSceneSnapshot* snapshot = nullptr;
    ConstantBuffer<ObjectConstants>* objectConstants = nullptr;
    ConstantBuffer<MaterialConstants>* materialConstants = nullptr;
    ConstantBuffer<ShadowConstants>* shadowConstants = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    MaterialTextureFallbacks materialFallbacks{};
    uint32_t* drawCounter = nullptr;
    uint32_t cascadeCount = 0;
    uint32_t maxShadowedLocalLights = 0;
    uint32_t shadowArraySize = 0;
    bool localShadowHasShadow = false;
    uint32_t localShadowCount = 0;
};

struct GraphContext {
    RGResourceHandle shadowMap;
    DrawContext draw;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool Draw(const DrawContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::ShadowPass
