#include "BloomGraphPass.h"

#include <algorithm>
#include <string>

namespace Cortex::Graphics::BloomGraphPass {

namespace {

[[nodiscard]] bool IsUsable(const FusedBloomContext& context) {
    return context.hdr.IsValid() &&
           !context.bloomA.empty() &&
           !context.bloomB.empty() &&
           context.bloomATemplates.size() >= context.activeLevels &&
           context.bloomBTemplates.size() >= context.activeLevels &&
           context.graphRtv &&
           context.downsamplePipeline &&
           context.blurHPipeline &&
           context.blurVPipeline &&
           context.compositePipeline &&
           context.activeLevels > 0 &&
           context.stageLevels > 0 &&
           context.activeLevels <= context.bloomA.size() &&
           context.activeLevels <= context.bloomB.size() &&
           context.baseLevel < context.activeLevels;
}

[[nodiscard]] bool IsUsable(const StandaloneBloomContext& context) {
    return context.hdr.IsValid() &&
           !context.bloomA.empty() &&
           !context.bloomB.empty() &&
           (!context.useTransients ||
            (context.bloomATemplates.size() >= context.activeLevels &&
             context.bloomBTemplates.size() >= context.activeLevels)) &&
           context.targetRtv &&
           context.downsamplePipeline &&
           context.blurHPipeline &&
           context.blurVPipeline &&
           context.compositePipeline &&
           context.activeLevels > 0 &&
           context.stageLevels > 0 &&
           context.activeLevels <= context.bloomA.size() &&
           context.activeLevels <= context.bloomB.size() &&
           context.baseLevel < context.activeLevels;
}

void Fail(const FusedBloomContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

void Fail(const StandaloneBloomContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

void MarkHdrShaderResource(const FusedBloomContext& context) {
    if (context.hdrResourceState) {
        *context.hdrResourceState = context.hdrShaderResourceState;
    }
}

void MarkHdrShaderResource(const StandaloneBloomContext& context) {
    if (context.hdrResourceState) {
        *context.hdrResourceState = context.hdrShaderResourceState;
    }
}

void MarkBloomRan(const FusedBloomContext& context) {
    if (context.bloomRan) {
        const bool failed = context.bloomStageFailed && *context.bloomStageFailed;
        *context.bloomRan = !failed;
    }
}

void DeclareTransients(RGPassBuilder& builder, const FusedBloomContext& context) {
    if (!context.useTransients) {
        return;
    }

    for (uint32_t level = 0; level < context.activeLevels; ++level) {
        if (!context.bloomA[level].IsValid() && context.bloomATemplates[level]) {
            context.bloomA[level] = builder.CreateTransient(
                BloomPass::MakeTextureDesc(
                    context.bloomATemplates[level],
                    "BloomA_FusedTransient" + std::to_string(level)));
        }
        if (!context.bloomB[level].IsValid() && context.bloomBTemplates[level]) {
            context.bloomB[level] = builder.CreateTransient(
                BloomPass::MakeTextureDesc(
                    context.bloomBTemplates[level],
                    "BloomB_FusedTransient" + std::to_string(level)));
        }
    }
}

void DeclareTransients(RGPassBuilder& builder, const StandaloneBloomContext& context) {
    if (!context.useTransients) {
        return;
    }

    for (uint32_t level = 0; level < context.activeLevels; ++level) {
        if (!context.bloomA[level].IsValid() && context.bloomATemplates[level]) {
            context.bloomA[level] = builder.CreateTransient(
                BloomPass::MakeTextureDesc(
                    context.bloomATemplates[level],
                    "BloomA_Transient" + std::to_string(level)));
        }
        if (!context.bloomB[level].IsValid() && context.bloomBTemplates[level]) {
            context.bloomB[level] = builder.CreateTransient(
                BloomPass::MakeTextureDesc(
                    context.bloomBTemplates[level],
                    "BloomB_Transient" + std::to_string(level)));
        }
    }
}

} // namespace

RGResourceHandle AddFusedBloom(RenderGraph& graph, const FusedBloomContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "fused_bloom_contract");
        return {};
    }

