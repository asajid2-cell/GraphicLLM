#include "ShadowPass.h"

namespace Cortex::Graphics::ShadowPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.shadowMap.IsValid() && static_cast<bool>(context.execute);
}

} // namespace

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "shadow_graph_contract");
        return {};
    }

    graph.AddPass(
        "ShadowPass",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.shadowMap, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.execute || !context.execute()) {
                Fail(context, "shadow_execute");
            }
        });

    graph.AddPass(
        "ShadowFinalize",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.shadowMap, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

    return context.shadowMap;
}

} // namespace Cortex::Graphics::ShadowPass
