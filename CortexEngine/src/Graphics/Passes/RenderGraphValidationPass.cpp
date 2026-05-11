#include "RenderGraphValidationPass.h"

#include "DescriptorTable.h"

#include <memory>

namespace Cortex::Graphics::RenderGraphValidationPass {

namespace {

void Fail(const TransientValidationContext& context, const char* reason) {
    if (context.failStage) {
        context.failStage(reason);
    }
}

bool IsValidTarget(const TransientValidationTarget& target) {
    return target.device &&
           target.descriptorManager &&
           target.rtv.IsValid() &&
           target.srv.IsValid();
}

bool ClearTransientTarget(ID3D12GraphicsCommandList* commandList,
                          const RenderGraph& graph,
                          RGResourceHandle transient,
                          const TransientValidationTarget& target,
                          const TransientValidationContext& context) {
    if (!commandList || !IsValidTarget(target)) {
        Fail(context, target.descriptorFailureReason ? target.descriptorFailureReason : "rg_transient_validation_descriptor_failed");
        return false;
    }

    ID3D12Resource* resource = graph.GetResource(transient);
    if (!DescriptorTable::WriteTexture2DRTVAndSRV(
            target.device,
            resource,
            target.rtv,
            target.srv,
            DXGI_FORMAT_R8G8B8A8_UNORM)) {
        Fail(context, target.descriptorFailureReason ? target.descriptorFailureReason : "rg_transient_validation_descriptor_failed");
        return false;
    }

    commandList->ClearRenderTargetView(target.rtv.cpu, target.clearColor.data(), 0, nullptr);
    return true;
}

} // namespace

Result<TransientValidationViews> CreateTransientValidationViews(DescriptorHeapManager* descriptorManager) {
    TransientValidationViews views{};
    auto viewsAResult = DescriptorTable::EnsureColorTargetViewHandles(
        descriptorManager,
        views.rtvA,
        views.srvA,
        "RG transient validation A");
    if (viewsAResult.IsErr()) {
        return Result<TransientValidationViews>::Err(viewsAResult.Error());
    }

    auto viewsBResult = DescriptorTable::EnsureColorTargetViewHandles(
        descriptorManager,
        views.rtvB,
        views.srvB,
        "RG transient validation B");
    if (viewsBResult.IsErr()) {
        return Result<TransientValidationViews>::Err(viewsBResult.Error());
    }

    return Result<TransientValidationViews>::Ok(views);
}

bool AddTransientValidation(RenderGraph& graph, const TransientValidationContext& context) {
    if (!IsValidTarget(context.passA) || !IsValidTarget(context.passB)) {
        Fail(context, "rg_transient_validation_graph_contract");
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
            ClearTransientTarget(commandList, graph, *transientA, context.passA, context);
        });

    graph.AddPass(
        "RGTransientValidationB",
        [desc = context.transientDesc, transientB](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            *transientB = builder.CreateTransient(desc);
            builder.Write(*transientB, RGResourceUsage::RenderTarget);
        },
        [context, transientB](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph) {
            ClearTransientTarget(commandList, graph, *transientB, context.passB, context);
        });

    return true;
}

} // namespace Cortex::Graphics::RenderGraphValidationPass
