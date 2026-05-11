#pragma once

#include "Graphics/MaterialModel.h"
#include "Graphics/RenderGraph.h"
#include "Graphics/RendererSceneSnapshot.h"
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/RHI/DX12Pipeline.h"
#include "Graphics/ShaderTypes.h"
#include "Graphics/Passes/DepthPrepassTargetPass.h"
#include "Graphics/Passes/MeshDrawPass.h"

namespace Cortex::Graphics::DepthPrepass {

struct GraphStatus {
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct DrawContext {
    DepthPrepassTargetPass::BindContext target;
    MeshDrawPass::PipelineStateContext pipeline;
    DX12Pipeline* depthOnly = nullptr;
    DX12Pipeline* depthOnlyDoubleSided = nullptr;
    DX12Pipeline* depthOnlyAlpha = nullptr;
    DX12Pipeline* depthOnlyAlphaDoubleSided = nullptr;
    const RendererSceneSnapshot* snapshot = nullptr;
    ConstantBuffer<ObjectConstants>* objectConstants = nullptr;
    ConstantBuffer<MaterialConstants>* materialConstants = nullptr;
    MaterialTextureFallbacks materialFallbacks{};
    uint32_t* drawCounter = nullptr;
};

struct GraphContext {
    RGResourceHandle depth;
    DrawContext draw;
    GraphStatus status;
};

[[nodiscard]] bool Draw(const DrawContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::DepthPrepass
