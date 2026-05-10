#include "SSAOPass.h"

#include "DescriptorTable.h"
#include "FullscreenPass.h"

namespace Cortex::Graphics::SSAOPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.depth.IsValid() &&
           context.ssao.IsValid() &&
           static_cast<bool>(context.execute);
}

} // namespace

bool DrawGraphics(const GraphicsContext& context) {
    if (!context.device || !context.commandList || !context.pipeline ||
        !context.pipeline->GetPipelineState() || !context.target ||
        !context.targetRtv.IsValid() || !context.depth || context.srvTable.empty()) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv.cpu, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.target);

    const float clearColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    context.commandList->ClearRenderTargetView(context.targetRtv.cpu, clearColor, 0, nullptr);

    if (!FullscreenPass::BindGraphicsState({
            context.commandList,
            context.descriptorManager,
            context.rootSignature,
            context.frameConstants,
        })) {
        return false;
    }
    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());

    if (!DescriptorTable::WriteTexture2DSRV(
            context.device, context.srvTable[0], context.depth, DXGI_FORMAT_R32_FLOAT)) {
        return false;
    }

    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

bool DispatchCompute(const ComputeContext& context) {
    if (!context.device || !context.commandList || !context.descriptorManager ||
        !context.rootSignature || !context.pipeline || !context.pipeline->GetPipelineState() ||
        !context.target || !context.depth || context.srvTable.empty() ||
        context.uavTable.empty()) {
        return false;
    }

    context.commandList->SetComputeRootSignature(context.rootSignature);
    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = {context.descriptorManager->GetCBV_SRV_UAV_Heap()};
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetComputeRootConstantBufferView(context.frameConstantsRoot, context.frameConstants);

    for (DescriptorHandle handle : context.srvTable) {
        if (!DescriptorTable::WriteTexture2DSRV(context.device, handle, context.depth, DXGI_FORMAT_R32_FLOAT)) {
            return false;
        }
    }

    for (DescriptorHandle handle : context.uavTable) {
        if (!DescriptorTable::WriteTexture2DUAV(context.device, handle, context.target, DXGI_FORMAT_R8_UNORM)) {
            return false;
        }
    }

    context.commandList->SetComputeRootDescriptorTable(context.srvTableRoot, context.srvTable[0].gpu);
    context.commandList->SetComputeRootDescriptorTable(context.uavTableRoot, context.uavTable[0].gpu);

    const D3D12_RESOURCE_DESC desc = context.target->GetDesc();
    const UINT dispatchX = static_cast<UINT>((desc.Width + 7) / 8);
    const UINT dispatchY = static_cast<UINT>((static_cast<UINT64>(desc.Height) + 7) / 8);
    context.commandList->Dispatch(dispatchX, dispatchY, 1);
    return true;
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "ssao_graph_contract");
        return {};
    }

    graph.AddPass(
        "SSAO",
        [context](RGPassBuilder& builder) {
            builder.SetType(context.useCompute ? RGPassType::Compute : RGPassType::Graphics);
            builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Write(context.ssao,
                          context.useCompute ? RGResourceUsage::UnorderedAccess : RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.execute || !context.execute()) {
                Fail(context, "ssao_execute");
            }
        });

    graph.AddPass(
        "SSAOFinalize",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.ssao, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

    return context.ssao;
}

} // namespace Cortex::Graphics::SSAOPass
