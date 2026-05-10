#include "TAAPass.h"

#include "FullscreenPass.h"

namespace Cortex::Graphics::TAAPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    if (!context.hdr.IsValid() || !context.history.IsValid()) {
        return false;
    }
    if (context.seedOnly) {
        return static_cast<bool>(context.seedHistory);
    }
    return context.intermediate.IsValid() &&
           static_cast<bool>(context.resolve) &&
           static_cast<bool>(context.copyToHDR) &&
           static_cast<bool>(context.copyToHistory);
}

} // namespace

bool Resolve(const ResolveContext& context) {
    if (!context.commandList || !context.pipeline || !context.pipeline->GetPipelineState() ||
        !context.viewportSource || !context.targetRtv.IsValid() || context.srvTable.empty()) {
        return false;
    }

    context.commandList->OMSetRenderTargets(1, &context.targetRtv.cpu, FALSE, nullptr);
    FullscreenPass::SetViewportAndScissor(context.commandList, context.viewportSource);

    if (!FullscreenPass::BindGraphicsState({
            context.commandList,
            context.descriptorManager,
            context.rootSignature,
            context.frameConstants,
        })) {
        return false;
    }

    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->SetGraphicsRootDescriptorTable(3, context.srvTable[0].gpu);
    if (context.shadowAndEnvironmentTable.IsValid()) {
        context.commandList->SetGraphicsRootDescriptorTable(4, context.shadowAndEnvironmentTable.gpu);
    }

    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "taa_graph_contract");
        return {};
    }

    if (context.seedOnly) {
        graph.AddPass(
            "TAASeedHistory",
            [context](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Copy);
                builder.Read(context.hdr, RGResourceUsage::CopySrc);
                builder.Write(context.history, RGResourceUsage::CopyDst);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                if (!context.seedHistory || !context.seedHistory()) {
                    Fail(context, "seed_history");
                }
            });

        graph.AddPass(
            "TAASeedFinalize",
            [context](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.hdr, RGResourceUsage::ShaderResource);
                builder.Read(context.history, RGResourceUsage::ShaderResource);
            },
            [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

        return context.history;
    }

    graph.AddPass(
        "TAAResolve",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.hdr, RGResourceUsage::ShaderResource);
            builder.Read(context.history, RGResourceUsage::ShaderResource);
            if (context.velocity.IsValid()) {
                builder.Read(context.velocity, RGResourceUsage::ShaderResource);
            }
            if (context.depth.IsValid()) {
                builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            }
            if (context.normalRoughness.IsValid()) {
                builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource);
            }
            builder.Write(context.intermediate, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.resolve || !context.resolve()) {
                Fail(context, "resolve");
            }
        });

    graph.AddPass(
        "TAACopyToHDR",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Copy);
            builder.Read(context.intermediate, RGResourceUsage::CopySrc);
            builder.Write(context.hdr, RGResourceUsage::CopyDst);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.copyToHDR || !context.copyToHDR()) {
                Fail(context, "copy_to_hdr");
            }
        });

    graph.AddPass(
        "TAACopyToHistory",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Copy);
            builder.Read(context.hdr, RGResourceUsage::CopySrc);
            builder.Write(context.history, RGResourceUsage::CopyDst);
            builder.Write(context.intermediate, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            if (!context.copyToHistory || !context.copyToHistory()) {
                Fail(context, "copy_to_history");
            }
        });

    graph.AddPass(
        "TAAFinalize",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.hdr, RGResourceUsage::ShaderResource);
            builder.Read(context.history, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});

    return context.hdr;
}

} // namespace Cortex::Graphics::TAAPass
