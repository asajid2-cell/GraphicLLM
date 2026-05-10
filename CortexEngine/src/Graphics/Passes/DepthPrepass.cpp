#include "DepthPrepass.h"

namespace Cortex::Graphics::DepthPrepass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.depth.IsValid() && static_cast<bool>(context.execute);
}

} // namespace

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "depth_prepass_graph_contract");
        return {};
    }

    graph.AddPass(
        "DepthPrepass",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.depth, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.execute || !context.execute()) {
                Fail(context, "depth_prepass_execute");
            }
        });

    return context.depth;
}

} // namespace Cortex::Graphics::DepthPrepass
