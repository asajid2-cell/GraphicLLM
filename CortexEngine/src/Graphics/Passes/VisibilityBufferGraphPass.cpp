#include "VisibilityBufferGraphPass.h"

namespace Cortex::Graphics::VisibilityBufferGraphPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool HasBaseResources(const ResourceHandles& resources) {
    return resources.depth.IsValid() &&
           resources.hdr.IsValid() &&
           resources.visibility.IsValid();
}

[[nodiscard]] bool HasGBufferResources(const ResourceHandles& resources) {
    return resources.albedo.IsValid() &&
           resources.normalRoughness.IsValid() &&
           resources.emissiveMetallic.IsValid() &&
           resources.materialExt0.IsValid() &&
           resources.materialExt1.IsValid() &&
           resources.materialExt2.IsValid();
}

} // namespace

bool AddStagedPath(RenderGraph& graph, const GraphContext& context) {
    const ResourceHandles& resources = context.resources;
    if (!HasBaseResources(resources) || !context.clear || !context.visibility) {
        Fail(context, "visibility_buffer_graph_contract");
        return false;
    }
    if (!context.debugPath && (!HasGBufferResources(resources) || !context.deferredLighting)) {
        Fail(context, "visibility_buffer_deferred_contract");
        return false;
    }
    if (context.needsMaterialResolve && (!HasGBufferResources(resources) || !context.materialResolve)) {
        Fail(context, "visibility_buffer_material_resolve_contract");
        return false;
    }
    if (context.debugPath && !context.debugBlit) {
        Fail(context, "visibility_buffer_debug_contract");
        return false;
    }

    graph.AddPass(
        "VBClear",
        [resources](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(resources.visibility, RGResourceUsage::UnorderedAccess);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            context.clear();
        });

    graph.AddPass(
        "VBVisibility",
        [resources](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(resources.visibility, RGResourceUsage::RenderTarget);
            builder.Write(resources.depth, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            context.visibility();
        });

    if (context.needsMaterialResolve) {
        graph.AddPass(
            "VBMaterialResolve",
            [resources](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Compute);
                builder.Read(resources.visibility, RGResourceUsage::ShaderResource);
                builder.Read(resources.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
                builder.Write(resources.albedo, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.normalRoughness, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.emissiveMetallic, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.materialExt0, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.materialExt1, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.materialExt2, RGResourceUsage::UnorderedAccess);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                context.materialResolve();
            });
    }

    if (context.debugPath) {
        graph.AddPass(
            "VBDebugBlit",
            [context, resources](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                if (context.debugVisibility) {
                    builder.Read(resources.visibility, RGResourceUsage::ShaderResource);
                } else if (context.debugDepth) {
                    builder.Read(resources.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
                } else if (context.debugGBuffer && resources.debugSource.IsValid()) {
                    builder.Read(resources.debugSource, RGResourceUsage::ShaderResource);
                }
                builder.Write(resources.hdr, RGResourceUsage::RenderTarget);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                context.debugBlit();
            });
    }

    if (context.brdfGraphOwned) {
        graph.AddPass(
            "VBBRDFLUT",
            [resources](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Compute);
                builder.Write(resources.brdfLut, RGResourceUsage::UnorderedAccess);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                if (context.brdfLut) {
                    context.brdfLut();
                }
            });
    }

    if (!context.debugPath && context.clusterGraphOwned) {
        graph.AddPass(
            "VBClusteredLights",
            [resources](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Compute);
                builder.Write(resources.clusterRanges, RGResourceUsage::UnorderedAccess);
                builder.Write(resources.clusterLightIndices, RGResourceUsage::UnorderedAccess);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                if (context.clusteredLights) {
                    context.clusteredLights();
                }
            });
    }

    if (!context.debugPath) {
        graph.AddPass(
            "VBDeferredLighting",
            [context, resources](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Graphics);
                builder.Read(resources.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
                builder.Read(resources.albedo, RGResourceUsage::ShaderResource);
                builder.Read(resources.normalRoughness, RGResourceUsage::ShaderResource);
                builder.Read(resources.emissiveMetallic, RGResourceUsage::ShaderResource);
                builder.Read(resources.materialExt0, RGResourceUsage::ShaderResource);
                builder.Read(resources.materialExt1, RGResourceUsage::ShaderResource);
                builder.Read(resources.materialExt2, RGResourceUsage::ShaderResource);
                if (resources.brdfLut.IsValid()) builder.Read(resources.brdfLut, RGResourceUsage::ShaderResource);
                if (context.clusterGraphOwned) {
                    builder.Read(resources.clusterRanges, RGResourceUsage::ShaderResource);
                    builder.Read(resources.clusterLightIndices, RGResourceUsage::ShaderResource);
                }
                if (resources.shadow.IsValid()) builder.Read(resources.shadow, RGResourceUsage::ShaderResource);
                if (resources.rtShadow.IsValid()) builder.Read(resources.rtShadow, RGResourceUsage::ShaderResource);
                if (resources.rtGI.IsValid()) builder.Read(resources.rtGI, RGResourceUsage::ShaderResource);
                builder.Write(resources.hdr, RGResourceUsage::RenderTarget);
            },
            [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
                context.deferredLighting();
            });
    }

    return true;
}

bool AddLegacyPath(RenderGraph& graph, const LegacyPathContext& context) {
    if (!context.depth.IsValid() || !context.hdr.IsValid() || !context.execute) {
        if (context.failStage) {
            context.failStage("visibility_buffer_legacy_graph_contract");
        }
        return false;
    }

    graph.AddPass(
        "VisibilityBufferPath",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.depth, RGResourceUsage::DepthStencilWrite);
            builder.Write(context.hdr, RGResourceUsage::RenderTarget);
            if (context.shadow.IsValid()) builder.Read(context.shadow, RGResourceUsage::ShaderResource);
            if (context.rtShadow.IsValid()) builder.Read(context.rtShadow, RGResourceUsage::ShaderResource);
            if (context.rtGI.IsValid()) builder.Read(context.rtGI, RGResourceUsage::ShaderResource);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            context.execute();
        });

    return true;
}

} // namespace Cortex::Graphics::VisibilityBufferGraphPass