    graph.AddPass(
        "BloomDownsample0",
        [context](RGPassBuilder& builder) {
            DeclareTransients(builder, context);
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.hdr, RGResourceUsage::ShaderResource);
            builder.Write(context.bloomA[0], RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            MarkHdrShaderResource(context);
            if (!BloomPass::RenderFullscreen(context.fullscreen,
                                             graph.GetResource(context.bloomA[0]),
                                             context.downsamplePipeline,
                                             graph.GetResource(context.hdr),
                                             BloomPass::BaseDownsampleSlot(),
                                             "downsample hdr",
                                             context.graphRtv[0][0])) {
                Fail(context, "downsample_base");
            }
        });

    for (uint32_t level = 1; level < context.activeLevels; ++level) {
        const uint32_t passLevel = level;
        graph.AddPass(
            "BloomDownsample" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomA[passLevel - 1u], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomA[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 context.downsamplePipeline,
                                                 graph.GetResource(context.bloomA[passLevel - 1u]),
                                                 BloomPass::DownsampleChainSlot(passLevel),
                                                 "downsample chain",
                                                 context.graphRtv[passLevel][0])) {
                    Fail(context, "downsample_chain");
                }
            });
    }

    for (uint32_t level = 0; level < context.activeLevels; ++level) {
        const uint32_t passLevel = level;
        graph.AddPass(
            "BloomBlurH" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomA[passLevel], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomB[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomB[passLevel]),
                                                 context.blurHPipeline,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 BloomPass::BlurHSlot(passLevel, context.stageLevels),
                                                 "blur horizontal",
                                                 context.graphRtv[passLevel][1])) {
                    Fail(context, "blur_horizontal");
                }
            });

        graph.AddPass(
            "BloomBlurV" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomB[passLevel], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomA[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 context.blurVPipeline,
                                                 graph.GetResource(context.bloomB[passLevel]),
                                                 BloomPass::BlurVSlot(passLevel, context.stageLevels),
                                                 "blur vertical",
                                                 context.graphRtv[passLevel][0])) {
                    Fail(context, "blur_vertical");
                }
            });
    }

    const uint32_t compositeBaseLevel = context.baseLevel;
    graph.AddPass(
        "BloomComposite",
        [context, compositeBaseLevel](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            for (uint32_t level = 0; level < context.activeLevels; ++level) {
                builder.Read(context.bloomA[level], RGResourceUsage::ShaderResource);
            }
            builder.Write(context.bloomB[compositeBaseLevel], RGResourceUsage::RenderTarget);
        },
        [context, compositeBaseLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            if (!BloomPass::RenderComposite(context.fullscreen,
                                            graph,
                                            context.bloomB[compositeBaseLevel],
                                            std::span<const RGResourceHandle>(context.bloomA.data(), context.activeLevels),
                                            context.activeLevels,
                                            context.stageLevels,
                                            context.compositePipeline,
                                            context.graphRtv[compositeBaseLevel][1])) {
                Fail(context, "composite");
                return;
            }
            MarkBloomRan(context);
        });

    return context.bloomB[context.baseLevel];
}

