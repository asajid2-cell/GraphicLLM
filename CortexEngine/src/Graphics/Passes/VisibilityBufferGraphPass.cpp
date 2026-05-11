#include "VisibilityBufferGraphPass.h"

namespace Cortex::Graphics::VisibilityBufferGraphPass {

namespace {

inline constexpr D3D12_RESOURCE_STATES kVBShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
inline constexpr D3D12_RESOURCE_STATES kDepthSampleState =
    D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

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

[[nodiscard]] bool IsValid(const MaterialResolveContext& context) {
    return context.renderer &&
           context.commandList &&
           context.depthBuffer &&
           context.viewProjection &&
           context.meshDraws;
}

[[nodiscard]] bool IsValid(const DebugBlitContext& context) {
    return context.renderer &&
           context.commandList &&
           context.hdrTarget &&
           (context.debugVisibility || (context.debugDepth && context.depthBuffer) || context.debugGBuffer);
}

[[nodiscard]] bool IsValid(const BRDFLUTContext& context) {
    return context.renderer && context.commandList;
}

[[nodiscard]] bool IsValid(const ClusteredLightsContext& context) {
    return context.renderer && context.commandList;
}

[[nodiscard]] bool IsValid(const DeferredLightingContext& context) {
    return context.renderer &&
           context.commandList &&
           context.hdrTarget &&
           context.depthBuffer;
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

bool ResolveMaterials(const MaterialResolveContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "material_resolve", "visibility-buffer material resolve context incomplete");
        return false;
    }

    if (context.depthState) {
        *context.depthState = kDepthSampleState;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    states.visibility = kVBShaderResourceState;
    states.albedo = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.normalRoughness = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.emissiveMetallic = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.materialExt0 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.materialExt1 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.materialExt2 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.materialResolve = true;
    context.renderer->SetTransitionSkipControls(controls);
    auto resolveResult = context.renderer->ResolveMaterials(
        context.commandList,
        context.depthBuffer,
        context.depthSRV,
        *context.meshDraws,
        *context.viewProjection,
        context.biomeMaterialsAddress);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (resolveResult.IsErr()) {
        RecordFailure(context.failure, "material_resolve", resolveResult.Error());
        return false;
    }
    return true;
}

bool DebugBlit(const DebugBlitContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "debug_blit", "visibility-buffer debug blit context incomplete");
        return false;
    }

    if (context.hdrState) {
        *context.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    if (context.debugVisibility) {
        states.visibility = kVBShaderResourceState;
    } else if (context.debugGBuffer) {
        switch (context.gbufferSource) {
            case VisibilityBufferRenderer::DebugBlitBuffer::NormalRoughness:
                states.normalRoughness = kVBShaderResourceState;
                break;
            case VisibilityBufferRenderer::DebugBlitBuffer::EmissiveMetallic:
                states.emissiveMetallic = kVBShaderResourceState;
                break;
            case VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt0:
                states.materialExt0 = kVBShaderResourceState;
                break;
            case VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt1:
                states.materialExt1 = kVBShaderResourceState;
                break;
            case VisibilityBufferRenderer::DebugBlitBuffer::MaterialExt2:
                states.materialExt2 = kVBShaderResourceState;
                break;
            case VisibilityBufferRenderer::DebugBlitBuffer::Albedo:
            default:
                states.albedo = kVBShaderResourceState;
                break;
        }
    }
    context.renderer->ApplyResourceStateSnapshot(states);

    if (context.debugDepth && context.depthState) {
        *context.depthState = kDepthSampleState;
    }

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.debugBlit = true;
    context.renderer->SetTransitionSkipControls(controls);

    Result<void> debugResult = Result<void>::Ok();
    if (context.debugVisibility) {
        debugResult = context.renderer->DebugBlitVisibilityToHDR(
            context.commandList,
            context.hdrTarget,
            context.hdrRTV);
    } else if (context.debugDepth) {
        debugResult = context.renderer->DebugBlitDepthToHDR(
            context.commandList,
            context.hdrTarget,
            context.hdrRTV,
            context.depthBuffer);
    } else if (context.debugGBuffer) {
        debugResult = context.renderer->DebugBlitGBufferToHDR(
            context.commandList,
            context.hdrTarget,
            context.hdrRTV,
            context.gbufferSource);
    }

    context.renderer->SetTransitionSkipControls(previousControls);
    if (debugResult.IsErr()) {
        RecordFailure(context.failure, "debug_blit", debugResult.Error());
        return false;
    }

    if (context.renderedThisFrame) {
        *context.renderedThisFrame = true;
    }
    if (context.debugOverrideThisFrame) {
        *context.debugOverrideThisFrame = true;
    }
    return true;
}

bool GenerateBRDFLUT(const BRDFLUTContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "brdf_lut", "visibility-buffer BRDF LUT context incomplete");
        return false;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    states.brdfLut = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.brdfLut = true;
    context.renderer->SetTransitionSkipControls(controls);
    auto brdfResult = context.renderer->EnsureBRDFLUT(context.commandList);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (brdfResult.IsErr()) {
        RecordFailure(context.failure, "brdf_lut", brdfResult.Error());
        return false;
    }
    return true;
}

