#include "SSRPass.h"

#include "DescriptorTable.h"
#include "FullscreenPass.h"

namespace Cortex::Graphics::SSRPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.hdr.IsValid() &&
           context.depth.IsValid() &&
           context.normalRoughness.IsValid() &&
           context.ssr.IsValid() &&
           context.prepare.commandList &&
           static_cast<bool>(context.draw.pipeline);
}

} // namespace

bool PrepareTargets(const PrepareContext& context) {
    if (!context.commandList ||
        !context.ssrTarget.resource ||
        !context.ssrTarget.state ||
        !context.hdr.resource ||
        !context.hdr.state ||
        !context.depth.resource ||
        !context.depth.state) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[4] = {};
    UINT barrierCount = 0;

    const ResourceStateRef resources[] = {
        context.ssrTarget,
        context.hdr,
        context.normalRoughness,
        context.depth,
    };
    for (const ResourceStateRef& resource : resources) {
        if (!resource.resource || !resource.state) {
            continue;
        }
        if (!context.skipTransitions && *resource.state != resource.desiredState) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = resource.resource;
            barriers[barrierCount].Transition.StateBefore = *resource.state;
            barriers[barrierCount].Transition.StateAfter = resource.desiredState;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
        }
    }

    if (barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }

    for (const ResourceStateRef& resource : resources) {
        if (resource.resource && resource.state) {
            *resource.state = resource.desiredState;
        }
    }

    return true;
}

bool Draw(const DrawContext& context) {
    if (!context.device || !context.commandList || !context.pipeline ||
        !context.pipeline->GetPipelineState() || !context.target ||
        !context.targetRtv.IsValid() || !context.hdr || !context.depth ||
        !context.normalRoughness || context.srvTable.size() < kDescriptorSlots) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv.cpu, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.target);

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
            context.device, context.srvTable[0], context.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !DescriptorTable::WriteTexture2DSRV(
            context.device, context.srvTable[1], context.depth, DXGI_FORMAT_R32_FLOAT) ||
        !DescriptorTable::WriteTexture2DSRV(
            context.device, context.srvTable[2], context.normalRoughness, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        return false;
    }

    for (uint32_t slot = 3; slot < kDescriptorSlots; ++slot) {
        if (!DescriptorTable::WriteTexture2DSRV(
                context.device, context.srvTable[slot], nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
            return false;
        }
    }

    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    if (context.shadowAndEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowAndEnvironmentTable.gpu);
    }

    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "ssr_graph_contract");
        return {};
    }

    graph.AddPass(
        "SSR",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.hdr, RGResourceUsage::ShaderResource);
            builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource);
            builder.Write(context.ssr, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!PrepareTargets(context.prepare) || !Draw(context.draw)) {
                Fail(context, "ssr_execute");
            }
        });

    graph.AddPass(
        "SSRFinalize",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.ssr, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

    return context.ssr;
}

} // namespace Cortex::Graphics::SSRPass
