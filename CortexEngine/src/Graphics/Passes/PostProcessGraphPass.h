#pragma once

#include "Graphics/RenderGraph.h"

#include <functional>

namespace Cortex::Graphics::PostProcessGraphPass {

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
    std::function<void(ID3D12Resource*)> renderWithBloomOverride;
    std::function<void()> renderDefault;
    std::function<void(const char*)> failBloomStage;
    std::function<void()> markRan;
};

struct GraphContext {
    ResourceHandles resources;
    ExecuteContext execute;
    std::function<void(const char*)> failStage;
};

void Declare(RGPassBuilder& builder, const ResourceHandles& resources);
void Execute(const RenderGraph& graph, const ExecuteContext& context);
[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::PostProcessGraphPass
