#include "Graphics/Passes/FullSceneShaderV3Passes.h"

#include "Graphics/Passes/DescriptorTable.h"
#include "Graphics/Passes/FullscreenPass.h"

namespace Cortex::Graphics::FullSceneShaderV3Passes {

namespace {

void FailFullSceneCompositeV3(const FullSceneCompositeV3Context& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "full_scene_composite_v3_unknown";
    }
}

void FailSceneLocalEnvironmentV3(const SceneLocalEnvironmentV3Context& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "scene_local_environment_v3_unknown";
    }
}

void FailFullSceneReflectionResolverV3(const FullSceneReflectionResolverV3Context& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "full_scene_reflection_resolver_v3_unknown";
    }
}

void FailFullSceneReflectionHistoryV3(const FullSceneReflectionHistoryV3Context& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "full_scene_reflection_history_v3_unknown";
    }
}

void FailFullSceneReflectionHistoryV3Copy(const FullSceneReflectionHistoryV3CopyContext& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "full_scene_reflection_history_v3_copy_unknown";
    }
}

void FailCandidateBeautyDisplay(const CandidateBeautyDisplayContext& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "candidate_beauty_display_unknown";
    }
}

} // namespace

bool AddSceneLocalEnvironmentV3Pass(RenderGraph& graph,
                                    const SceneLocalEnvironmentV3Context& context) {
    if (!context.sceneLocalEnvironment.IsValid() ||
        !context.ambientLighting.IsValid() ||
        !context.visibleBackground.IsValid() ||
        !context.reflectionBackground.IsValid() ||
        !context.atmosphere.IsValid() ||
        !context.device ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        !context.descriptorManager ||
        context.frameConstants == 0 ||
        context.width == 0 ||
        context.height == 0) {
        FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_contract");
        return false;
    }
    for (const auto rtv : context.outputRTVs) {
        if (rtv.ptr == 0) {
            FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_rtv");
            return false;
        }
    }

    graph.AddPass(
        "SceneLocalEnvironmentV3",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Write(context.sceneLocalEnvironment, RGResourceUsage::RenderTarget);
            builder.Write(context.ambientLighting, RGResourceUsage::RenderTarget);
            builder.Write(context.visibleBackground, RGResourceUsage::RenderTarget);
            builder.Write(context.reflectionBackground, RGResourceUsage::RenderTarget);
            builder.Write(context.atmosphere, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            auto payloadTableResult = context.descriptorManager->AllocateTransientCBV_SRV_UAVRange(5);
            if (payloadTableResult.IsErr()) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_payload_descriptor");
                return;
            }
            const DescriptorHandle payloadAlbedoSRV = payloadTableResult.Value();
            const DescriptorHandle payloadNormalSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(payloadAlbedoSRV.index + 1u);
            const DescriptorHandle irradianceProxySRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(payloadAlbedoSRV.index + 2u);
            const DescriptorHandle specularProxySRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(payloadAlbedoSRV.index + 3u);
            const DescriptorHandle visibleBackgroundProxySRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(payloadAlbedoSRV.index + 4u);
            if (!payloadAlbedoSRV.IsValid() ||
                !payloadNormalSRV.IsValid() ||
                !irradianceProxySRV.IsValid() ||
                !specularProxySRV.IsValid() ||
                !visibleBackgroundProxySRV.IsValid()) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_payload_descriptor_slot");
                return;
            }

            auto writeTextureSRV = [&context](const DescriptorHandle& handle,
                                              const std::shared_ptr<DX12Texture>& texture) {
                return DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    handle,
                    texture && texture->GetResource() ? texture->GetResource() : nullptr,
                    texture ? texture->GetFormat() : DXGI_FORMAT_R8G8B8A8_UNORM,
                    texture ? texture->GetMipLevels() : 1u);
            };

            if (!writeTextureSRV(payloadAlbedoSRV, context.payloadAlbedo)) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_payload_albedo_srv");
                return;
            }
            if (!writeTextureSRV(payloadNormalSRV, context.payloadNormal)) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_payload_normal_srv");
                return;
            }
            if (!writeTextureSRV(irradianceProxySRV, context.irradianceProxy)) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_irradiance_proxy_srv");
                return;
            }
            if (!writeTextureSRV(specularProxySRV, context.specularProxy)) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_specular_proxy_srv");
                return;
            }
            if (!writeTextureSRV(visibleBackgroundProxySRV, context.visibleBackgroundProxy)) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_visible_background_proxy_srv");
                return;
            }

            context.commandList->OMSetRenderTargets(
                static_cast<UINT>(context.outputRTVs.size()),
                context.outputRTVs.data(),
                FALSE,
                nullptr);
            FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);
            if (!FullscreenPass::BindGraphicsState({
                    context.commandList,
                    context.descriptorManager,
                    context.rootSignature,
                    context.frameConstants,
                })) {
                FailSceneLocalEnvironmentV3(context, "scene_local_environment_v3_bind");
                return;
            }
            context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
            context.commandList->SetGraphicsRootDescriptorTable(3, payloadAlbedoSRV.gpu);
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