bool BuildClusteredLights(const ClusteredLightsContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "clustered_lights", "visibility-buffer clustered-lights context incomplete");
        return false;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    states.clusterRanges = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    states.clusterLightIndices = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.clusteredLights = true;
    context.renderer->SetTransitionSkipControls(controls);
    auto clusterResult = context.renderer->BuildClusteredLightLists(context.commandList, context.params);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (clusterResult.IsErr()) {
        RecordFailure(context.failure, "clustered_lights", clusterResult.Error());
        return false;
    }
    return true;
}

bool ApplyDeferredLighting(const DeferredLightingContext& context) {
    if (HasFailed(context.failure)) {
        return false;
    }
    if (!IsValid(context)) {
        RecordFailure(context.failure, "deferred_lighting", "visibility-buffer deferred-lighting context incomplete");
        return false;
    }

    if (context.depthState) {
        *context.depthState = kDepthSampleState;
    }
    if (context.hdrState) {
        *context.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    if (context.shadowValid && context.shadowState) {
        *context.shadowState = kVBShaderResourceState;
    }
    if (context.rtShadowValid && context.rtShadowState) {
        *context.rtShadowState = kVBShaderResourceState;
    }
    if (context.rtGIValid && context.rtGIState) {
        *context.rtGIState = kVBShaderResourceState;
    }

    auto states = context.renderer->GetResourceStateSnapshot();
    states.albedo = kVBShaderResourceState;
    states.normalRoughness = kVBShaderResourceState;
    states.emissiveMetallic = kVBShaderResourceState;
    states.materialExt0 = kVBShaderResourceState;
    states.materialExt1 = kVBShaderResourceState;
    states.materialExt2 = kVBShaderResourceState;
    if (context.brdfLutValid) {
        states.brdfLut = kVBShaderResourceState;
    }
    if (context.clusterGraphOwned) {
        states.clusterRanges = kVBShaderResourceState;
        states.clusterLightIndices = kVBShaderResourceState;
    }
    context.renderer->ApplyResourceStateSnapshot(states);

    auto controls = context.renderer->GetTransitionSkipControls();
    const auto previousControls = controls;
    controls.deferredLighting = true;
    if (context.clusterGraphOwned) {
        controls.clusteredLights = true;
    }
    context.renderer->SetTransitionSkipControls(controls);
    auto lightingResult = context.renderer->ApplyDeferredLighting(
        context.commandList,
        context.hdrTarget,
        context.hdrRTV,
        context.depthBuffer,
        context.depthSRV,
        context.envDiffuseResource,
        context.envSpecularResource,
        context.envFormat,
        context.shadowMapSRV,
        context.params);
    context.renderer->SetTransitionSkipControls(previousControls);
    if (lightingResult.IsErr()) {
        RecordFailure(context.failure, "deferred_lighting", lightingResult.Error());
        return false;
    }

    if (context.renderedThisFrame) {
        *context.renderedThisFrame = true;
    }
    return true;
}

bool AddStagedPath(RenderGraph& graph, const GraphContext& context) {
    const ResourceHandles& resources = context.resources;
    if (!HasBaseResources(resources) || !IsValid(context.clear) || !IsValid(context.visibility)) {
        Fail(context, "visibility_buffer_graph_contract");
        return false;
    }
    if (!context.debugPath && (!HasGBufferResources(resources) || !IsValid(context.deferredLighting))) {
        Fail(context, "visibility_buffer_deferred_contract");
        return false;
    }
    if (context.needsMaterialResolve && (!HasGBufferResources(resources) || !IsValid(context.materialResolve))) {
        Fail(context, "visibility_buffer_material_resolve_contract");
        return false;
    }
    if (context.debugPath && !IsValid(context.debugBlit)) {
        Fail(context, "visibility_buffer_debug_contract");
        return false;
    }
    if (context.brdfGraphOwned && !IsValid(context.brdfLut)) {
        Fail(context, "visibility_buffer_brdf_lut_contract");
        return false;
    }
    if (!context.debugPath && context.clusterGraphOwned && !IsValid(context.clusteredLights)) {
        Fail(context, "visibility_buffer_clustered_lights_contract");
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
                (void)ResolveMaterials(context.materialResolve);
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
                (void)DebugBlit(context.debugBlit);
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
                (void)GenerateBRDFLUT(context.brdfLut);
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
                (void)BuildClusteredLights(context.clusteredLights);
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
                (void)ApplyDeferredLighting(context.deferredLighting);
            });
    }

    return true;
}

} // namespace Cortex::Graphics::VisibilityBufferGraphPass
