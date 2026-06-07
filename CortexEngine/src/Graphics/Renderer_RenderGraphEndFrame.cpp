#include "Renderer.h"

#include "Debug/GPUProfiler.h"
#include "Passes/BloomGraphPass.h"
#include "Passes/BloomPass.h"
#include "Passes/DescriptorTable.h"
#include "Passes/FullscreenPass.h"
#include "Passes/LocalReflectionRadiancePass.h"
#include "Passes/PostProcessGraphPass.h"
#include "Passes/RenderPassScope.h"
#include "RenderGraph.h"

#include <array>
#include <cstdlib>
#include <glm/geometric.hpp>
#include <span>

namespace Cortex::Graphics {

namespace {

constexpr D3D12_RESOURCE_STATES kRenderGraphShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

struct FullSceneCompositeV3Context {
    RGResourceHandle directLighting;
    RGResourceHandle indirectLighting;
    RGResourceHandle shadowVisibility;
    RGResourceHandle legacyHdr;
    RGResourceHandle localReflectionRadiance;
    RGResourceHandle reflectionConfidence;
    RGResourceHandle materialAlbedo;
    RGResourceHandle sceneLocalEnvironment;
    RGResourceHandle output;
    RGResourceHandle energyClampPolicy;
    RGResourceHandle overbrightDiagnostics;
    RGResourceHandle compositeContributionMap;
    RGResourceHandle legacyRescueUsage;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle directLightingSRV;
    DescriptorHandle indirectLightingSRV;
    DescriptorHandle shadowVisibilitySRV;
    DescriptorHandle legacyHdrSRV;
    DescriptorHandle sceneLocalEnvironmentSRV;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct SceneLocalEnvironmentV3Context {
    RGResourceHandle sceneLocalEnvironment;
    RGResourceHandle ambientLighting;
    RGResourceHandle visibleBackground;
    RGResourceHandle reflectionBackground;
    RGResourceHandle atmosphere;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 5> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionResolverV3Context {
    RGResourceHandle localReflectionRadiance;
    RGResourceHandle ssr;
    RGResourceHandle rtReflection;
    RGResourceHandle historyPrevSourceId;
    RGResourceHandle historyValidity;
    RGResourceHandle historyRejection;
    RGResourceHandle normalRoughness;
    RGResourceHandle emissiveMetallic;
    RGResourceHandle materialExt2;
    RGResourceHandle radiance;
    RGResourceHandle confidence;
    RGResourceHandle sourceId;
    RGResourceHandle rejectedSourceMask;
    RGResourceHandle temporalDelta;
    RGResourceHandle ssrSourceSignal;
    RGResourceHandle rtSourceSignal;
    RGResourceHandle sourceSuppression;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionHistoryV3Context {
    RGResourceHandle radiance;
    RGResourceHandle sourceId;
    RGResourceHandle temporalDelta;
    RGResourceHandle historyPrev;
    RGResourceHandle historyPrevSourceId;
    RGResourceHandle depth;
    RGResourceHandle normalRoughness;
    RGResourceHandle velocity;
    RGResourceHandle historyCurr;
    RGResourceHandle historyValidity;
    RGResourceHandle historyRejection;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> outputRTVs{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

struct FullSceneReflectionHistoryV3CopyContext {
    RGResourceHandle historyCurr;
    RGResourceHandle historyPrev;
    RGResourceHandle sourceId;
    RGResourceHandle historyPrevSourceId;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

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

[[nodiscard]] bool AddSceneLocalEnvironmentV3Pass(RenderGraph& graph,
                                                  const SceneLocalEnvironmentV3Context& context) {
    if (!context.sceneLocalEnvironment.IsValid() ||
        !context.ambientLighting.IsValid() ||
        !context.visibleBackground.IsValid() ||
        !context.reflectionBackground.IsValid() ||
        !context.atmosphere.IsValid() ||
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
            FullscreenPass::DrawTriangle(context.commandList);
            if (context.ran) {
                *context.ran = true;
            }
        });

    return true;
}

[[nodiscard]] bool AddFullSceneCompositeV3Pass(RenderGraph& graph,
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

[[nodiscard]] bool AddFullSceneReflectionResolverV3Pass(RenderGraph& graph,
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

[[nodiscard]] bool AddFullSceneReflectionHistoryV3Pass(RenderGraph& graph,
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

[[nodiscard]] bool AddFullSceneReflectionHistoryV3CopyPass(
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

struct CandidateBeautyDisplayContext {
    const char* passName = "FullSceneCandidateBeautyV3Display";
    RGResourceHandle candidate;
    RGResourceHandle backBuffer;
    ID3D12Device* device = nullptr;
    DescriptorHeapManager* descriptorManager = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    DX12RootSignature* rootSignature = nullptr;
    DX12Pipeline* pipeline = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS frameConstants = 0;
    DescriptorHandle candidateSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV{};
    uint32_t width = 0;
    uint32_t height = 0;
    bool* ran = nullptr;
    bool* failed = nullptr;
    const char** stage = nullptr;
};

void FailCandidateBeautyDisplay(const CandidateBeautyDisplayContext& context, const char* stage) {
    if (context.failed) {
        *context.failed = true;
    }
    if (context.stage && !*context.stage) {
        *context.stage = stage ? stage : "candidate_beauty_display_unknown";
    }
}

[[nodiscard]] bool AddCandidateBeautyDisplayPass(RenderGraph& graph,
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

} // namespace

Renderer::EndFrameGraphResult
Renderer::ExecuteEndFrameInRenderGraph(const EndFrameGraphInputs& inputs) {
    EndFrameGraphResult result{};

    const bool canRunRg = (m_services.renderGraph && m_services.device && m_commandResources.graphicsList && m_services.descriptorManager);
    const bool wantsRgHzbThisFrame =
        inputs.hzbPending && inputs.useRenderGraphHZB && canRunRg && m_depthResources.resources.buffer &&
        m_depthResources.descriptors.srv.IsValid() && m_hzbResources.resources.texture;
    const bool wantsRgPostThisFrame =
        inputs.runPostProcess && inputs.useRenderGraphPost && canRunRg && m_pipelineState.postProcess &&
        m_mainTargets.hdr.resources.color && m_services.window && m_services.window->GetCurrentBackBuffer();
    bool wantsFusedBloomThisFrame =
        inputs.runBloom && wantsRgPostThisFrame &&
        m_pipelineState.bloomDownsample && m_pipelineState.bloomBlurH && m_pipelineState.bloomBlurV &&
        m_pipelineState.bloomComposite && m_mainTargets.hdr.descriptors.srv.IsValid() && m_bloomResources.controls.intensity > 0.0f &&
        m_bloomResources.resources.texA[0] && m_bloomResources.resources.texB[0];
    const bool wantsCandidateBeautyThisFrame =
        wantsRgPostThisFrame &&
        (m_postProcessState.fullSceneCandidateBeautyV3Enabled ||
         std::getenv("CORTEX_ENABLE_FULL_SCENE_CANDIDATE_BEAUTY_V3") != nullptr) &&
        m_mainTargets.candidateBeautyV3.resources.ldrOutput &&
        m_mainTargets.candidateBeautyV3.descriptors.ldrOutputRTV.IsValid();
    const bool wantsCandidateBeautyDisplayThisFrame =
        wantsCandidateBeautyThisFrame &&
        (m_postProcessState.fullSceneCandidateBeautyV3Enabled ||
         std::getenv("CORTEX_DISPLAY_FULL_SCENE_CANDIDATE_BEAUTY_V3") != nullptr) &&
        m_pipelineState.candidateBeautyDisplay &&
        m_mainTargets.candidateBeautyV3.descriptors.ldrOutputSRV.IsValid();
    const bool wantsCompositeV3ThisFrame =
        wantsCandidateBeautyThisFrame &&
        m_pipelineState.fullSceneCompositeV3 &&
        m_mainTargets.compositeV3.resources.hdrSceneColor &&
        m_mainTargets.compositeV3.resources.energyClampPolicy &&
        m_mainTargets.compositeV3.resources.overbrightDiagnostics &&
        m_mainTargets.compositeV3.resources.compositeContributionMap &&
        m_mainTargets.compositeV3.resources.legacyRescueUsage &&
        m_mainTargets.compositeV3.descriptors.hdrSceneColorRTV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.hdrSceneColorSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.energyClampPolicyRTV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.energyClampPolicySRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.overbrightDiagnosticsRTV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.overbrightDiagnosticsSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.compositeContributionMapRTV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.compositeContributionMapSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.legacyRescueUsageRTV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.legacyRescueUsageSRV.IsValid() &&
        m_mainTargets.lightingV3.resources.directLighting &&
        m_mainTargets.lightingV3.resources.indirectLighting &&
        m_mainTargets.lightingV3.resources.shadowVisibility &&
        m_mainTargets.lightingV3.descriptors.directLightingSRV.IsValid() &&
        m_mainTargets.lightingV3.descriptors.indirectLightingSRV.IsValid() &&
        m_mainTargets.lightingV3.descriptors.shadowVisibilitySRV.IsValid() &&
        m_mainTargets.hdr.descriptors.srv.IsValid();
    const bool wantsSceneLocalEnvironmentV3ThisFrame =
        wantsRgPostThisFrame &&
        m_pipelineState.sceneLocalEnvironmentV3 &&
        m_mainTargets.environmentV3.resources.sceneLocalEnvironment &&
        m_mainTargets.environmentV3.resources.ambientLighting &&
        m_mainTargets.environmentV3.resources.visibleBackground &&
        m_mainTargets.environmentV3.resources.reflectionBackground &&
        m_mainTargets.environmentV3.resources.atmosphere &&
        m_mainTargets.environmentV3.descriptors.sceneLocalEnvironmentRTV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.sceneLocalEnvironmentSRV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.ambientLightingRTV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.ambientLightingSRV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.visibleBackgroundRTV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.visibleBackgroundSRV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.reflectionBackgroundRTV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.reflectionBackgroundSRV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.atmosphereRTV.IsValid() &&
        m_mainTargets.environmentV3.descriptors.atmosphereSRV.IsValid();
    const bool wantsSceneLocalEnvironmentV3DebugViewThisFrame =
        wantsSceneLocalEnvironmentV3ThisFrame &&
        m_debugViewState.mode >= 83u &&
        m_debugViewState.mode <= 87u &&
        m_pipelineState.candidateBeautyDisplay;
    const bool wantsCompositeV3DebugViewThisFrame =
        wantsCompositeV3ThisFrame &&
        (m_debugViewState.mode == 67u ||
         m_debugViewState.mode == 80u ||
         m_debugViewState.mode == 81u ||
         m_debugViewState.mode == 88u ||
         m_debugViewState.mode == 89u) &&
        m_pipelineState.candidateBeautyDisplay &&
        m_mainTargets.compositeV3.descriptors.hdrSceneColorSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.energyClampPolicySRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.overbrightDiagnosticsSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.compositeContributionMapSRV.IsValid() &&
        m_mainTargets.compositeV3.descriptors.legacyRescueUsageSRV.IsValid();
    const bool wantsReflectionResolverV3ThisFrame =
        wantsRgPostThisFrame &&
        m_pipelineState.fullSceneReflectionResolverV3 &&
        m_mainTargets.normalRoughness.resources.texture &&
        m_mainTargets.reflectionV3.resources.radiance &&
        m_mainTargets.reflectionV3.resources.confidence &&
        m_mainTargets.reflectionV3.resources.sourceId &&
        m_mainTargets.reflectionV3.resources.rejectedSourceMask &&
        m_mainTargets.reflectionV3.resources.temporalDelta &&
        m_mainTargets.reflectionV3.resources.ssrSourceSignal &&
        m_mainTargets.reflectionV3.resources.rtSourceSignal &&
        m_mainTargets.reflectionV3.resources.sourceSuppression &&
        m_mainTargets.reflectionV3.resources.historyPrevSourceId &&
        m_mainTargets.reflectionV3.resources.historyValidity &&
        m_mainTargets.reflectionV3.resources.historyRejection &&
        m_mainTargets.reflectionV3.descriptors.radianceRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.radianceSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.confidenceRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.confidenceSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.sourceIdRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.sourceIdSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.rejectedSourceMaskRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.rejectedSourceMaskSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.temporalDeltaRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.temporalDeltaSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.ssrSourceSignalRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.ssrSourceSignalSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.rtSourceSignalRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.rtSourceSignalSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.sourceSuppressionRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.sourceSuppressionSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyPrevSourceIdSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyValiditySRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyRejectionSRV.IsValid();
    const bool wantsReflectionHistoryV3ThisFrame =
        wantsReflectionResolverV3ThisFrame &&
        m_pipelineState.fullSceneReflectionHistoryV3 &&
        m_mainTargets.reflectionV3.resources.historyCurr &&
        m_mainTargets.reflectionV3.resources.historyPrev &&
        m_mainTargets.reflectionV3.resources.historyPrevSourceId &&
        m_mainTargets.reflectionV3.resources.historyValidity &&
        m_mainTargets.reflectionV3.resources.historyRejection &&
        m_mainTargets.reflectionV3.descriptors.historyCurrRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyCurrSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyPrevSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyPrevSourceIdSRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyValidityRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyValiditySRV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyRejectionRTV.IsValid() &&
        m_mainTargets.reflectionV3.descriptors.historyRejectionSRV.IsValid();
    const bool wantsReflectionResolverV3DebugViewThisFrame =
        wantsReflectionResolverV3ThisFrame &&
        m_debugViewState.mode >= 68u &&
        m_debugViewState.mode <= 79u &&
        m_pipelineState.candidateBeautyDisplay;
    const bool useFusedBloomTransients =
        wantsFusedBloomThisFrame &&
        std::getenv("CORTEX_DISABLE_BLOOM_TRANSIENTS") == nullptr;

    if (!wantsRgHzbThisFrame && !wantsRgPostThisFrame) {
        return result;
    }

    result.attempted = true;
    result.attemptedBloom = wantsFusedBloomThisFrame;
    result.attemptedCompositeV3 = wantsCompositeV3ThisFrame;
    result.attemptedCompositeV3DebugView = wantsCompositeV3DebugViewThisFrame;
    result.attemptedCandidateBeauty = wantsCandidateBeautyThisFrame;
    result.attemptedCandidateBeautyDisplay = wantsCandidateBeautyDisplayThisFrame;
    Debug::GPUProfiler::Get().BeginScope(m_commandResources.graphicsList.Get(), "RenderGraphEndFrame", "RenderGraph");
    m_services.renderGraph->BeginFrame();

    RGResourceHandle depthHandle{};
    RGResourceHandle hzbHandle{};
    if (wantsRgHzbThisFrame) {
        depthHandle = m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth");
        hzbHandle = m_services.renderGraph->ImportResource(m_hzbResources.resources.texture.Get(), m_hzbResources.resources.resourceState, "HZB");
        AddHZBFromDepthPasses_RG(*m_services.renderGraph, depthHandle, hzbHandle);
    }

    RGResourceHandle hdrHandle{};
    RGResourceHandle ssaoHandle{};
    RGResourceHandle ssrHandle{};
    RGResourceHandle bloomHandle{};
    RGResourceHandle historyHandle{};
    RGResourceHandle depthPpHandle{};
    RGResourceHandle albedoHandle{};
    RGResourceHandle normalHandle{};
    RGResourceHandle emissiveMetallicHandle{};
    RGResourceHandle materialExt1Handle{};
    RGResourceHandle materialExt2Handle{};
    RGResourceHandle velocityHandle{};
    RGResourceHandle taaHandle{};
    RGResourceHandle rtReflHandle{};
    RGResourceHandle rtReflHistHandle{};
    RGResourceHandle localReflRadianceHandle{};
    RGResourceHandle backBufferHandle{};
    RGResourceHandle v3DirectLightingHandle{};
    RGResourceHandle v3IndirectLightingHandle{};
    RGResourceHandle v3ShadowVisibilityHandle{};
    RGResourceHandle reflectionRadianceHandle{};
    RGResourceHandle reflectionConfidenceHandle{};
    RGResourceHandle reflectionSourceIdHandle{};
    RGResourceHandle reflectionRejectedSourceMaskHandle{};
    RGResourceHandle reflectionTemporalDeltaHandle{};
    RGResourceHandle reflectionSSRSourceSignalHandle{};
    RGResourceHandle reflectionRTSourceSignalHandle{};
    RGResourceHandle reflectionSourceSuppressionHandle{};
    RGResourceHandle reflectionHistoryCurrHandle{};
    RGResourceHandle reflectionHistoryPrevHandle{};
    RGResourceHandle reflectionHistoryPrevSourceIdHandle{};
    RGResourceHandle reflectionHistoryValidityHandle{};
    RGResourceHandle reflectionHistoryRejectionHandle{};
    RGResourceHandle sceneLocalEnvironmentHandle{};
    RGResourceHandle ambientLightingHandle{};
    RGResourceHandle visibleBackgroundHandle{};
    RGResourceHandle reflectionBackgroundHandle{};
    RGResourceHandle atmosphereHandle{};
    RGResourceHandle candidateHdrSceneColorHandle{};
    RGResourceHandle energyClampPolicyHandle{};
    RGResourceHandle overbrightDiagnosticsHandle{};
    RGResourceHandle compositeContributionMapHandle{};
    RGResourceHandle legacyRescueUsageHandle{};
    RGResourceHandle candidateBeautyHandle{};
    std::array<RGResourceHandle, kBloomLevels> bloomA{};
    std::array<RGResourceHandle, kBloomLevels> bloomB{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomA{};
    std::array<ComPtr<ID3D12Resource>, kBloomLevels> savedBloomB{};
    bool bloomStageFailed = false;
    bool ranReflectionResolverV3 = false;
    bool ranReflectionHistoryV3 = false;
    bool ranReflectionHistoryV3Copy = false;
    bool ranReflectionResolverV3DebugView = false;
    bool ranSceneLocalEnvironmentV3 = false;
    bool ranSceneLocalEnvironmentV3DebugView = false;
    bool scheduledReflectionResolverV3 = false;
    const char* bloomGraphStageError = nullptr;
    const char* postProcessGraphStageError = nullptr;
    VisibilityBufferRenderer::ResourceStateSnapshot vbPostInitialStates{};
    bool hasVBPostStates = false;
    ID3D12Resource* postNormalResource = nullptr;
    ID3D12Resource* postAlbedoResource = nullptr;
    ID3D12Resource* postEmissiveMetallicResource = nullptr;
    ID3D12Resource* postMaterialExt1Resource = nullptr;
    ID3D12Resource* postMaterialExt2Resource = nullptr;

    if (useFusedBloomTransients) {
        for (uint32_t level = 0; level < kBloomLevels; ++level) {
            savedBloomA[level] = m_bloomResources.resources.texA[level];
            savedBloomB[level] = m_bloomResources.resources.texB[level];
        }

        static bool s_loggedFusedBloomTransients = false;
        if (!s_loggedFusedBloomTransients) {
            s_loggedFusedBloomTransients = true;
            spdlog::info("Bloom RG: fused graph-owned intermediates enabled (default)");
        }
    }

    auto bloomFullscreenContext = [&]() {
        BloomPass::FullscreenContext context{};
        context.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        context.commandList = m_commandResources.graphicsList.Get();
        context.descriptorManager = m_services.descriptorManager.get();
        context.rootSignature = m_pipelineState.rootSignature.get();
        context.frameConstants = m_constantBuffers.currentFrameGPU;
        context.srvTable = m_bloomResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount].data();
        context.srvTableCount = kBloomDescriptorSlots;
        context.srvTableValid = m_bloomResources.descriptors.srvTableValid;
        return context;
    };

    if (wantsRgPostThisFrame) {
        hdrHandle = m_services.renderGraph->ImportResource(m_mainTargets.hdr.resources.color.Get(), m_mainTargets.hdr.resources.state, "HDR");
        if (m_temporalScreenState.historyColor) {
            historyHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.historyColor.Get(), m_temporalScreenState.historyState, "TAAHistory");
        }
        if (m_depthResources.resources.buffer) {
            depthPpHandle = depthHandle.IsValid()
                ? depthHandle
                : m_services.renderGraph->ImportResource(m_depthResources.resources.buffer.Get(), m_depthResources.resources.resourceState, "Depth_Post");
        }
        if (m_ssaoResources.resources.texture) {
            ssaoHandle = m_services.renderGraph->ImportResource(m_ssaoResources.resources.texture.Get(), m_ssaoResources.resources.resourceState, "SSAO");
        }
        if (m_ssrResources.resources.color) {
            ssrHandle = m_services.renderGraph->ImportResource(m_ssrResources.resources.color.Get(), m_ssrResources.resources.resourceState, "SSRColor");
        }
        if (wantsFusedBloomThisFrame) {
            if (!useFusedBloomTransients) {
                for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
                    if (m_bloomResources.resources.texA[level]) {
                        bloomA[level] = m_services.renderGraph->ImportResource(
                            m_bloomResources.resources.texA[level].Get(),
                            m_bloomResources.resources.resourceState[level][0],
                            "BloomA_FusedPersistent" + std::to_string(level));
                    }
                    if (m_bloomResources.resources.texB[level]) {
                        bloomB[level] = m_services.renderGraph->ImportResource(
                            m_bloomResources.resources.texB[level].Get(),
                            m_bloomResources.resources.resourceState[level][1],
                            "BloomB_FusedPersistent" + std::to_string(level));
                    }
                }
            }

            const uint32_t baseLevel = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
            std::array<ID3D12Resource*, kBloomLevels> bloomATemplates{};
            std::array<ID3D12Resource*, kBloomLevels> bloomBTemplates{};
            for (uint32_t level = 0; level < kBloomLevels; ++level) {
                bloomATemplates[level] = savedBloomA[level].Get();
                bloomBTemplates[level] = savedBloomB[level].Get();
            }

            BloomGraphPass::FusedBloomContext bloomContext{};
            bloomContext.hdr = hdrHandle;
            bloomContext.bloomA = std::span<RGResourceHandle>(bloomA.data(), bloomA.size());
            bloomContext.bloomB = std::span<RGResourceHandle>(bloomB.data(), bloomB.size());
            bloomContext.bloomATemplates =
                std::span<ID3D12Resource* const>(bloomATemplates.data(), bloomATemplates.size());
            bloomContext.bloomBTemplates =
                std::span<ID3D12Resource* const>(bloomBTemplates.data(), bloomBTemplates.size());
            bloomContext.graphRtv = m_bloomResources.resources.graphRtv;
            bloomContext.fullscreen = bloomFullscreenContext();
            bloomContext.downsamplePipeline = m_pipelineState.bloomDownsample.get();
            bloomContext.blurHPipeline = m_pipelineState.bloomBlurH.get();
            bloomContext.blurVPipeline = m_pipelineState.bloomBlurV.get();
            bloomContext.compositePipeline = m_pipelineState.bloomComposite.get();
            bloomContext.activeLevels = m_bloomResources.resources.activeLevels;
            bloomContext.stageLevels = kBloomLevels;
            bloomContext.baseLevel = baseLevel;
            bloomContext.useTransients = useFusedBloomTransients;
            bloomContext.hdrResourceState = &m_mainTargets.hdr.resources.state;
            bloomContext.hdrShaderResourceState = kRenderGraphShaderResourceState;
            bloomContext.status.failed = &bloomStageFailed;
            bloomContext.status.stage = &bloomGraphStageError;
            bloomContext.bloomRan = &result.ranBloom;
            bloomContext.bloomStageFailed = &bloomStageFailed;

            bloomHandle = BloomGraphPass::AddFusedBloom(*m_services.renderGraph, bloomContext);
        } else if (m_bloomResources.controls.intensity > 0.0f) {
            ID3D12Resource* bloomRes = (m_bloomResources.resources.activeLevels > 1) ? m_bloomResources.resources.texA[1].Get() : m_bloomResources.resources.texA[0].Get();
            if (bloomRes) {
                const uint32_t level = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
                bloomHandle = m_services.renderGraph->ImportResource(bloomRes, m_bloomResources.resources.resourceState[level][0], "BloomCombined");
            }
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer) {
            vbPostInitialStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            hasVBPostStates = true;
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetAlbedoBuffer()) {
            postAlbedoResource = m_services.visibilityBuffer->GetAlbedoBuffer();
            albedoHandle = m_services.renderGraph->ImportResource(
                postAlbedoResource,
                vbPostInitialStates.albedo,
                "MaterialAlbedo");
        }
        {
            ID3D12Resource* normalRes = m_mainTargets.normalRoughness.resources.texture.Get();
            D3D12_RESOURCE_STATES normalState = m_mainTargets.normalRoughness.resources.state;
            if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetNormalRoughnessBuffer()) {
                normalRes = m_services.visibilityBuffer->GetNormalRoughnessBuffer();
                normalState = vbPostInitialStates.normalRoughness;
            }
            postNormalResource = normalRes;
            if (normalRes) {
                normalHandle = m_services.renderGraph->ImportResource(normalRes, normalState, "NormalRoughness");
            }
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetEmissiveMetallicBuffer()) {
            postEmissiveMetallicResource = m_services.visibilityBuffer->GetEmissiveMetallicBuffer();
            emissiveMetallicHandle = m_services.renderGraph->ImportResource(
                postEmissiveMetallicResource,
                vbPostInitialStates.emissiveMetallic,
                "EmissiveMetallic");
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetMaterialExt1Buffer()) {
            postMaterialExt1Resource = m_services.visibilityBuffer->GetMaterialExt1Buffer();
            materialExt1Handle = m_services.renderGraph->ImportResource(
                postMaterialExt1Resource,
                vbPostInitialStates.materialExt1,
                "MaterialExt1");
        }
        if (m_visibilityBufferState.renderedThisFrame && m_services.visibilityBuffer && m_services.visibilityBuffer->GetMaterialExt2Buffer()) {
            postMaterialExt2Resource = m_services.visibilityBuffer->GetMaterialExt2Buffer();
            materialExt2Handle = m_services.renderGraph->ImportResource(
                postMaterialExt2Resource,
                vbPostInitialStates.materialExt2,
                "MaterialExt2_SurfaceClass");
        }
        if (m_temporalScreenState.velocityBuffer) {
            velocityHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.velocityBuffer.Get(), m_temporalScreenState.velocityState, "Velocity");
        }
        if (m_temporalScreenState.taaIntermediate) {
            taaHandle = m_services.renderGraph->ImportResource(m_temporalScreenState.taaIntermediate.Get(), m_temporalScreenState.taaIntermediateState, "TAAIntermediate");
        }
        if (m_rtReflectionTargets.color) {
            rtReflHandle = m_services.renderGraph->ImportResource(m_rtReflectionTargets.color.Get(), m_rtReflectionTargets.colorState, "RTReflection");
        }
        if (m_rtReflectionTargets.history) {
            rtReflHistHandle =
                m_services.renderGraph->ImportResource(m_rtReflectionTargets.history.Get(), m_rtReflectionTargets.historyState, "RTReflectionHistory");
        }

        backBufferHandle = m_services.renderGraph->ImportResource(
            m_services.window->GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            "BackBuffer");
        if (wantsCompositeV3ThisFrame) {
            v3DirectLightingHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.lightingV3.resources.directLighting.Get(),
                m_mainTargets.lightingV3.resources.state,
                "V3DirectLighting_ForComposite");
            v3IndirectLightingHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.lightingV3.resources.indirectLighting.Get(),
                m_mainTargets.lightingV3.resources.state,
                "V3IndirectLighting_ForComposite");
            v3ShadowVisibilityHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.lightingV3.resources.shadowVisibility.Get(),
                m_mainTargets.lightingV3.resources.state,
                "V3ShadowVisibility_ForComposite");
            candidateHdrSceneColorHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.compositeV3.resources.hdrSceneColor.Get(),
                m_mainTargets.compositeV3.resources.hdrSceneColorState,
                "CandidateHDRSceneColor");
            energyClampPolicyHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.compositeV3.resources.energyClampPolicy.Get(),
                m_mainTargets.compositeV3.resources.energyClampPolicyState,
                "EnergyClampPolicy");
            overbrightDiagnosticsHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.compositeV3.resources.overbrightDiagnostics.Get(),
                m_mainTargets.compositeV3.resources.overbrightDiagnosticsState,
                "OverbrightDiagnostics");
            compositeContributionMapHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.compositeV3.resources.compositeContributionMap.Get(),
                m_mainTargets.compositeV3.resources.compositeContributionMapState,
                "CompositeContributionMap");
            legacyRescueUsageHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.compositeV3.resources.legacyRescueUsage.Get(),
                m_mainTargets.compositeV3.resources.legacyRescueUsageState,
                "LegacyRescueUsage");
        }
        if (wantsReflectionResolverV3ThisFrame) {
            reflectionRadianceHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.radiance.Get(),
                m_mainTargets.reflectionV3.resources.radianceState,
                "ReflectionV3Radiance");
            reflectionConfidenceHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.confidence.Get(),
                m_mainTargets.reflectionV3.resources.confidenceState,
                "ReflectionV3Confidence");
            reflectionSourceIdHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.sourceId.Get(),
                m_mainTargets.reflectionV3.resources.sourceIdState,
                "ReflectionV3SourceId");
            reflectionRejectedSourceMaskHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.rejectedSourceMask.Get(),
                m_mainTargets.reflectionV3.resources.rejectedSourceMaskState,
                "ReflectionV3RejectedSourceMask");
            reflectionTemporalDeltaHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.temporalDelta.Get(),
                m_mainTargets.reflectionV3.resources.temporalDeltaState,
                "ReflectionV3TemporalDelta");
            reflectionSSRSourceSignalHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.ssrSourceSignal.Get(),
                m_mainTargets.reflectionV3.resources.ssrSourceSignalState,
                "ReflectionV3SSRSourceSignal");
            reflectionRTSourceSignalHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.rtSourceSignal.Get(),
                m_mainTargets.reflectionV3.resources.rtSourceSignalState,
                "ReflectionV3RTSourceSignal");
            reflectionSourceSuppressionHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.reflectionV3.resources.sourceSuppression.Get(),
                m_mainTargets.reflectionV3.resources.sourceSuppressionState,
                "ReflectionV3SourceSuppression");
            if (wantsReflectionHistoryV3ThisFrame) {
                reflectionHistoryCurrHandle = m_services.renderGraph->ImportResource(
                    m_mainTargets.reflectionV3.resources.historyCurr.Get(),
                    m_mainTargets.reflectionV3.resources.historyCurrState,
                    "ReflectionHistoryV3Curr");
                reflectionHistoryPrevHandle = m_services.renderGraph->ImportResource(
                    m_mainTargets.reflectionV3.resources.historyPrev.Get(),
                    m_mainTargets.reflectionV3.resources.historyPrevState,
                    "ReflectionHistoryV3Prev");
                reflectionHistoryPrevSourceIdHandle = m_services.renderGraph->ImportResource(
                    m_mainTargets.reflectionV3.resources.historyPrevSourceId.Get(),
                    m_mainTargets.reflectionV3.resources.historyPrevSourceIdState,
                    "ReflectionHistoryV3PrevSourceId");
                reflectionHistoryValidityHandle = m_services.renderGraph->ImportResource(
                    m_mainTargets.reflectionV3.resources.historyValidity.Get(),
                    m_mainTargets.reflectionV3.resources.historyValidityState,
                    "ReflectionHistoryV3Validity");
                reflectionHistoryRejectionHandle = m_services.renderGraph->ImportResource(
                    m_mainTargets.reflectionV3.resources.historyRejection.Get(),
                    m_mainTargets.reflectionV3.resources.historyRejectionState,
                    "ReflectionHistoryV3Rejection");
            }
        }
        if (wantsCandidateBeautyThisFrame) {
            candidateBeautyHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.candidateBeautyV3.resources.ldrOutput.Get(),
                m_mainTargets.candidateBeautyV3.resources.state,
                "CandidateBeautyV3Output");
        }
        if (wantsSceneLocalEnvironmentV3ThisFrame) {
            sceneLocalEnvironmentHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.environmentV3.resources.sceneLocalEnvironment.Get(),
                m_mainTargets.environmentV3.resources.sceneLocalEnvironmentState,
                "SceneLocalEnvironmentV3Aggregate");
            ambientLightingHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.environmentV3.resources.ambientLighting.Get(),
                m_mainTargets.environmentV3.resources.ambientLightingState,
                "SceneLocalEnvironmentV3AmbientLighting");
            visibleBackgroundHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.environmentV3.resources.visibleBackground.Get(),
                m_mainTargets.environmentV3.resources.visibleBackgroundState,
                "SceneLocalEnvironmentV3VisibleBackground");
            reflectionBackgroundHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.environmentV3.resources.reflectionBackground.Get(),
                m_mainTargets.environmentV3.resources.reflectionBackgroundState,
                "SceneLocalEnvironmentV3ReflectionBackground");
            atmosphereHandle = m_services.renderGraph->ImportResource(
                m_mainTargets.environmentV3.resources.atmosphere.Get(),
                m_mainTargets.environmentV3.resources.atmosphereState,
                "SceneLocalEnvironmentV3Atmosphere");
        }

        const bool wantsHzbDebug = (m_debugViewState.mode == 32u);
        if (wantsHzbDebug && m_hzbResources.resources.texture && !hzbHandle.IsValid()) {
            hzbHandle = m_services.renderGraph->ImportResource(m_hzbResources.resources.texture.Get(), m_hzbResources.resources.resourceState, "HZB_Debug");
        }

        if (wantsSceneLocalEnvironmentV3ThisFrame) {
            SceneLocalEnvironmentV3Context environmentContext{};
            environmentContext.sceneLocalEnvironment = sceneLocalEnvironmentHandle;
            environmentContext.ambientLighting = ambientLightingHandle;
            environmentContext.visibleBackground = visibleBackgroundHandle;
            environmentContext.reflectionBackground = reflectionBackgroundHandle;
            environmentContext.atmosphere = atmosphereHandle;
            environmentContext.commandList = m_commandResources.graphicsList.Get();
            environmentContext.rootSignature = m_pipelineState.rootSignature.get();
            environmentContext.pipeline = m_pipelineState.sceneLocalEnvironmentV3.get();
            environmentContext.descriptorManager = m_services.descriptorManager.get();
            environmentContext.frameConstants = m_constantBuffers.currentFrameGPU;
            environmentContext.outputRTVs = {
                m_mainTargets.environmentV3.descriptors.sceneLocalEnvironmentRTV.cpu,
                m_mainTargets.environmentV3.descriptors.ambientLightingRTV.cpu,
                m_mainTargets.environmentV3.descriptors.visibleBackgroundRTV.cpu,
                m_mainTargets.environmentV3.descriptors.reflectionBackgroundRTV.cpu,
                m_mainTargets.environmentV3.descriptors.atmosphereRTV.cpu,
            };
            environmentContext.width = GetInternalRenderWidth();
            environmentContext.height = GetInternalRenderHeight();
            environmentContext.ran = &ranSceneLocalEnvironmentV3;
            environmentContext.failed = &bloomStageFailed;
            environmentContext.stage = &postProcessGraphStageError;
            if (!AddSceneLocalEnvironmentV3Pass(*m_services.renderGraph, environmentContext)) {
                bloomStageFailed = true;
            }
        }

        if (m_pipelineState.localReflectionRadianceCompute &&
            m_localReflectionRadianceState.descriptors.valid &&
            depthPpHandle.IsValid() &&
            normalHandle.IsValid() &&
            emissiveMetallicHandle.IsValid() &&
            materialExt1Handle.IsValid() &&
            materialExt2Handle.IsValid() &&
            hdrHandle.IsValid()) {
            auto& localSrvTable =
                m_localReflectionRadianceState.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];
            auto& localUavTable =
                m_localReflectionRadianceState.descriptors.uavTables[m_frameRuntime.frameIndex % kFrameCount];

            ID3D12Resource* envSpecularResource = nullptr;
            DXGI_FORMAT envSpecularFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (const EnvironmentMaps* env = m_environmentState.ActiveEnvironment()) {
                if (env->specularPrefiltered && env->specularPrefiltered->GetResource()) {
                    envSpecularResource = env->specularPrefiltered->GetResource();
                    envSpecularFormat = env->specularPrefiltered->GetFormat();
                }
            }

            LocalReflectionRadiancePass::GraphContext localRadianceContext{};
            localRadianceContext.resources.depth = depthPpHandle;
            localRadianceContext.resources.normalRoughness = normalHandle;
            localRadianceContext.resources.emissiveMetallic = emissiveMetallicHandle;
            localRadianceContext.resources.materialExt1 = materialExt1Handle;
            localRadianceContext.resources.materialExt2 = materialExt2Handle;
            localRadianceContext.resources.sceneColor = hdrHandle;
            localRadianceContext.dispatch.device = m_services.device->GetDevice();
            localRadianceContext.dispatch.descriptorManager = m_services.descriptorManager.get();
            localRadianceContext.dispatch.rootSignature = m_pipelineState.computeRootSignature->GetRootSignature();
            localRadianceContext.dispatch.pipeline = m_pipelineState.localReflectionRadianceCompute.get();
            localRadianceContext.dispatch.frameConstants = m_constantBuffers.currentFrameGPU;
            localRadianceContext.dispatch.srvTable =
                std::span<DescriptorHandle>(localSrvTable.data(), localSrvTable.size());
            localRadianceContext.dispatch.uavTable =
                std::span<DescriptorHandle>(localUavTable.data(), localUavTable.size());
            localRadianceContext.dispatch.envSpecular = envSpecularResource;
            localRadianceContext.dispatch.envSpecularFormat = envSpecularFormat;
            localRadianceContext.dispatch.width = m_services.window->GetWidth();
            localRadianceContext.dispatch.height = m_services.window->GetHeight();
            localRadianceContext.status.failed = &bloomStageFailed;
            localRadianceContext.status.stage = &postProcessGraphStageError;
            localReflRadianceHandle =
                LocalReflectionRadiancePass::AddToGraph(*m_services.renderGraph, localRadianceContext);
        }

        if (wantsReflectionResolverV3ThisFrame && localReflRadianceHandle.IsValid()) {
            FullSceneReflectionResolverV3Context reflectionContext{};
            reflectionContext.localReflectionRadiance = localReflRadianceHandle;
            reflectionContext.ssr = ssrHandle;
            reflectionContext.rtReflection = rtReflHandle;
            reflectionContext.historyPrevSourceId = reflectionHistoryPrevSourceIdHandle;
            reflectionContext.historyValidity = reflectionHistoryValidityHandle;
            reflectionContext.historyRejection = reflectionHistoryRejectionHandle;
            reflectionContext.normalRoughness = normalHandle;
            reflectionContext.emissiveMetallic = emissiveMetallicHandle;
            reflectionContext.materialExt2 = materialExt2Handle;
            reflectionContext.radiance = reflectionRadianceHandle;
            reflectionContext.confidence = reflectionConfidenceHandle;
            reflectionContext.sourceId = reflectionSourceIdHandle;
            reflectionContext.rejectedSourceMask = reflectionRejectedSourceMaskHandle;
            reflectionContext.temporalDelta = reflectionTemporalDeltaHandle;
            reflectionContext.ssrSourceSignal = reflectionSSRSourceSignalHandle;
            reflectionContext.rtSourceSignal = reflectionRTSourceSignalHandle;
            reflectionContext.sourceSuppression = reflectionSourceSuppressionHandle;
            reflectionContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
            reflectionContext.descriptorManager = m_services.descriptorManager.get();
            reflectionContext.commandList = m_commandResources.graphicsList.Get();
            reflectionContext.rootSignature = m_pipelineState.rootSignature.get();
            reflectionContext.pipeline = m_pipelineState.fullSceneReflectionResolverV3.get();
            reflectionContext.frameConstants = m_constantBuffers.currentFrameGPU;
            reflectionContext.outputRTVs = {
                m_mainTargets.reflectionV3.descriptors.radianceRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.confidenceRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.sourceIdRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.rejectedSourceMaskRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.temporalDeltaRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.ssrSourceSignalRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.rtSourceSignalRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.sourceSuppressionRTV.cpu,
            };
            reflectionContext.width = GetInternalRenderWidth();
            reflectionContext.height = GetInternalRenderHeight();
            reflectionContext.ran = &ranReflectionResolverV3;
            reflectionContext.failed = &bloomStageFailed;
            reflectionContext.stage = &postProcessGraphStageError;
            if (!AddFullSceneReflectionResolverV3Pass(*m_services.renderGraph, reflectionContext)) {
                bloomStageFailed = true;
            } else {
                scheduledReflectionResolverV3 = true;
            }
        }

        if (wantsReflectionHistoryV3ThisFrame && scheduledReflectionResolverV3) {
            FullSceneReflectionHistoryV3Context historyContext{};
            historyContext.radiance = reflectionRadianceHandle;
            historyContext.sourceId = reflectionSourceIdHandle;
            historyContext.temporalDelta = reflectionTemporalDeltaHandle;
            historyContext.historyPrev = reflectionHistoryPrevHandle;
            historyContext.historyPrevSourceId = reflectionHistoryPrevSourceIdHandle;
            historyContext.depth = depthPpHandle;
            historyContext.normalRoughness = normalHandle;
            historyContext.velocity = velocityHandle;
            historyContext.historyCurr = reflectionHistoryCurrHandle;
            historyContext.historyValidity = reflectionHistoryValidityHandle;
            historyContext.historyRejection = reflectionHistoryRejectionHandle;
            historyContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
            historyContext.descriptorManager = m_services.descriptorManager.get();
            historyContext.commandList = m_commandResources.graphicsList.Get();
            historyContext.rootSignature = m_pipelineState.rootSignature.get();
            historyContext.pipeline = m_pipelineState.fullSceneReflectionHistoryV3.get();
            historyContext.frameConstants = m_constantBuffers.currentFrameGPU;
            historyContext.outputRTVs = {
                m_mainTargets.reflectionV3.descriptors.historyCurrRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.historyValidityRTV.cpu,
                m_mainTargets.reflectionV3.descriptors.historyRejectionRTV.cpu,
            };
            historyContext.width = GetInternalRenderWidth();
            historyContext.height = GetInternalRenderHeight();
            historyContext.ran = &ranReflectionHistoryV3;
            historyContext.failed = &bloomStageFailed;
            historyContext.stage = &postProcessGraphStageError;
            if (!AddFullSceneReflectionHistoryV3Pass(*m_services.renderGraph, historyContext)) {
                bloomStageFailed = true;
            } else {
                FullSceneReflectionHistoryV3CopyContext historyCopyContext{};
                historyCopyContext.historyCurr = reflectionHistoryCurrHandle;
                historyCopyContext.historyPrev = reflectionHistoryPrevHandle;
                historyCopyContext.sourceId = reflectionSourceIdHandle;
                historyCopyContext.historyPrevSourceId = reflectionHistoryPrevSourceIdHandle;
                historyCopyContext.ran = &ranReflectionHistoryV3Copy;
                historyCopyContext.failed = &bloomStageFailed;
                historyCopyContext.stage = &postProcessGraphStageError;
                if (!AddFullSceneReflectionHistoryV3CopyPass(*m_services.renderGraph, historyCopyContext)) {
                    bloomStageFailed = true;
                }
            }
        }

        const PostProcessGraphPass::ResourceHandles postProcessResources{
            hdrHandle,
            bloomHandle,
            ssaoHandle,
            historyHandle,
            depthPpHandle,
            normalHandle,
            emissiveMetallicHandle,
            materialExt1Handle,
            materialExt2Handle,
            ssrHandle,
            velocityHandle,
            taaHandle,
            rtReflHandle,
            rtReflHistHandle,
            localReflRadianceHandle,
            hzbHandle,
            backBufferHandle,
            wantsHzbDebug,
        };

        PostProcessGraphPass::ExecuteContext executeContext{};
        executeContext.useBloomOverride = wantsFusedBloomThisFrame && bloomHandle.IsValid();
        executeContext.bloom = bloomHandle;
        executeContext.localReflectionRadiance = localReflRadianceHandle;
        auto& postTable = m_temporalScreenState.postProcessSrvTables[m_frameRuntime.frameIndex % kFrameCount];
        executeContext.descriptorUpdate.device = m_services.device ? m_services.device->GetDevice() : nullptr;
        executeContext.descriptorUpdate.srvTable = std::span<DescriptorHandle>(postTable.data(), postTable.size());
        executeContext.descriptorUpdate.hdr = m_mainTargets.hdr.resources.color.Get();
        executeContext.descriptorUpdate.bloomIntensity = m_bloomResources.controls.intensity;
        executeContext.descriptorUpdate.bloomFallback = (m_bloomResources.resources.activeLevels > 1)
            ? m_bloomResources.resources.texA[1].Get()
            : m_bloomResources.resources.texA[0].Get();
        executeContext.descriptorUpdate.ssao = m_ssaoResources.resources.texture.Get();
        executeContext.descriptorUpdate.history = m_temporalScreenState.historyColor.Get();
        executeContext.descriptorUpdate.depth = m_depthResources.resources.buffer.Get();
        executeContext.descriptorUpdate.normalRoughness = postNormalResource;
        executeContext.descriptorUpdate.hzb = m_hzbResources.resources.texture.Get();
        executeContext.descriptorUpdate.hzbMipCount = m_hzbResources.resources.mipCount;
        executeContext.descriptorUpdate.wantsHzbDebug = wantsHzbDebug;
        executeContext.descriptorUpdate.ssr = m_ssrResources.resources.color.Get();
        executeContext.descriptorUpdate.velocity = m_temporalScreenState.velocityBuffer.Get();
        executeContext.descriptorUpdate.rtReflection = m_rtReflectionTargets.color.Get();
        executeContext.descriptorUpdate.rtReflectionHistory = m_rtReflectionTargets.history.Get();
        executeContext.descriptorUpdate.emissiveMetallic = postEmissiveMetallicResource;
        executeContext.descriptorUpdate.materialExt1 = postMaterialExt1Resource;
        executeContext.descriptorUpdate.materialExt2 = postMaterialExt2Resource;
        executeContext.descriptorUpdate.localReflectionRadiance = nullptr;
        executeContext.draw.commandList = m_commandResources.graphicsList.Get();
        executeContext.draw.descriptorManager = m_services.descriptorManager.get();
        executeContext.draw.rootSignature = m_pipelineState.rootSignature.get();
        executeContext.draw.frameConstants = m_constantBuffers.currentFrameGPU;
        executeContext.draw.pipeline = m_pipelineState.postProcess.get();
        executeContext.draw.width = m_services.window->GetWidth();
        executeContext.draw.height = m_services.window->GetHeight();
        executeContext.draw.targetRtv = m_services.window->GetCurrentRTV();
        executeContext.draw.srvTable = std::span<DescriptorHandle>(postTable.data(), postTable.size());
        executeContext.draw.shadowAndEnvironmentTable = m_environmentState.shadowAndEnvDescriptors[0];
        executeContext.backBufferUsedAsRenderTarget = &m_frameLifecycle.backBufferUsedAsRTThisFrame;
        if (m_rtReflectionTargets.color) {
            static bool s_checkedRtReflPostClear = false;
            static int s_rtReflPostClearMode = 0;
            if (!s_checkedRtReflPostClear) {
                s_checkedRtReflPostClear = true;
                if (const char* mode = std::getenv("CORTEX_RTREFL_CLEAR")) {
                    s_rtReflPostClearMode = std::atoi(mode);
                    if (s_rtReflPostClearMode != 0) {
                        spdlog::warn("Renderer: CORTEX_RTREFL_CLEAR={} set; post-process graph will clear RT reflection buffer for debug view validation",
                                     s_rtReflPostClearMode);
                    }
                }
            }
            const bool rtReflDebugView =
                (m_debugViewState.mode == 20u || m_debugViewState.mode == 30u || m_debugViewState.mode == 31u);
            executeContext.runRtReflectionDebugClear =
                rtReflDebugView && s_rtReflPostClearMode != 0 && m_services.descriptorManager &&
                m_services.device && m_rtReflectionTargets.uav.IsValid() &&
                m_rtReflectionTargets.postClearUAVs[m_frameRuntime.frameIndex % kFrameCount].IsValid();
            if (executeContext.runRtReflectionDebugClear) {
                executeContext.rtReflectionDebugClear.commandList = m_commandResources.graphicsList.Get();
                executeContext.rtReflectionDebugClear.device = m_services.device->GetDevice();
                executeContext.rtReflectionDebugClear.descriptorHeap = m_services.descriptorManager->GetCBV_SRV_UAV_Heap();
                executeContext.rtReflectionDebugClear.reflectionColor = m_rtReflectionTargets.color.Get();
                executeContext.rtReflectionDebugClear.reflectionState = &m_rtReflectionTargets.colorState;
                executeContext.rtReflectionDebugClear.shaderVisibleUav =
                    m_rtReflectionTargets.postClearUAVs[m_frameRuntime.frameIndex % kFrameCount];
                executeContext.rtReflectionDebugClear.cpuUav = m_rtReflectionTargets.uav;
                executeContext.rtReflectionDebugClear.clearMode = s_rtReflPostClearMode;
            }
        }
        executeContext.status.failed = &bloomStageFailed;
        executeContext.status.stage = &postProcessGraphStageError;
        executeContext.ranPostProcess = &result.ranPostProcess;

        if (wantsCompositeV3ThisFrame &&
            candidateHdrSceneColorHandle.IsValid() &&
            energyClampPolicyHandle.IsValid() &&
            overbrightDiagnosticsHandle.IsValid() &&
            compositeContributionMapHandle.IsValid() &&
            legacyRescueUsageHandle.IsValid() &&
            albedoHandle.IsValid() &&
            sceneLocalEnvironmentHandle.IsValid()) {
            FullSceneCompositeV3Context compositeContext{};
            compositeContext.directLighting = v3DirectLightingHandle;
            compositeContext.indirectLighting = v3IndirectLightingHandle;
            compositeContext.shadowVisibility = v3ShadowVisibilityHandle;
            compositeContext.legacyHdr = hdrHandle;
            compositeContext.localReflectionRadiance = scheduledReflectionResolverV3 && reflectionRadianceHandle.IsValid()
                ? reflectionRadianceHandle
                : localReflRadianceHandle;
            compositeContext.reflectionConfidence = scheduledReflectionResolverV3 && reflectionConfidenceHandle.IsValid()
                ? reflectionConfidenceHandle
                : RGResourceHandle{};
            compositeContext.materialAlbedo = albedoHandle;
            compositeContext.sceneLocalEnvironment = sceneLocalEnvironmentHandle;
            compositeContext.output = candidateHdrSceneColorHandle;
            compositeContext.energyClampPolicy = energyClampPolicyHandle;
            compositeContext.overbrightDiagnostics = overbrightDiagnosticsHandle;
            compositeContext.compositeContributionMap = compositeContributionMapHandle;
            compositeContext.legacyRescueUsage = legacyRescueUsageHandle;
            compositeContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
            compositeContext.descriptorManager = m_services.descriptorManager.get();
            compositeContext.commandList = m_commandResources.graphicsList.Get();
            compositeContext.rootSignature = m_pipelineState.rootSignature.get();
            compositeContext.pipeline = m_pipelineState.fullSceneCompositeV3.get();
            compositeContext.frameConstants = m_constantBuffers.currentFrameGPU;
            compositeContext.directLightingSRV = m_mainTargets.lightingV3.descriptors.directLightingSRV;
            compositeContext.indirectLightingSRV = m_mainTargets.lightingV3.descriptors.indirectLightingSRV;
            compositeContext.shadowVisibilitySRV = m_mainTargets.lightingV3.descriptors.shadowVisibilitySRV;
            compositeContext.legacyHdrSRV = m_mainTargets.hdr.descriptors.srv;
            compositeContext.sceneLocalEnvironmentSRV =
                m_mainTargets.environmentV3.descriptors.sceneLocalEnvironmentSRV;
            compositeContext.outputRTVs = {
                m_mainTargets.compositeV3.descriptors.hdrSceneColorRTV.cpu,
                m_mainTargets.compositeV3.descriptors.energyClampPolicyRTV.cpu,
                m_mainTargets.compositeV3.descriptors.overbrightDiagnosticsRTV.cpu,
                m_mainTargets.compositeV3.descriptors.compositeContributionMapRTV.cpu,
                m_mainTargets.compositeV3.descriptors.legacyRescueUsageRTV.cpu,
            };
            compositeContext.width = GetInternalRenderWidth();
            compositeContext.height = GetInternalRenderHeight();
            compositeContext.ran = &result.ranCompositeV3;
            compositeContext.failed = &bloomStageFailed;
            compositeContext.stage = &postProcessGraphStageError;
            if (!AddFullSceneCompositeV3Pass(*m_services.renderGraph, compositeContext)) {
                bloomStageFailed = true;
            }
        }

        if (candidateBeautyHandle.IsValid()) {
            PostProcessGraphPass::ResourceHandles candidateBeautyResources = postProcessResources;
            if (candidateHdrSceneColorHandle.IsValid()) {
                candidateBeautyResources.hdr = candidateHdrSceneColorHandle;
            }
            candidateBeautyResources.backBuffer = candidateBeautyHandle;

            PostProcessGraphPass::ExecuteContext candidateExecuteContext = executeContext;
            if (candidateHdrSceneColorHandle.IsValid()) {
                candidateExecuteContext.descriptorUpdate.hdr =
                    m_mainTargets.compositeV3.resources.hdrSceneColor.Get();
            }
            candidateExecuteContext.draw.targetRtv = m_mainTargets.candidateBeautyV3.descriptors.ldrOutputRTV.cpu;
            candidateExecuteContext.draw.width = GetInternalRenderWidth();
            candidateExecuteContext.draw.height = GetInternalRenderHeight();
            candidateExecuteContext.backBufferUsedAsRenderTarget = nullptr;
            candidateExecuteContext.runRtReflectionDebugClear = false;
            candidateExecuteContext.ranPostProcess = &result.ranCandidateBeauty;

            PostProcessGraphPass::GraphContext candidateBeautyContext{};
            candidateBeautyContext.passName = candidateHdrSceneColorHandle.IsValid() ? "CinematicPostV3" : "FullSceneCandidateBeautyV3";
            candidateBeautyContext.resources = candidateBeautyResources;
            candidateBeautyContext.execute = candidateExecuteContext;
            candidateBeautyContext.status.failed = &bloomStageFailed;
            candidateBeautyContext.status.stage = &postProcessGraphStageError;
            const RGResourceHandle candidateBeautyResult =
                PostProcessGraphPass::AddToGraph(*m_services.renderGraph, candidateBeautyContext);
            if (!candidateBeautyResult.IsValid()) {
                bloomStageFailed = true;
            }
        }

        PostProcessGraphPass::GraphContext postProcessContext{};
        postProcessContext.resources = postProcessResources;
        postProcessContext.execute = executeContext;
        postProcessContext.status.failed = &bloomStageFailed;
        postProcessContext.status.stage = &postProcessGraphStageError;
        const RGResourceHandle postProcessResult =
            PostProcessGraphPass::AddToGraph(*m_services.renderGraph, postProcessContext);
        if (!postProcessResult.IsValid()) {
            bloomStageFailed = true;
        }

        if (wantsCandidateBeautyDisplayThisFrame && candidateBeautyHandle.IsValid()) {
            CandidateBeautyDisplayContext displayContext{};
            displayContext.passName = "FullSceneCandidateBeautyV3Display";
            displayContext.candidate = candidateBeautyHandle;
            displayContext.backBuffer = backBufferHandle;
            displayContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
            displayContext.descriptorManager = m_services.descriptorManager.get();
            displayContext.commandList = m_commandResources.graphicsList.Get();
            displayContext.rootSignature = m_pipelineState.rootSignature.get();
            displayContext.pipeline = m_pipelineState.candidateBeautyDisplay.get();
            displayContext.frameConstants = m_constantBuffers.currentFrameGPU;
            displayContext.candidateSRV = m_mainTargets.candidateBeautyV3.descriptors.ldrOutputSRV;
            displayContext.backBufferRTV = m_services.window->GetCurrentRTV();
            displayContext.width = m_services.window->GetWidth();
            displayContext.height = m_services.window->GetHeight();
            displayContext.ran = &result.ranCandidateBeautyDisplay;
            displayContext.failed = &bloomStageFailed;
            displayContext.stage = &postProcessGraphStageError;
            if (!AddCandidateBeautyDisplayPass(*m_services.renderGraph, displayContext)) {
                bloomStageFailed = true;
            }
        }

        if (wantsCompositeV3DebugViewThisFrame && candidateHdrSceneColorHandle.IsValid()) {
            RGResourceHandle compositeDebugHandle = candidateHdrSceneColorHandle;
            DescriptorHandle compositeDebugSRV = m_mainTargets.compositeV3.descriptors.hdrSceneColorSRV;
            if (m_debugViewState.mode == 80u && energyClampPolicyHandle.IsValid()) {
                compositeDebugHandle = energyClampPolicyHandle;
                compositeDebugSRV = m_mainTargets.compositeV3.descriptors.energyClampPolicySRV;
            } else if (m_debugViewState.mode == 81u && overbrightDiagnosticsHandle.IsValid()) {
                compositeDebugHandle = overbrightDiagnosticsHandle;
                compositeDebugSRV = m_mainTargets.compositeV3.descriptors.overbrightDiagnosticsSRV;
            } else if (m_debugViewState.mode == 88u && compositeContributionMapHandle.IsValid()) {
                compositeDebugHandle = compositeContributionMapHandle;
                compositeDebugSRV = m_mainTargets.compositeV3.descriptors.compositeContributionMapSRV;
            } else if (m_debugViewState.mode == 89u && legacyRescueUsageHandle.IsValid()) {
                compositeDebugHandle = legacyRescueUsageHandle;
                compositeDebugSRV = m_mainTargets.compositeV3.descriptors.legacyRescueUsageSRV;
            }
            CandidateBeautyDisplayContext debugContext{};
            debugContext.passName = "FullSceneCompositeV3DebugView";
            debugContext.candidate = compositeDebugHandle;
            debugContext.backBuffer = backBufferHandle;
            debugContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
            debugContext.descriptorManager = m_services.descriptorManager.get();
            debugContext.commandList = m_commandResources.graphicsList.Get();
            debugContext.rootSignature = m_pipelineState.rootSignature.get();
            debugContext.pipeline = m_pipelineState.candidateBeautyDisplay.get();
            debugContext.frameConstants = m_constantBuffers.currentFrameGPU;
            debugContext.candidateSRV = compositeDebugSRV;
            debugContext.backBufferRTV = m_services.window->GetCurrentRTV();
            debugContext.width = m_services.window->GetWidth();
            debugContext.height = m_services.window->GetHeight();
            debugContext.ran = &result.ranCompositeV3DebugView;
            debugContext.failed = &bloomStageFailed;
            debugContext.stage = &postProcessGraphStageError;
            if (!AddCandidateBeautyDisplayPass(*m_services.renderGraph, debugContext)) {
                bloomStageFailed = true;
            }
        }

        if (wantsSceneLocalEnvironmentV3DebugViewThisFrame) {
            RGResourceHandle environmentDebugHandle{};
            DescriptorHandle environmentDebugSRV{};
            const char* debugPassName = "SceneLocalEnvironmentV3DebugView";
            switch (m_debugViewState.mode) {
            case 83u:
                environmentDebugHandle = sceneLocalEnvironmentHandle;
                environmentDebugSRV = m_mainTargets.environmentV3.descriptors.sceneLocalEnvironmentSRV;
                debugPassName = "SceneLocalEnvironmentV3AggregateDebugView";
                break;
            case 84u:
                environmentDebugHandle = ambientLightingHandle;
                environmentDebugSRV = m_mainTargets.environmentV3.descriptors.ambientLightingSRV;
                debugPassName = "SceneLocalEnvironmentV3AmbientLightingDebugView";
                break;
            case 85u:
                environmentDebugHandle = visibleBackgroundHandle;
                environmentDebugSRV = m_mainTargets.environmentV3.descriptors.visibleBackgroundSRV;
                debugPassName = "SceneLocalEnvironmentV3VisibleBackgroundDebugView";
                break;
            case 86u:
                environmentDebugHandle = reflectionBackgroundHandle;
                environmentDebugSRV = m_mainTargets.environmentV3.descriptors.reflectionBackgroundSRV;
                debugPassName = "SceneLocalEnvironmentV3ReflectionBackgroundDebugView";
                break;
            case 87u:
                environmentDebugHandle = atmosphereHandle;
                environmentDebugSRV = m_mainTargets.environmentV3.descriptors.atmosphereSRV;
                debugPassName = "SceneLocalEnvironmentV3AtmosphereDebugView";
                break;
            default:
                break;
            }

            if (environmentDebugHandle.IsValid() && environmentDebugSRV.IsValid()) {
                CandidateBeautyDisplayContext environmentDebugContext{};
                environmentDebugContext.passName = debugPassName;
                environmentDebugContext.candidate = environmentDebugHandle;
                environmentDebugContext.backBuffer = backBufferHandle;
                environmentDebugContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
                environmentDebugContext.descriptorManager = m_services.descriptorManager.get();
                environmentDebugContext.commandList = m_commandResources.graphicsList.Get();
                environmentDebugContext.rootSignature = m_pipelineState.rootSignature.get();
                environmentDebugContext.pipeline = m_pipelineState.candidateBeautyDisplay.get();
                environmentDebugContext.frameConstants = m_constantBuffers.currentFrameGPU;
                environmentDebugContext.candidateSRV = environmentDebugSRV;
                environmentDebugContext.backBufferRTV = m_services.window->GetCurrentRTV();
                environmentDebugContext.width = m_services.window->GetWidth();
                environmentDebugContext.height = m_services.window->GetHeight();
                environmentDebugContext.ran = &ranSceneLocalEnvironmentV3DebugView;
                environmentDebugContext.failed = &bloomStageFailed;
                environmentDebugContext.stage = &postProcessGraphStageError;
                if (!AddCandidateBeautyDisplayPass(*m_services.renderGraph, environmentDebugContext)) {
                    bloomStageFailed = true;
                }
            }
        }

        if (wantsReflectionResolverV3DebugViewThisFrame && scheduledReflectionResolverV3) {
            RGResourceHandle debugSource{};
            DescriptorHandle debugSRV{};
            const char* debugPassName = "FullSceneReflectionV3DebugView";
            switch (m_debugViewState.mode) {
            case 68u:
                debugSource = reflectionRadianceHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.radianceSRV;
                debugPassName = "FullSceneReflectionV3RadianceDebugView";
                break;
            case 69u:
                debugSource = reflectionConfidenceHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.confidenceSRV;
                debugPassName = "FullSceneReflectionV3ConfidenceDebugView";
                break;
            case 70u:
                debugSource = reflectionSourceIdHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.sourceIdSRV;
                debugPassName = "FullSceneReflectionV3SourceIdDebugView";
                break;
            case 71u:
                debugSource = reflectionRejectedSourceMaskHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.rejectedSourceMaskSRV;
                debugPassName = "FullSceneReflectionV3RejectedSourceMaskDebugView";
                break;
            case 72u:
                debugSource = reflectionTemporalDeltaHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.temporalDeltaSRV;
                debugPassName = "FullSceneReflectionV3TemporalDeltaDebugView";
                break;
            case 73u:
                debugSource = reflectionSSRSourceSignalHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.ssrSourceSignalSRV;
                debugPassName = "FullSceneReflectionV3SSRSourceSignalDebugView";
                break;
            case 74u:
                debugSource = reflectionRTSourceSignalHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.rtSourceSignalSRV;
                debugPassName = "FullSceneReflectionV3RTSourceSignalDebugView";
                break;
            case 75u:
                debugSource = reflectionHistoryCurrHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.historyCurrSRV;
                debugPassName = "FullSceneReflectionHistoryV3CurrDebugView";
                break;
            case 76u:
                debugSource = reflectionHistoryValidityHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.historyValiditySRV;
                debugPassName = "FullSceneReflectionHistoryV3ValidityDebugView";
                break;
            case 77u:
                debugSource = reflectionHistoryPrevHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.historyPrevSRV;
                debugPassName = "FullSceneReflectionHistoryV3PrevDebugView";
                break;
            case 78u:
                debugSource = reflectionHistoryRejectionHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.historyRejectionSRV;
                debugPassName = "FullSceneReflectionHistoryV3RejectionDebugView";
                break;
            case 79u:
                debugSource = reflectionSourceSuppressionHandle;
                debugSRV = m_mainTargets.reflectionV3.descriptors.sourceSuppressionSRV;
                debugPassName = "FullSceneReflectionV3SourceSuppressionDebugView";
                break;
            default:
                break;
            }

            if (debugSource.IsValid() && debugSRV.IsValid()) {
                CandidateBeautyDisplayContext reflectionDebugContext{};
                reflectionDebugContext.passName = debugPassName;
                reflectionDebugContext.candidate = debugSource;
                reflectionDebugContext.backBuffer = backBufferHandle;
                reflectionDebugContext.device = m_services.device ? m_services.device->GetDevice() : nullptr;
                reflectionDebugContext.descriptorManager = m_services.descriptorManager.get();
                reflectionDebugContext.commandList = m_commandResources.graphicsList.Get();
                reflectionDebugContext.rootSignature = m_pipelineState.rootSignature.get();
                reflectionDebugContext.pipeline = m_pipelineState.candidateBeautyDisplay.get();
                reflectionDebugContext.frameConstants = m_constantBuffers.currentFrameGPU;
                reflectionDebugContext.candidateSRV = debugSRV;
                reflectionDebugContext.backBufferRTV = m_services.window->GetCurrentRTV();
                reflectionDebugContext.width = m_services.window->GetWidth();
                reflectionDebugContext.height = m_services.window->GetHeight();
                reflectionDebugContext.ran = &ranReflectionResolverV3DebugView;
                reflectionDebugContext.failed = &bloomStageFailed;
                reflectionDebugContext.stage = &postProcessGraphStageError;
                if (!AddCandidateBeautyDisplayPass(*m_services.renderGraph, reflectionDebugContext)) {
                    bloomStageFailed = true;
                }
            }
        }
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats();
    Debug::GPUProfiler::Get().EndScope(m_commandResources.graphicsList.Get());

    if (execResult.IsErr()) {
        result.fallbackUsed = true;
        result.fallbackReason = execResult.Error();
        spdlog::warn("RenderGraph end-of-frame: Execute failed: {}", result.fallbackReason);
        RecordFramePass("RenderGraphEndFrame", true, false, 0,
                        {"depth", "hdr_color", "ssao", "ssr_color", "bloom", "taa_history", "velocity", "rt_reflection", "hzb"},
                        {"hzb", "back_buffer"},
                        true,
                        result.fallbackReason.c_str());
        result.ranBloom = false;
        m_services.renderGraph->EndFrame();
        return result;
    }

    if (wantsFusedBloomThisFrame && bloomStageFailed) {
        result.fallbackUsed = true;
        result.fallbackReason = "fused_bloom_graph_stage_failed";
        if (bloomGraphStageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += bloomGraphStageError;
        } else if (postProcessGraphStageError) {
            result.fallbackReason += ": ";
            result.fallbackReason += postProcessGraphStageError;
        }
        result.ranBloom = false;
        result.ranPostProcess = false;
        spdlog::warn("RenderGraph end-of-frame: {} (graph path did not execute)", result.fallbackReason);
    }

    static bool s_loggedHzbRg = false;
    if (!s_loggedHzbRg && wantsRgHzbThisFrame) {
        s_loggedHzbRg = true;
        spdlog::info("HZB RG: passes={}, barriers={}",
                     m_services.renderGraph->GetPassCount(), m_services.renderGraph->GetBarrierCount());
    }

    if (wantsRgHzbThisFrame) {
        m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthHandle);
        m_hzbResources.resources.resourceState = m_services.renderGraph->GetResourceState(hzbHandle);
        m_hzbResources.resources.valid = true;

        m_hzbResources.capture.captureViewMatrix = m_constantBuffers.frameCPU.viewMatrix;
        m_hzbResources.capture.captureViewProjMatrix = m_constantBuffers.frameCPU.viewProjectionMatrix;
        m_hzbResources.capture.captureCameraPosWS = m_cameraState.positionWS;
        m_hzbResources.capture.captureCameraForwardWS = glm::normalize(m_cameraState.forwardWS);
        m_hzbResources.capture.captureNearPlane = m_cameraState.nearPlane;
        m_hzbResources.capture.captureFarPlane = m_cameraState.farPlane;
        m_hzbResources.capture.captureFrameCounter = m_frameLifecycle.renderFrameCounter;
        m_hzbResources.capture.captureValid = true;
        result.ranHZB = true;
        RecordFramePass("HZB", true, true, 0,
                        {"depth"},
                        {"hzb"},
                        false,
                        nullptr,
                        true);
    }

    if (wantsRgPostThisFrame) {
        m_mainTargets.hdr.resources.state = m_services.renderGraph->GetResourceState(hdrHandle);
        if (bloomHandle.IsValid() && !wantsFusedBloomThisFrame) {
            const uint32_t level = (m_bloomResources.resources.activeLevels > 1) ? 1u : 0u;
            m_bloomResources.resources.resourceState[level][0] = m_services.renderGraph->GetResourceState(bloomHandle);
        }
        if (ssaoHandle.IsValid()) m_ssaoResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssaoHandle);
        if (ssrHandle.IsValid()) m_ssrResources.resources.resourceState = m_services.renderGraph->GetResourceState(ssrHandle);
        if (historyHandle.IsValid()) m_temporalScreenState.historyState = m_services.renderGraph->GetResourceState(historyHandle);
        if (depthPpHandle.IsValid()) m_depthResources.resources.resourceState = m_services.renderGraph->GetResourceState(depthPpHandle);
        if (!m_visibilityBufferState.renderedThisFrame && normalHandle.IsValid()) {
            m_mainTargets.normalRoughness.resources.state = m_services.renderGraph->GetResourceState(normalHandle);
        }
        if (hasVBPostStates) {
            auto finalStates = m_services.visibilityBuffer->GetResourceStateSnapshot();
            if (albedoHandle.IsValid()) {
                finalStates.albedo = m_services.renderGraph->GetResourceState(albedoHandle);
            }
            if (normalHandle.IsValid()) {
                finalStates.normalRoughness = m_services.renderGraph->GetResourceState(normalHandle);
            }
            if (emissiveMetallicHandle.IsValid()) {
                finalStates.emissiveMetallic = m_services.renderGraph->GetResourceState(emissiveMetallicHandle);
            }
            if (materialExt1Handle.IsValid()) {
                finalStates.materialExt1 = m_services.renderGraph->GetResourceState(materialExt1Handle);
            }
            if (materialExt2Handle.IsValid()) {
                finalStates.materialExt2 = m_services.renderGraph->GetResourceState(materialExt2Handle);
            }
            m_services.visibilityBuffer->ApplyResourceStateSnapshot(finalStates);
        }
        if (velocityHandle.IsValid()) m_temporalScreenState.velocityState = m_services.renderGraph->GetResourceState(velocityHandle);
        if (taaHandle.IsValid()) m_temporalScreenState.taaIntermediateState = m_services.renderGraph->GetResourceState(taaHandle);
        if (rtReflHandle.IsValid()) m_rtReflectionTargets.colorState = m_services.renderGraph->GetResourceState(rtReflHandle);
        if (rtReflHistHandle.IsValid()) m_rtReflectionTargets.historyState = m_services.renderGraph->GetResourceState(rtReflHistHandle);
        if (candidateBeautyHandle.IsValid()) {
            m_mainTargets.candidateBeautyV3.resources.state =
                m_services.renderGraph->GetResourceState(candidateBeautyHandle);
        }
        if (candidateHdrSceneColorHandle.IsValid()) {
            m_mainTargets.compositeV3.resources.hdrSceneColorState =
                m_services.renderGraph->GetResourceState(candidateHdrSceneColorHandle);
            m_mainTargets.compositeV3.resources.state =
                m_mainTargets.compositeV3.resources.hdrSceneColorState;
        }
        if (energyClampPolicyHandle.IsValid()) {
            m_mainTargets.compositeV3.resources.energyClampPolicyState =
                m_services.renderGraph->GetResourceState(energyClampPolicyHandle);
        }
        if (overbrightDiagnosticsHandle.IsValid()) {
            m_mainTargets.compositeV3.resources.overbrightDiagnosticsState =
                m_services.renderGraph->GetResourceState(overbrightDiagnosticsHandle);
        }
        if (compositeContributionMapHandle.IsValid()) {
            m_mainTargets.compositeV3.resources.compositeContributionMapState =
                m_services.renderGraph->GetResourceState(compositeContributionMapHandle);
        }
        if (legacyRescueUsageHandle.IsValid()) {
            m_mainTargets.compositeV3.resources.legacyRescueUsageState =
                m_services.renderGraph->GetResourceState(legacyRescueUsageHandle);
        }
        if (sceneLocalEnvironmentHandle.IsValid()) {
            m_mainTargets.environmentV3.resources.sceneLocalEnvironmentState =
                m_services.renderGraph->GetResourceState(sceneLocalEnvironmentHandle);
            m_mainTargets.environmentV3.resources.state =
                m_mainTargets.environmentV3.resources.sceneLocalEnvironmentState;
        }
        if (ambientLightingHandle.IsValid()) {
            m_mainTargets.environmentV3.resources.ambientLightingState =
                m_services.renderGraph->GetResourceState(ambientLightingHandle);
        }
        if (visibleBackgroundHandle.IsValid()) {
            m_mainTargets.environmentV3.resources.visibleBackgroundState =
                m_services.renderGraph->GetResourceState(visibleBackgroundHandle);
        }
        if (reflectionBackgroundHandle.IsValid()) {
            m_mainTargets.environmentV3.resources.reflectionBackgroundState =
                m_services.renderGraph->GetResourceState(reflectionBackgroundHandle);
        }
        if (atmosphereHandle.IsValid()) {
            m_mainTargets.environmentV3.resources.atmosphereState =
                m_services.renderGraph->GetResourceState(atmosphereHandle);
        }
        if (reflectionRadianceHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.radianceState =
                m_services.renderGraph->GetResourceState(reflectionRadianceHandle);
            m_mainTargets.reflectionV3.resources.state =
                m_mainTargets.reflectionV3.resources.radianceState;
        }
        if (reflectionConfidenceHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.confidenceState =
                m_services.renderGraph->GetResourceState(reflectionConfidenceHandle);
        }
        if (reflectionSourceIdHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.sourceIdState =
                m_services.renderGraph->GetResourceState(reflectionSourceIdHandle);
        }
        if (reflectionRejectedSourceMaskHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.rejectedSourceMaskState =
                m_services.renderGraph->GetResourceState(reflectionRejectedSourceMaskHandle);
        }
        if (reflectionTemporalDeltaHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.temporalDeltaState =
                m_services.renderGraph->GetResourceState(reflectionTemporalDeltaHandle);
        }
        if (reflectionSSRSourceSignalHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.ssrSourceSignalState =
                m_services.renderGraph->GetResourceState(reflectionSSRSourceSignalHandle);
        }
        if (reflectionRTSourceSignalHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.rtSourceSignalState =
                m_services.renderGraph->GetResourceState(reflectionRTSourceSignalHandle);
        }
        if (reflectionSourceSuppressionHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.sourceSuppressionState =
                m_services.renderGraph->GetResourceState(reflectionSourceSuppressionHandle);
        }
        if (reflectionHistoryCurrHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.historyCurrState =
                m_services.renderGraph->GetResourceState(reflectionHistoryCurrHandle);
        }
        if (reflectionHistoryPrevHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.historyPrevState =
                m_services.renderGraph->GetResourceState(reflectionHistoryPrevHandle);
        }
        if (reflectionHistoryPrevSourceIdHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.historyPrevSourceIdState =
                m_services.renderGraph->GetResourceState(reflectionHistoryPrevSourceIdHandle);
        }
        if (reflectionHistoryValidityHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.historyValidityState =
                m_services.renderGraph->GetResourceState(reflectionHistoryValidityHandle);
        }
        if (reflectionHistoryRejectionHandle.IsValid()) {
            m_mainTargets.reflectionV3.resources.historyRejectionState =
                m_services.renderGraph->GetResourceState(reflectionHistoryRejectionHandle);
        }
        if (hzbHandle.IsValid() && (m_debugViewState.mode == 32u)) m_hzbResources.resources.resourceState = m_services.renderGraph->GetResourceState(hzbHandle);
        if (wantsFusedBloomThisFrame && !useFusedBloomTransients) {
            for (uint32_t level = 0; level < m_bloomResources.resources.activeLevels; ++level) {
                if (bloomA[level].IsValid()) {
                    m_bloomResources.resources.resourceState[level][0] = m_services.renderGraph->GetResourceState(bloomA[level]);
                }
                if (bloomB[level].IsValid()) {
                    m_bloomResources.resources.resourceState[level][1] = m_services.renderGraph->GetResourceState(bloomB[level]);
                }
            }
        }
        if (wantsFusedBloomThisFrame && result.ranBloom) {
            RecordFramePass("Bloom", true, true, 1,
                            {"hdr_color"},
                            {"bloom"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsSceneLocalEnvironmentV3ThisFrame) {
            RecordFramePass("SceneLocalEnvironmentV3",
                            true,
                            ranSceneLocalEnvironmentV3,
                            ranSceneLocalEnvironmentV3 ? 1u : 0u,
                            {"frame_constants", "scene_visual_contract", "environment_state"},
                            {"scene_local_environment", "ambient_lighting", "visible_background",
                             "reflection_background", "atmosphere"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsReflectionResolverV3ThisFrame) {
            RecordFramePass("FullSceneReflectionV3",
                            true,
                            ranReflectionResolverV3,
                            ranReflectionResolverV3 ? 1u : 0u,
                            {"local_reflection_radiance", "ssr_color", "rt_reflection",
                             "reflection_history_v3_prev_source_id", "reflection_history_v3_validity",
                             "reflection_history_v3_rejection", inputs.frameNormalRoughnessResource,
                             "vb_gbuffer_emissive_metallic", "vb_gbuffer_material_ext2"},
                            {"reflection_radiance", "reflection_confidence", "reflection_source_id",
                             "reflection_rejected_source_mask", "reflection_temporal_delta",
                             "reflection_ssr_source_signal", "reflection_rt_source_signal",
                             "reflection_source_suppression"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsReflectionHistoryV3ThisFrame) {
            RecordFramePass("FullSceneReflectionHistoryV3",
                            true,
                            ranReflectionHistoryV3,
                            ranReflectionHistoryV3 ? 1u : 0u,
                            {"reflection_radiance", "reflection_source_id", "reflection_temporal_delta",
                             "reflection_history_v3_prev", "reflection_history_v3_prev_source_id",
                             "depth", inputs.frameNormalRoughnessResource, "velocity"},
                            {"reflection_history_v3_curr", "reflection_history_v3_validity",
                             "reflection_history_v3_rejection"},
                            false,
                            nullptr,
                            true);
            RecordFramePass("FullSceneReflectionHistoryV3Copy",
                            true,
                            ranReflectionHistoryV3Copy,
                            ranReflectionHistoryV3Copy ? 1u : 0u,
                            {"reflection_history_v3_curr", "reflection_source_id"},
                            {"reflection_history_v3_prev", "reflection_history_v3_prev_source_id"},
                            false,
                            nullptr,
                            true);
        }
        RecordFramePass("PostProcess", true, result.ranPostProcess, result.ranPostProcess ? 1u : 0u,
                        {"hdr_color", "ssao", "ssr_color", "bloom", "taa_history", "depth",
                         inputs.frameNormalRoughnessResource,
                         "vb_gbuffer_emissive_metallic", "vb_gbuffer_material_ext1",
                         "vb_gbuffer_material_ext2", "velocity", "rt_reflection", "hzb"},
                        {"back_buffer"},
                        false,
                        nullptr,
                        true);
        if (wantsCompositeV3ThisFrame) {
            if (ranReflectionResolverV3 && reflectionRadianceHandle.IsValid()) {
                RecordFramePass("FullSceneCompositeV3",
                                true,
                                result.ranCompositeV3,
                                result.ranCompositeV3 ? 1u : 0u,
                                {"direct_lighting", "indirect_lighting", "shadow_visibility", "hdr_color",
                                 "reflection_radiance", "reflection_confidence", "vb_gbuffer_albedo",
                                 "scene_local_environment"},
                                {"candidate_hdr_scene_color", "energy_clamp_policy", "overbright_diagnostics",
                                 "composite_contribution_map", "legacy_rescue_usage"},
                                false,
                                nullptr,
                                true);
            } else if (localReflRadianceHandle.IsValid()) {
                RecordFramePass("FullSceneCompositeV3",
                                true,
                                result.ranCompositeV3,
                                result.ranCompositeV3 ? 1u : 0u,
                                {"direct_lighting", "indirect_lighting", "shadow_visibility", "hdr_color",
                                 "local_reflection_radiance", "vb_gbuffer_albedo", "scene_local_environment"},
                                {"candidate_hdr_scene_color", "energy_clamp_policy", "overbright_diagnostics",
                                 "composite_contribution_map", "legacy_rescue_usage"},
                                false,
                                nullptr,
                                true);
            } else {
                RecordFramePass("FullSceneCompositeV3",
                                true,
                                result.ranCompositeV3,
                                result.ranCompositeV3 ? 1u : 0u,
                                {"direct_lighting", "indirect_lighting", "shadow_visibility", "hdr_color",
                                 "vb_gbuffer_albedo", "scene_local_environment"},
                                {"candidate_hdr_scene_color", "energy_clamp_policy", "overbright_diagnostics",
                                 "composite_contribution_map", "legacy_rescue_usage"},
                                false,
                                nullptr,
                                true);
            }
        }
        if (wantsCandidateBeautyThisFrame) {
            const bool usedCandidateHdr = wantsCompositeV3ThisFrame && result.ranCompositeV3;
            RecordFramePass(usedCandidateHdr ? "CinematicPostV3" : "FullSceneCandidateBeautyV3",
                            true,
                            result.ranCandidateBeauty,
                            result.ranCandidateBeauty ? 1u : 0u,
                            {usedCandidateHdr ? "candidate_hdr_scene_color" : "hdr_color",
                             "ssao", "ssr_color", "bloom", "taa_history", "depth",
                             inputs.frameNormalRoughnessResource,
                             "vb_gbuffer_emissive_metallic", "vb_gbuffer_material_ext1",
                             "vb_gbuffer_material_ext2", "velocity", "rt_reflection", "hzb"},
                            {"candidate_ldr_cinematic_output"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsCandidateBeautyDisplayThisFrame) {
            RecordFramePass("FullSceneCandidateBeautyV3Display",
                            true,
                            result.ranCandidateBeautyDisplay,
                            result.ranCandidateBeautyDisplay ? 1u : 0u,
                            {"candidate_ldr_cinematic_output"},
                            {"back_buffer"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsCompositeV3DebugViewThisFrame) {
            const char* readResource =
                m_debugViewState.mode == 80u ? "energy_clamp_policy" :
                m_debugViewState.mode == 81u ? "overbright_diagnostics" :
                m_debugViewState.mode == 88u ? "composite_contribution_map" :
                m_debugViewState.mode == 89u ? "legacy_rescue_usage" :
                "candidate_hdr_scene_color";
            RecordFramePass("FullSceneCompositeV3DebugView",
                            true,
                            result.ranCompositeV3DebugView,
                            result.ranCompositeV3DebugView ? 1u : 0u,
                            {readResource},
                            {"back_buffer"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsSceneLocalEnvironmentV3DebugViewThisFrame) {
            const char* readResource =
                m_debugViewState.mode == 84u ? "ambient_lighting" :
                m_debugViewState.mode == 85u ? "visible_background" :
                m_debugViewState.mode == 86u ? "reflection_background" :
                m_debugViewState.mode == 87u ? "atmosphere" :
                "scene_local_environment";
            RecordFramePass("SceneLocalEnvironmentV3DebugView",
                            true,
                            ranSceneLocalEnvironmentV3DebugView,
                            ranSceneLocalEnvironmentV3DebugView ? 1u : 0u,
                            {readResource},
                            {"back_buffer"},
                            false,
                            nullptr,
                            true);
        }
        if (wantsReflectionResolverV3DebugViewThisFrame) {
            const char* readResource =
                m_debugViewState.mode == 68u ? "reflection_radiance" :
                m_debugViewState.mode == 69u ? "reflection_confidence" :
                m_debugViewState.mode == 70u ? "reflection_source_id" :
                m_debugViewState.mode == 71u ? "reflection_rejected_source_mask" :
                m_debugViewState.mode == 72u ? "reflection_temporal_delta" :
                m_debugViewState.mode == 73u ? "reflection_ssr_source_signal" :
                m_debugViewState.mode == 74u ? "reflection_rt_source_signal" :
                m_debugViewState.mode == 75u ? "reflection_history_v3_curr" :
                m_debugViewState.mode == 76u ? "reflection_history_v3_validity" :
                m_debugViewState.mode == 77u ? "reflection_history_v3_prev" :
                m_debugViewState.mode == 78u ? "reflection_history_v3_rejection" :
                m_debugViewState.mode == 79u ? "reflection_source_suppression" :
                "reflection_radiance";
            RecordFramePass("FullSceneReflectionV3DebugView",
                            true,
                            ranReflectionResolverV3DebugView,
                            ranReflectionResolverV3DebugView ? 1u : 0u,
                            {readResource},
                            {"back_buffer"},
                            false,
                            nullptr,
                            true);
        }
    }

    if (wantsCandidateBeautyThisFrame) {
        RecordFramePass("RenderGraphEndFrame", true, true, result.ranPostProcess ? 1u : 0u,
                        {"depth", "hdr_color", "ssao", "ssr_color", "bloom", "taa_history",
                         "velocity", "rt_reflection", "vb_gbuffer_material_ext1",
                         "vb_gbuffer_material_ext2", "hzb"},
                        wantsCompositeV3ThisFrame
                            ? std::initializer_list<const char*>{"hzb", "back_buffer", "candidate_hdr_scene_color", "energy_clamp_policy", "overbright_diagnostics", "composite_contribution_map", "legacy_rescue_usage", "candidate_ldr_cinematic_output", "reflection_history_v3_prev"}
                            : std::initializer_list<const char*>{"hzb", "back_buffer", "candidate_ldr_cinematic_output"});
    } else {
        RecordFramePass("RenderGraphEndFrame", true, true, result.ranPostProcess ? 1u : 0u,
                        {"depth", "hdr_color", "ssao", "ssr_color", "bloom", "taa_history",
                         "velocity", "rt_reflection", "vb_gbuffer_material_ext1",
                         "vb_gbuffer_material_ext2", "hzb"},
                        {"hzb", "back_buffer"});
    }

    m_services.renderGraph->EndFrame();
    return result;
}

} // namespace Cortex::Graphics