bool AddFullSceneCompositeV3Pass(RenderGraph& graph,
                                 const FullSceneCompositeV3Context& context) {
    if (!context.directLighting.IsValid() ||
        !context.indirectLighting.IsValid() ||
        !context.shadowVisibility.IsValid() ||
        !context.legacyHdr.IsValid() ||
        !context.materialAlbedo.IsValid() ||
        !context.sceneLocalEnvironment.IsValid() ||
        !context.output.IsValid() ||
        !context.energyClampPolicy.IsValid() ||
        !context.overbrightDiagnostics.IsValid() ||
        !context.compositeContributionMap.IsValid() ||
        !context.legacyRescueUsage.IsValid() ||
        !context.device ||
        !context.descriptorManager ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        context.frameConstants == 0 ||
        !context.directLightingSRV.IsValid() ||
        !context.indirectLightingSRV.IsValid() ||
        !context.shadowVisibilitySRV.IsValid() ||
        !context.legacyHdrSRV.IsValid() ||
        !context.sceneLocalEnvironmentSRV.IsValid() ||
        context.width == 0 ||
        context.height == 0) {
        FailFullSceneCompositeV3(context, "full_scene_composite_v3_contract");
        return false;
    }
    for (const auto rtv : context.outputRTVs) {
        if (rtv.ptr == 0) {
            FailFullSceneCompositeV3(context, "full_scene_composite_v3_rtv");
            return false;
        }
    }

    graph.AddPass(
        "FullSceneCompositeV3",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.directLighting, RGResourceUsage::ShaderResource);
            builder.Read(context.indirectLighting, RGResourceUsage::ShaderResource);
            builder.Read(context.shadowVisibility, RGResourceUsage::ShaderResource);
            builder.Read(context.legacyHdr, RGResourceUsage::ShaderResource);
            if (context.localReflectionRadiance.IsValid()) {
                builder.Read(context.localReflectionRadiance, RGResourceUsage::ShaderResource);
            }
            if (context.reflectionConfidence.IsValid()) {
                builder.Read(context.reflectionConfidence, RGResourceUsage::ShaderResource);
            }
            if (context.materialAlbedo.IsValid()) {
                builder.Read(context.materialAlbedo, RGResourceUsage::ShaderResource);
            }
            builder.Read(context.sceneLocalEnvironment, RGResourceUsage::ShaderResource);
            builder.Write(context.output, RGResourceUsage::RenderTarget);
            builder.Write(context.energyClampPolicy, RGResourceUsage::RenderTarget);
            builder.Write(context.overbrightDiagnostics, RGResourceUsage::RenderTarget);
            builder.Write(context.compositeContributionMap, RGResourceUsage::RenderTarget);
            builder.Write(context.legacyRescueUsage, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            auto tableResult = context.descriptorManager->AllocateTransientCBV_SRV_UAVRange(8);
            if (tableResult.IsErr()) {
                FailFullSceneCompositeV3(context, "full_scene_composite_v3_descriptor");
                return;
            }

            const DescriptorHandle base = tableResult.Value();
            const DescriptorHandle table[8] = {
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 0u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 1u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 2u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 3u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 4u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 5u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 6u),
                context.descriptorManager->GetCBV_SRV_UAVHandle(base.index + 7u),
            };
            for (const DescriptorHandle& handle : table) {
                if (!handle.IsValid()) {
                    FailFullSceneCompositeV3(context, "full_scene_composite_v3_descriptor_slot");
                    return;
                }
            }

            context.device->CopyDescriptorsSimple(1, table[0].cpu, context.directLightingSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context.device->CopyDescriptorsSimple(1, table[1].cpu, context.indirectLightingSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context.device->CopyDescriptorsSimple(1, table[2].cpu, context.shadowVisibilitySRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context.device->CopyDescriptorsSimple(1, table[3].cpu, context.legacyHdrSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context.device->CopyDescriptorsSimple(
                1,
                table[7].cpu,
                context.sceneLocalEnvironmentSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Resource* reflectionRadiance = context.localReflectionRadiance.IsValid()
                ? graph.GetResource(context.localReflectionRadiance)
                : nullptr;
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    table[4],
                    reflectionRadiance,
                    DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                FailFullSceneCompositeV3(context, "full_scene_composite_v3_reflection_srv");
                return;
            }
            ID3D12Resource* reflectionConfidence = context.reflectionConfidence.IsValid()
                ? graph.GetResource(context.reflectionConfidence)
                : nullptr;
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    table[5],
                    reflectionConfidence,
                    DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                FailFullSceneCompositeV3(context, "full_scene_composite_v3_reflection_confidence_srv");
                return;
            }
            ID3D12Resource* materialAlbedo = context.materialAlbedo.IsValid()
                ? graph.GetResource(context.materialAlbedo)
                : nullptr;
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    table[6],
                    materialAlbedo,
                    DXGI_FORMAT_R8G8B8A8_UNORM)) {
                FailFullSceneCompositeV3(context, "full_scene_composite_v3_material_albedo_srv");
                return;
            }

            context.commandList->OMSetRenderTargets(
                static_cast<UINT>(context.outputRTVs.size()),
                context.outputRTVs.data(),
                FALSE,
                nullptr);
            FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);
            if (!FullscreenPass::BindGraphicsState({
                    context.commandList,
                    context.descriptorManager,
                    context.rootSignature,
                    context.frameConstants,
                })) {
                FailFullSceneCompositeV3(context, "full_scene_composite_v3_bind");
                return;
            }
            context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
            context.commandList->SetGraphicsRootDescriptorTable(3, table[0].gpu);
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

bool AddFullSceneReflectionResolverV3Pass(RenderGraph& graph,
                                          const FullSceneReflectionResolverV3Context& context) {
    if (!context.localReflectionRadiance.IsValid() ||
        !context.radiance.IsValid() ||
        !context.confidence.IsValid() ||
        !context.sourceId.IsValid() ||
        !context.rejectedSourceMask.IsValid() ||
        !context.temporalDelta.IsValid() ||
        !context.ssrSourceSignal.IsValid() ||
        !context.rtSourceSignal.IsValid() ||
        !context.sourceSuppression.IsValid() ||
        !context.historyPrevSourceId.IsValid() ||
        !context.historyValidity.IsValid() ||
        !context.historyRejection.IsValid() ||
        !context.normalRoughness.IsValid() ||
        !context.emissiveMetallic.IsValid() ||
        !context.materialExt2.IsValid() ||
        !context.device ||
        !context.descriptorManager ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        context.frameConstants == 0 ||
        context.width == 0 ||
        context.height == 0) {
        FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_contract");
        return false;
    }
    for (const auto rtv : context.outputRTVs) {
        if (rtv.ptr == 0) {
            FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_rtv");
            return false;
        }
    }

    graph.AddPass(
        "FullSceneReflectionV3",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.localReflectionRadiance, RGResourceUsage::ShaderResource);
            if (context.ssr.IsValid()) {
                builder.Read(context.ssr, RGResourceUsage::ShaderResource);
            }
            if (context.rtReflection.IsValid()) {
                builder.Read(context.rtReflection, RGResourceUsage::ShaderResource);
            }
            builder.Read(context.historyPrevSourceId, RGResourceUsage::ShaderResource);
            builder.Read(context.historyValidity, RGResourceUsage::ShaderResource);
            builder.Read(context.historyRejection, RGResourceUsage::ShaderResource);
            builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource);
            builder.Read(context.emissiveMetallic, RGResourceUsage::ShaderResource);
            builder.Read(context.materialExt2, RGResourceUsage::ShaderResource);
            builder.Write(context.radiance, RGResourceUsage::RenderTarget);
            builder.Write(context.confidence, RGResourceUsage::RenderTarget);
            builder.Write(context.sourceId, RGResourceUsage::RenderTarget);
            builder.Write(context.rejectedSourceMask, RGResourceUsage::RenderTarget);
            builder.Write(context.temporalDelta, RGResourceUsage::RenderTarget);
            builder.Write(context.ssrSourceSignal, RGResourceUsage::RenderTarget);
            builder.Write(context.rtSourceSignal, RGResourceUsage::RenderTarget);
            builder.Write(context.sourceSuppression, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            auto srvResult = context.descriptorManager->AllocateTransientCBV_SRV_UAVRange(9);
            if (srvResult.IsErr()) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_descriptor");
                return;
            }

            const DescriptorHandle inputSRV = srvResult.Value();
            const DescriptorHandle ssrSRV = context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 1u);
            const DescriptorHandle rtReflectionSRV = context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 2u);
            const DescriptorHandle historyPrevSourceIdSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 3u);
            const DescriptorHandle historyValiditySRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 4u);
            const DescriptorHandle historyRejectionSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 5u);
            const DescriptorHandle normalRoughnessSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 6u);
            const DescriptorHandle emissiveMetallicSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 7u);
            const DescriptorHandle materialExt2SRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(inputSRV.index + 8u);
            if (!ssrSRV.IsValid() ||
                !rtReflectionSRV.IsValid() ||
                !historyPrevSourceIdSRV.IsValid() ||
                !historyValiditySRV.IsValid() ||
                !historyRejectionSRV.IsValid() ||
                !normalRoughnessSRV.IsValid() ||
                !emissiveMetallicSRV.IsValid() ||
                !materialExt2SRV.IsValid()) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_source_descriptors");
                return;
            }
            ID3D12Resource* localRadiance = graph.GetResource(context.localReflectionRadiance);
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    inputSRV,
                    localRadiance,
                    DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_input_srv");
                return;
            }
            ID3D12Resource* ssr = context.ssr.IsValid()
                ? graph.GetResource(context.ssr)
                : nullptr;
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    ssrSRV,
                    ssr,
                    DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_ssr_srv");
                return;
            }
            ID3D12Resource* rtReflection = context.rtReflection.IsValid()
                ? graph.GetResource(context.rtReflection)
                : nullptr;
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    rtReflectionSRV,
                    rtReflection,
                    DXGI_FORMAT_R16G16B16A16_FLOAT)) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_rt_srv");
                return;
            }
            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    historyPrevSourceIdSRV,
                    graph.GetResource(context.historyPrevSourceId),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    historyValiditySRV,
                    graph.GetResource(context.historyValidity),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    historyRejectionSRV,
                    graph.GetResource(context.historyRejection),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    normalRoughnessSRV,
                    graph.GetResource(context.normalRoughness),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    emissiveMetallicSRV,
                    graph.GetResource(context.emissiveMetallic),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    materialExt2SRV,
                    graph.GetResource(context.materialExt2),
                    DXGI_FORMAT_R8G8B8A8_UNORM)) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_history_material_srv");
                return;
            }

            context.commandList->OMSetRenderTargets(
                static_cast<UINT>(context.outputRTVs.size()),
                context.outputRTVs.data(),
                FALSE,
                nullptr);
            FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);
            if (!FullscreenPass::BindGraphicsState({
                    context.commandList,
                    context.descriptorManager,
                    context.rootSignature,
                    context.frameConstants,
                })) {
                FailFullSceneReflectionResolverV3(context, "full_scene_reflection_resolver_v3_bind");
                return;
            }
            context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
            context.commandList->SetGraphicsRootDescriptorTable(3, inputSRV.gpu);
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

