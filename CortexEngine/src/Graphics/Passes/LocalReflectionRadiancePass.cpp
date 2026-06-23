#include "LocalReflectionRadiancePass.h"

#include "DescriptorTable.h"

#include <algorithm>
#include <memory>

namespace Cortex::Graphics::LocalReflectionRadiancePass {

namespace {

void Fail(const GraphContext& context, const char* stage) {
    if (context.status.failed) {
        *context.status.failed = true;
    }
    if (context.status.stage && !*context.status.stage) {
        *context.status.stage = stage ? stage : "local_reflection_radiance_failed";
    }
}

[[nodiscard]] bool IsUsable(const GraphContext& context) {
    return context.resources.depth.IsValid() &&
           context.resources.normalRoughness.IsValid() &&
           context.resources.emissiveMetallic.IsValid() &&
           context.resources.materialExt1.IsValid() &&
           context.resources.materialExt2.IsValid() &&
           context.resources.sceneColor.IsValid() &&
           context.dispatch.device &&
           context.dispatch.descriptorManager &&
           context.dispatch.rootSignature &&
           context.dispatch.pipeline &&
           context.dispatch.pipeline->GetPipelineState() &&
           context.dispatch.frameConstants != 0 &&
           context.dispatch.srvTable.size() >= 8 &&
           !context.dispatch.uavTable.empty() &&
           context.dispatch.width > 0 &&
           context.dispatch.height > 0;
}

[[nodiscard]] RGResourceDesc MakeRadianceDesc(uint32_t width, uint32_t height) {
    return RGResourceDesc::Texture2D(
        width,
        height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        "LocalReflectionRadiance");
}

[[nodiscard]] bool WriteDescriptors(const DispatchContext& context,
                                    ID3D12Resource* output,
                                    ID3D12Resource* depth,
                                    ID3D12Resource* normalRoughness,
                                    ID3D12Resource* emissiveMetallic,
                                    ID3D12Resource* materialExt1,
                                    ID3D12Resource* materialExt2,
                                    ID3D12Resource* sceneColor) {
    return DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[0], depth, DXGI_FORMAT_R32_FLOAT) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[1], normalRoughness, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[2], emissiveMetallic, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[3], materialExt1, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[4], materialExt2, DXGI_FORMAT_R8G8B8A8_UNORM) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[5], sceneColor, DXGI_FORMAT_R16G16B16A16_FLOAT) &&
           DescriptorTable::WriteTexture2DSRV(context.device, context.srvTable[6], context.envSpecular, context.envSpecularFormat) &&
           DescriptorTable::WriteTextureCubeSRV(context.device,
                                                context.srvTable[7],
                                                context.localReflectionCubemap,
                                                context.localReflectionCubemapFormat,
                                                std::max(1u, context.localReflectionCubemapMipLevels)) &&
           DescriptorTable::WriteTexture2DUAV(context.device, context.uavTable[0], output, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

[[nodiscard]] bool Dispatch(ID3D12GraphicsCommandList* commandList,
                            const RenderGraph& graph,
                            const GraphContext& context,
                            RGResourceHandle outputHandle) {
    if (!commandList) {
        return false;
    }

    ID3D12Resource* output = graph.GetResource(outputHandle);
    ID3D12Resource* depth = graph.GetResource(context.resources.depth);
    ID3D12Resource* normalRoughness = graph.GetResource(context.resources.normalRoughness);
    ID3D12Resource* emissiveMetallic = graph.GetResource(context.resources.emissiveMetallic);
    ID3D12Resource* materialExt1 = graph.GetResource(context.resources.materialExt1);
    ID3D12Resource* materialExt2 = graph.GetResource(context.resources.materialExt2);
    ID3D12Resource* sceneColor = graph.GetResource(context.resources.sceneColor);
    if (!output || !depth || !normalRoughness || !emissiveMetallic || !materialExt1 || !materialExt2 || !sceneColor) {
        return false;
    }

    if (!WriteDescriptors(context.dispatch,
                          output,
                          depth,
                          normalRoughness,
                          emissiveMetallic,
                          materialExt1,
                          materialExt2,
                          sceneColor)) {
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = {context.dispatch.descriptorManager->GetCBV_SRV_UAV_Heap()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(context.dispatch.rootSignature);
    commandList->SetPipelineState(context.dispatch.pipeline->GetPipelineState());
    commandList->SetComputeRootConstantBufferView(1, context.dispatch.frameConstants);
    commandList->SetComputeRootDescriptorTable(3, context.dispatch.srvTable[0].gpu);
    commandList->SetComputeRootDescriptorTable(6, context.dispatch.uavTable[0].gpu);

    const UINT dispatchX = std::max<UINT>(1u, static_cast<UINT>((context.dispatch.width + 7u) / 8u));
    const UINT dispatchY = std::max<UINT>(1u, static_cast<UINT>((context.dispatch.height + 7u) / 8u));
    commandList->Dispatch(dispatchX, dispatchY, 1);
    return true;
}

} // namespace

RGResourceHandle AddToGraph(RenderGraph& graph, const GraphContext& context) {
    if (!IsUsable(context)) {
        Fail(context, "local_reflection_radiance_graph_contract");
        return {};
    }

    auto output = std::make_shared<RGResourceHandle>();
    const RGResourceDesc outputDesc = MakeRadianceDesc(context.dispatch.width, context.dispatch.height);

    graph.AddPass(
        "LocalReflectionRadiance",
        [context, output, outputDesc](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Compute);
            *output = builder.CreateTransient(outputDesc);
            builder.Read(context.resources.depth, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Read(context.resources.normalRoughness, RGResourceUsage::ShaderResource);
            builder.Read(context.resources.emissiveMetallic, RGResourceUsage::ShaderResource);
            builder.Read(context.resources.materialExt1, RGResourceUsage::ShaderResource);
            builder.Read(context.resources.materialExt2, RGResourceUsage::ShaderResource);
            builder.Read(context.resources.sceneColor, RGResourceUsage::ShaderResource);
            builder.Write(*output, RGResourceUsage::UnorderedAccess);
        },
        [context, output](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph) {
            if (!Dispatch(commandList, graph, context, *output)) {
                Fail(context, "local_reflection_radiance_dispatch");
                return;
            }
            if (context.status.ran) {
                *context.status.ran = true;
            }
        });

    return *output;
}

} // namespace Cortex::Graphics::LocalReflectionRadiancePass
