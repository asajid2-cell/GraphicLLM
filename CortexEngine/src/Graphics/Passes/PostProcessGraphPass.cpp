#include "PostProcessGraphPass.h"

namespace Cortex::Graphics::PostProcessGraphPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.resources.hdr.IsValid() &&
           context.resources.backBuffer.IsValid() &&
           context.execute.descriptorUpdate.device &&
           !context.execute.descriptorUpdate.srvTable.empty() &&
           context.execute.draw.commandList &&
           context.execute.draw.pipeline;
}

} // namespace

void Declare(RGPassBuilder& builder, const ResourceHandles& resources) {
    builder.SetType(RGPassType::Graphics);
    builder.Read(resources.hdr, RGResourceUsage::ShaderResource);
    if (resources.bloom.IsValid()) builder.Read(resources.bloom, RGResourceUsage::ShaderResource);
    if (resources.ssao.IsValid()) builder.Read(resources.ssao, RGResourceUsage::ShaderResource);
    if (resources.history.IsValid()) builder.Read(resources.history, RGResourceUsage::ShaderResource);
    if (resources.depth.IsValid()) {
        builder.Read(resources.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
    }
    if (resources.normalRoughness.IsValid()) builder.Read(resources.normalRoughness, RGResourceUsage::ShaderResource);
    if (resources.emissiveMetallic.IsValid()) {
        builder.Read(resources.emissiveMetallic, RGResourceUsage::ShaderResource);
    }
    if (resources.materialExt1.IsValid()) builder.Read(resources.materialExt1, RGResourceUsage::ShaderResource);
    if (resources.materialExt2.IsValid()) builder.Read(resources.materialExt2, RGResourceUsage::ShaderResource);
    if (resources.ssr.IsValid()) builder.Read(resources.ssr, RGResourceUsage::ShaderResource);
    if (resources.velocity.IsValid()) builder.Read(resources.velocity, RGResourceUsage::ShaderResource);
    if (resources.taa.IsValid()) builder.Read(resources.taa, RGResourceUsage::ShaderResource);
    if (resources.rtReflection.IsValid()) builder.Read(resources.rtReflection, RGResourceUsage::ShaderResource);
    if (resources.rtReflectionHistory.IsValid()) {
        builder.Read(resources.rtReflectionHistory, RGResourceUsage::ShaderResource);
    }
    if (resources.hzb.IsValid() && resources.wantsHzbDebug) {
        builder.Read(resources.hzb, RGResourceUsage::ShaderResource);
    }
    builder.Write(resources.backBuffer, RGResourceUsage::RenderTarget);
}

void Execute(const RenderGraph& graph, const ExecuteContext& context) {
    if (context.backBufferUsedAsRenderTarget) {
        *context.backBufferUsedAsRenderTarget = true;
    }

    PostProcessPass::DescriptorUpdateContext descriptorUpdate = context.descriptorUpdate;
    if (context.useBloomOverride && context.bloom.IsValid()) {
        ID3D12Resource* bloomOverride = graph.GetResource(context.bloom);
        if (!bloomOverride) {
            if (context.failBloomStage) {
                context.failBloomStage("bind_post_process_bloom");
            }
            return;
        }
        descriptorUpdate.bloomOverride = bloomOverride;
    }

    if (context.runRtReflectionDebugClear) {
        (void)RTReflectionDebugClearPass::ClearForDebugView(context.rtReflectionDebugClear);
    }

    if (!PostProcessPass::UpdateDescriptorTable(descriptorUpdate)) {
        if (context.failBloomStage) {
            context.failBloomStage("post_process_descriptor_table");
        }
        return;
    }

    if (!PostProcessPass::Draw(context.draw)) {
        if (context.failBloomStage) {
            context.failBloomStage("post_process_draw");
        }
        return;
    }

    if (context.ranPostProcess) {
        *context.ranPostProcess = true;
    }
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "post_process_graph_contract");
        return {};
    }

    graph.AddPass(
        "PostProcess",
        [context](RGPassBuilder& builder) {
            Declare(builder, context.resources);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            Execute(graph, context.execute);
        });

    return context.resources.backBuffer;
}

} // namespace Cortex::Graphics::PostProcessGraphPass