bool AddFullSceneReflectionHistoryV3Pass(RenderGraph& graph,
                                         const FullSceneReflectionHistoryV3Context& context) {
    if (!context.radiance.IsValid() ||
        !context.sourceId.IsValid() ||
        !context.temporalDelta.IsValid() ||
        !context.historyPrev.IsValid() ||
        !context.historyPrevSourceId.IsValid() ||
        !context.depth.IsValid() ||
        !context.normalRoughness.IsValid() ||
        !context.velocity.IsValid() ||
        !context.historyCurr.IsValid() ||
        !context.historyValidity.IsValid() ||
        !context.historyRejection.IsValid() ||
        !context.device ||
        !context.descriptorManager ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        context.frameConstants == 0 ||
        context.width == 0 ||
        context.height == 0) {
        FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_contract");
        return false;
    }
    for (const auto rtv : context.outputRTVs) {
        if (rtv.ptr == 0) {
            FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_rtv");
            return false;
        }
    }

    graph.AddPass(
        "FullSceneReflectionHistoryV3",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.radiance, RGResourceUsage::ShaderResource);
            builder.Read(context.sourceId, RGResourceUsage::ShaderResource);
            builder.Read(context.temporalDelta, RGResourceUsage::ShaderResource);
            builder.Read(context.historyPrev, RGResourceUsage::ShaderResource);
            builder.Read(context.historyPrevSourceId, RGResourceUsage::ShaderResource);
            builder.Read(context.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Read(context.normalRoughness, RGResourceUsage::ShaderResource);
            builder.Read(context.velocity, RGResourceUsage::ShaderResource);
            builder.Write(context.historyCurr, RGResourceUsage::RenderTarget);
            builder.Write(context.historyValidity, RGResourceUsage::RenderTarget);
            builder.Write(context.historyRejection, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph& graph) {
            auto srvResult = context.descriptorManager->AllocateTransientCBV_SRV_UAVRange(8);
            if (srvResult.IsErr()) {
                FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_descriptor");
                return;
            }

            const DescriptorHandle baseSRV = srvResult.Value();
            const DescriptorHandle sourceIdSRV = context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 1u);
            const DescriptorHandle temporalDeltaSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 2u);
            const DescriptorHandle historyPrevSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 3u);
            const DescriptorHandle historyPrevSourceIdSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 4u);
            const DescriptorHandle depthSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 5u);
            const DescriptorHandle normalRoughnessSRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 6u);
            const DescriptorHandle velocitySRV =
                context.descriptorManager->GetCBV_SRV_UAVHandle(baseSRV.index + 7u);
            if (!sourceIdSRV.IsValid() ||
                !temporalDeltaSRV.IsValid() ||
                !historyPrevSRV.IsValid() ||
                !historyPrevSourceIdSRV.IsValid() ||
                !depthSRV.IsValid() ||
                !normalRoughnessSRV.IsValid() ||
                !velocitySRV.IsValid()) {
                FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_source_descriptors");
                return;
            }

            if (!DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    baseSRV,
                    graph.GetResource(context.radiance),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    sourceIdSRV,
                    graph.GetResource(context.sourceId),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    temporalDeltaSRV,
                    graph.GetResource(context.temporalDelta),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    historyPrevSRV,
                    graph.GetResource(context.historyPrev),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    historyPrevSourceIdSRV,
                    graph.GetResource(context.historyPrevSourceId),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    depthSRV,
                    graph.GetResource(context.depth),
                    DXGI_FORMAT_R32_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    normalRoughnessSRV,
                    graph.GetResource(context.normalRoughness),
                    DXGI_FORMAT_R16G16B16A16_FLOAT) ||
                !DescriptorTable::WriteTexture2DSRV(
                    context.device,
                    velocitySRV,
                    graph.GetResource(context.velocity),
                    DXGI_FORMAT_R16G16_FLOAT)) {
                FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_input_srv");
                return;
            }

            context.commandList->OMSetRenderTargets(
                static_cast<UINT>(context.outputRTVs.size()),
                context.outputRTVs.data(),
                FALSE,
                nullptr);
            FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);
            if (!FullscreenPass::BindGraphicsState({
                    context.commandList,
                    context.descriptorManager,
                    context.rootSignature,
                    context.frameConstants,
                })) {
                FailFullSceneReflectionHistoryV3(context, "full_scene_reflection_history_v3_bind");
                return;
            }
            context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
            context.commandList->SetGraphicsRootDescriptorTable(3, baseSRV.gpu);
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

