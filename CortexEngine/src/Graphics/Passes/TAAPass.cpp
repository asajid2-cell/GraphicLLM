#include "TAAPass.h"

#include "DescriptorTable.h"
#include "FullscreenPass.h"

namespace Cortex::Graphics::TAAPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.status.failed) {
        *context.status.failed = true;
    }
    if (context.status.stage && !*context.status.stage) {
        *context.status.stage = stage ? stage : "unknown";
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    if (!context.hdr.IsValid() || !context.history.IsValid()) {
        return false;
    }
    if (context.seedOnly) {
        return context.seedHistory.commandList &&
               context.seedHistory.hdrColor.resource &&
               context.seedHistory.historyColor.resource;
    }
    return context.intermediate.IsValid() &&
           context.resolveInputs.commandList &&
           context.resolveDescriptors.device &&
           !context.resolveDescriptors.srvTable.empty() &&
           context.resolve.commandList &&
           context.copyToHDR.commandList &&
           context.copyToHistory.commandList;
}

} // namespace

bool UpdateResolveDescriptorTable(const DescriptorUpdateContext& context) {
    if (!context.device || context.srvTable.empty()) {
        return false;
    }

    auto writeOrNull = [&](size_t slot, ID3D12Resource* resource, DXGI_FORMAT fmt) {
        if (slot >= context.srvTable.size() || !context.srvTable[slot].IsValid()) {
            return;
        }
        DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[slot], nullptr, fmt);
        if (resource) {
            DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[slot], resource, fmt);
        }
    };

    // Must match PostProcess.hlsl TAAResolvePS bindings.
    writeOrNull(0, context.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT);

    ID3D12Resource* bloom = nullptr;
    if (context.bloomIntensity > 0.0f) {
        bloom = context.bloomOverride ? context.bloomOverride : context.bloomFallback;
    }
    writeOrNull(1, bloom, DXGI_FORMAT_R11G11B10_FLOAT);

    writeOrNull(2, context.ssao, DXGI_FORMAT_R8_UNORM);
    writeOrNull(3, context.history, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(4, context.depth, DXGI_FORMAT_R32_FLOAT);
    writeOrNull(5, context.normalRoughness, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(6, context.ssr, DXGI_FORMAT_R16G16B16A16_FLOAT);
    writeOrNull(7, context.velocity, DXGI_FORMAT_R16G16_FLOAT);
    writeOrNull(12, context.temporalMask, DXGI_FORMAT_R16G16B16A16_FLOAT);
    return true;
}

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
                if (!TAACopyPass::CopyHdrToHistory(context.seedHistory)) {
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
            if (!TAACopyPass::PrepareResolveInputs(context.resolveInputs) ||
                !UpdateResolveDescriptorTable(context.resolveDescriptors) ||
                !Resolve(context.resolve)) {
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
            if (!TAACopyPass::CopyIntermediateToHdr(context.copyToHDR)) {
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
            if (!TAACopyPass::CopyHdrToHistory(context.copyToHistory)) {
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
