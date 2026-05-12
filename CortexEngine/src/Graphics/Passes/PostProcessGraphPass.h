#pragma once

#include "Graphics/Passes/PostProcessPass.h"
#include "Graphics/Passes/RTReflectionDebugClearPass.h"
#include "Graphics/RenderGraph.h"

namespace Cortex::Graphics::PostProcessGraphPass {

struct GraphStatus {
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct ResourceHandles {
    RGResourceHandle hdr;
    RGResourceHandle bloom;
    RGResourceHandle ssao;
    RGResourceHandle history;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt1;
    RGResourceHandle materialExt2;
    RGResourceHandle ssr;
    RGResourceHandle velocity;
    RGResourceHandle taa;
    RGResourceHandle rtReflection;
    RGResourceHandle rtReflectionHistory;
    RGResourceHandle hzb;
    RGResourceHandle backBuffer;
    bool wantsHzbDebug = false;
};

struct ExecuteContext {
    bool useBloomOverride = false;
    RGResourceHandle bloom;
    PostProcessPass::DescriptorUpdateContext descriptorUpdate;
    PostProcessPass::DrawContext draw;
    bool* backBufferUsedAsRenderTarget = nullptr;
    bool runRtReflectionDebugClear = false;
    RTReflectionDebugClearPass::ClearContext rtReflectionDebugClear;
    GraphStatus status;
    bool* ranPostProcess = nullptr;
};

struct GraphContext {
    ResourceHandles resources;
    ExecuteContext execute;
    GraphStatus status;
};

void Declare(RGPassBuilder& builder, const ResourceHandles& resources);
void Execute(const RenderGraph& graph, const ExecuteContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::PostProcessGraphPass
