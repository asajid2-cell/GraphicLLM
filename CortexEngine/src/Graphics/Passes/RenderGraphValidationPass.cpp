#include "RenderGraphValidationPass.h"

#include <memory>

namespace Cortex::Graphics::RenderGraphValidationPass {

bool AddTransientValidation(RenderGraph& graph, const TransientValidationContext& context) {
    if (!context.passA || !context.passB) {
        if (context.failStage) {
            context.failStage("rg_transient_validation_graph_contract");
        }
        return false;
    }

    auto transientA = std::make_shared<RGResourceHandle>();
    auto transientB = std::make_shared<RGResourceHandle>();

    graph.AddPass(
        "RGTransientValidationA",
        [desc = context.transientDesc, transientA](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            *transientA = builder.CreateTransient(desc);
            builder.Write(*transientA, RGResourceUsage::RenderTarget);
        },
        [context, transientA](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph) {
            context.passA(commandList, graph, *transientA);
        });

    graph.AddPass(
        "RGTransientValidationB",
        [desc = context.transientDesc, transientB](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            *transientB = builder.CreateTransient(desc);
            builder.Write(*transientB, RGResourceUsage::RenderTarget);
        },
        [context, transientB](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph) {
            context.passB(commandList, graph, *transientB);
        });

    return true;
}

} // namespace Cortex::Graphics::RenderGraphValidationPass
