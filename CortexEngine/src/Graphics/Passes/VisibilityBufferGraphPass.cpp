#include "VisibilityBufferGraphPass.h"

namespace Cortex::Graphics::VisibilityBufferGraphPass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.failStage) {
        context.failStage(stage);
    }
}

[[nodiscard]] bool HasFailed(const StageFailureContext& failure) {
    return failure.failed && *failure.failed;
}

void RecordFailure(const StageFailureContext& failure, const char* stage, const std::string& error) {
    if (failure.failed && !*failure.failed) {
        if (failure.stage) {
            *failure.stage = stage ? stage : "visibility_buffer_stage";
        }
        if (failure.error) {
            *failure.error = error;
        }
    }
    if (failure.failed) {
        *failure.failed = true;
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

[[nodiscard]] bool IsValid(const ClearContext& context) {
    return context.renderer && context.commandList;
}

[[nodiscard]] bool IsValid(const VisibilityContext& context) {
    return context.renderer &&
           context.commandList &&
           context.depthBuffer &&
           context.viewProjection &&
           context.meshDraws;
}

} // namespace

bool Clear(const ClearContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "clear", "visibility-buffer clear context incomplete");
        return false;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    states.visibility = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.visibilityPass = true;
    context.renderer->SetTransitionSkipControls(controls);
    auto clearResult = context.renderer->ClearVisibilityBuffer(context.commandList);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (clearResult.IsErr()) {
        RecordFailure(context.failure, "clear", clearResult.Error());
        return false;
    }
    return true;
}

bool RasterizeVisibility(const VisibilityContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "visibility", "visibility-buffer raster context incomplete");
        return false;
    }

    if (context.depthState) {
        *context.depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    auto states = context.renderer->GetResourceStateSnapshot();
    states.visibility = D3D12_RESOURCE_STATE_RENDER_TARGET;
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.visibilityPass = true;
    context.renderer->SetTransitionSkipControls(controls);
    auto visResult = context.renderer->RasterizeVisibilityBuffer(
        context.commandList,
        context.depthBuffer,
        context.depthDSV,
        *context.viewProjection,
        *context.meshDraws,
        context.cullMaskAddress);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (visResult.IsErr()) {
        RecordFailure(context.failure, "visibility", visResult.Error());
        return false;
    }

    uint32_t vbDrawBatches = 0;
    for (const auto& draw : *context.meshDraws) {
        vbDrawBatches += (draw.instanceCount > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountDoubleSided > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlpha > 0) ? 1u : 0u;
        vbDrawBatches += (draw.instanceCountAlphaDoubleSided > 0) ? 1u : 0u;
    }

    if (context.contractInstances) {
        *context.contractInstances = context.instanceCount;
    }
    if (context.contractMeshes) {
        *context.contractMeshes = static_cast<uint32_t>(context.meshDraws->size());
    }
    if (context.contractDrawBatches) {
        *context.contractDrawBatches = vbDrawBatches;
    }
    return true;
}

bool AddStagedPath(RenderGraph& graph, const GraphContext& context) {
    const ResourceHandles& resources = context.resources;
    if (!HasBaseResources(resources) || !IsValid(context.clear) || !IsValid(context.visibility)) {
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
            (void)Clear(context.clear);
        });

    graph.AddPass(
        "VBVisibility",
        [resources](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(resources.visibility, RGResourceUsage::RenderTarget);
            builder.Write(resources.depth, RGResourceUsage::DepthStencilWrite);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            (void)RasterizeVisibility(context.visibility);
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

} // namespace Cortex::Graphics::VisibilityBufferGraphPass
