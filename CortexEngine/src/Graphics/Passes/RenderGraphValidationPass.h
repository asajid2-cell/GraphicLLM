#pragma once

#include "Graphics/RenderGraph.h"

#include <functional>

namespace Cortex::Graphics::RenderGraphValidationPass {

using StageCallback = std::function<void(ID3D12GraphicsCommandList*, const RenderGraph&, RGResourceHandle)>;

struct TransientValidationContext {
    RGResourceDesc transientDesc;
    StageCallback passA;
    StageCallback passB;
    std::function<void(const char*)> failStage;
};

[[nodiscard]] bool AddTransientValidation(RenderGraph& graph, const TransientValidationContext& context);

} // namespace Cortex::Graphics::RenderGraphValidationPass