RGResourceHandle AddStandaloneBloom(RenderGraph& graph, const StandaloneBloomContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "standalone_bloom_contract");
        return {};
    }

    graph.AddPass(
        "BloomDownsample0",
        [context](RGPassBuilder& builder) {
            DeclareTransients(builder, context);
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.hdr, RGResourceUsage::ShaderResource);
            builder.Write(context.bloomA[0], RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            MarkHdrShaderResource(context);
            if (!BloomPass::RenderFullscreen(context.fullscreen,
                                             graph.GetResource(context.bloomA[0]),
                                             context.downsamplePipeline,
                                             graph.GetResource(context.hdr),
                                             BloomPass::BaseDownsampleSlot(),
                                             "downsample hdr",
                                             context.targetRtv[0][0])) {
                Fail(context, "downsample_base");
            }
        });

    for (uint32_t level = 1; level < context.activeLevels; ++level) {
        const uint32_t passLevel = level;
        if (!context.bloomA[passLevel].IsValid() || !context.bloomA[passLevel - 1u].IsValid()) {
            continue;
        }
        graph.AddPass(
            "BloomDownsample" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomA[passLevel - 1u], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomA[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 context.downsamplePipeline,
                                                 graph.GetResource(context.bloomA[passLevel - 1u]),
                                                 BloomPass::DownsampleChainSlot(passLevel),
                                                 "downsample chain",
                                                 context.targetRtv[passLevel][0])) {
                    Fail(context, "downsample_chain");
                }
            });
    }

    for (uint32_t level = 0; level < context.activeLevels; ++level) {
        const uint32_t passLevel = level;
        if (!context.bloomA[passLevel].IsValid() || !context.bloomB[passLevel].IsValid()) {
            continue;
        }

        graph.AddPass(
            "BloomBlurH" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomA[passLevel], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomB[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomB[passLevel]),
                                                 context.blurHPipeline,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 BloomPass::BlurHSlot(passLevel, context.stageLevels),
                                                 "blur horizontal",
                                                 context.targetRtv[passLevel][1])) {
                    Fail(context, "blur_horizontal");
                }
            });

        graph.AddPass(
            "BloomBlurV" + std::to_string(passLevel),
            [context, passLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomB[passLevel], RGResourceUsage::ShaderResource);
                builder.Write(context.bloomA[passLevel], RGResourceUsage::RenderTarget);
            },
            [context, passLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderFullscreen(context.fullscreen,
                                                 graph.GetResource(context.bloomA[passLevel]),
                                                 context.blurVPipeline,
                                                 graph.GetResource(context.bloomB[passLevel]),
                                                 BloomPass::BlurVSlot(passLevel, context.stageLevels),
                                                 "blur vertical",
                                                 context.targetRtv[passLevel][0])) {
                    Fail(context, "blur_vertical");
                }
            });
    }

    if (context.bloomB[context.baseLevel].IsValid()) {
        const uint32_t compositeBaseLevel = context.baseLevel;
        graph.AddPass(
            "BloomComposite",
            [context, compositeBaseLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                for (uint32_t level = 0; level < context.activeLevels; ++level) {
                    if (context.bloomA[level].IsValid()) {
                        builder.Read(context.bloomA[level], RGResourceUsage::ShaderResource);
                    }
                }
                builder.Write(context.bloomB[compositeBaseLevel], RGResourceUsage::RenderTarget);
            },
            [context, compositeBaseLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                if (!BloomPass::RenderComposite(context.fullscreen,
                                                graph,
                                                context.bloomB[compositeBaseLevel],
                                                std::span<const RGResourceHandle>(context.bloomA.data(), context.activeLevels),
                                                context.activeLevels,
                                                context.stageLevels,
                                                context.compositePipeline,
                                                context.targetRtv[compositeBaseLevel][1])) {
                    Fail(context, "composite");
                }
            });
    }

    if (context.bloomA[context.baseLevel].IsValid() && context.bloomB[context.baseLevel].IsValid()) {
        const uint32_t combinedLevel = context.baseLevel;
        graph.AddPass(
            "BloomCopyCombined",
            [context, combinedLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Copy);
                builder.Read(context.bloomB[combinedLevel], RGResourceUsage::CopySrc);
                builder.Write(context.bloomA[combinedLevel], RGResourceUsage::CopyDst);
            },
            [context, combinedLevel](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
                D3D12_RESOURCE_STATES sourceState = D3D12_RESOURCE_STATE_COPY_SOURCE;
                D3D12_RESOURCE_STATES targetState = D3D12_RESOURCE_STATE_COPY_DEST;
                if (!BloomPass::CopyCompositeToCombined({
                        context.fullscreen.commandList,
                        {graph.GetResource(context.bloomB[combinedLevel]), &sourceState},
                        {graph.GetResource(context.bloomA[combinedLevel]), &targetState},
                        true,
                    })) {
                    Fail(context, "copy_combined");
                }
            });

        graph.AddPass(
            "BloomFinalize",
            [context, combinedLevel](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(context.bloomA[combinedLevel], RGResourceUsage::ShaderResource);
            },
            [](ID3D12GraphicsCommandList*, const RenderGraph&) {});
    }

    return context.bloomA[context.baseLevel];
}

} // namespace Cortex::Graphics::BloomGraphPass
