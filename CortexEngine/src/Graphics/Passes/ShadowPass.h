#pragma once

#include "Graphics/RenderGraph.h"

#include <functional>

namespace Cortex::Graphics::ShadowPass {

struct GraphContext {
    RGResourceHandle shadowMap;
    std::function<bool()> execute;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context);

} // namespace Cortex::Graphics::ShadowPass
