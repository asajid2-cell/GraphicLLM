#pragma once

#include "Graphics/RenderGraph.h"

#include <functional>

namespace Cortex::Graphics::DepthPrepass {

struct GraphContext {
    RGResourceHandle depth;
    std::function<bool()> execute;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::DepthPrepass