bool AddFullSceneReflectionHistoryV3CopyPass(
    RenderGraph& graph,
    const FullSceneReflectionHistoryV3CopyContext& context) {
    if (!context.historyCurr.IsValid() ||
        !context.historyPrev.IsValid() ||
        !context.sourceId.IsValid() ||
        !context.historyPrevSourceId.IsValid()) {
        FailFullSceneReflectionHistoryV3Copy(context, "full_scene_reflection_history_v3_copy_contract");
        return false;
    }

    graph.AddPass(
        "FullSceneReflectionHistoryV3Copy",
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Copy);
            builder.Read(context.historyCurr, RGResourceUsage::CopySrc);
            builder.Read(context.sourceId, RGResourceUsage::CopySrc);
            builder.Write(context.historyPrev, RGResourceUsage::CopyDst);
            builder.Write(context.historyPrevSourceId, RGResourceUsage::CopyDst);
        },
        [context](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph) {
            ID3D12Resource* curr = graph.GetResource(context.historyCurr);
            ID3D12Resource* prev = graph.GetResource(context.historyPrev);
            ID3D12Resource* sourceId = graph.GetResource(context.sourceId);
            ID3D12Resource* prevSourceId = graph.GetResource(context.historyPrevSourceId);
            if (!commandList || !curr || !prev || !sourceId || !prevSourceId) {
                FailFullSceneReflectionHistoryV3Copy(context, "full_scene_reflection_history_v3_copy_resources");
                return;
            }
            commandList->CopyResource(prev, curr);
            commandList->CopyResource(prevSourceId, sourceId);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

bool AddCandidateBeautyDisplayPass(RenderGraph& graph,
                                   const CandidateBeautyDisplayContext& context) {
    const char* passName = (context.passName && *context.passName)
        ? context.passName
        : "FullSceneCandidateBeautyV3Display";
    if (!context.candidate.IsValid() ||
        !context.backBuffer.IsValid() ||
        !context.device ||
        !context.descriptorManager ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        context.frameConstants == 0 ||
        !context.candidateSRV.IsValid() ||
        context.backBufferRTV.ptr == 0 ||
        context.width == 0 ||
        context.height == 0) {
        FailCandidateBeautyDisplay(context, "candidate_beauty_display_contract");
        return false;
    }

    graph.AddPass(
        passName,
        [context](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Graphics);
            builder.Read(context.candidate, RGResourceUsage::ShaderResource);
            builder.Write(context.backBuffer, RGResourceUsage::RenderTarget);
        },
        [context](ID3D12GraphicsCommandList*, const RenderGraph&) {
            auto transientResult = context.descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (transientResult.IsErr()) {
                FailCandidateBeautyDisplay(context, "candidate_beauty_display_descriptor");
                return;
            }

            const DescriptorHandle transientSRV = transientResult.Value();
            context.device->CopyDescriptorsSimple(
                1,
                transientSRV.cpu,
                context.candidateSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            context.commandList->OMSetRenderTargets(1, &context.backBufferRTV, FALSE, nullptr);
            FullscreenPass::SetViewportAndScissor(context.commandList, context.width, context.height);
            if (!FullscreenPass::BindGraphicsState({
                    context.commandList,
                    context.descriptorManager,
                    context.rootSignature,
                    context.frameConstants,
                })) {
                FailCandidateBeautyDisplay(context, "candidate_beauty_display_bind");
                return;
            }
            context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
            context.commandList->SetGraphicsRootDescriptorTable(3, transientSRV.gpu);
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

} // namespace Cortex::Graphics::FullSceneShaderV3Passes
