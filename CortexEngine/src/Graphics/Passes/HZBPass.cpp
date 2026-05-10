#include "HZBPass.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics::HZBPass {

namespace {

struct RootBindings {
    ID3D12RootSignature* rootSignature = nullptr;
    UINT frameConstantsRoot = 0;
    UINT srvTableRoot = 0;
    UINT uavTableRoot = 0;
};

[[nodiscard]] RootBindings ResolveRootBindings(const GraphContext& context) {
    if (context.compactRootSignature) {
        return {context.compactRootSignature, 0u, 1u, 2u};
    }
    if (context.fallbackRootSignature && context.fallbackRootSignature->GetRootSignature()) {
        return {context.fallbackRootSignature->GetRootSignature(), 1u, 3u, 6u};
    }
    return {};
}

[[nodiscard]] bool BindCommon(ID3D12GraphicsCommandList* cmdList,
                              const GraphContext& context,
                              DX12ComputePipeline* pipeline,
                              uint32_t mip) {
    if (!cmdList || !context.descriptorManager || !pipeline || !pipeline->GetPipelineState() ||
        !context.frameConstants) {
        spdlog::error("HZB RG: missing compute prerequisites for mip {}", mip);
        return false;
    }

    const RootBindings bindings = ResolveRootBindings(context);
    if (!bindings.rootSignature) {
        spdlog::error("HZB RG: compute root signature unavailable for mip {}", mip);
        return false;
    }

    if (context.srvTable.size() <= mip || context.uavTable.size() <= mip ||
        !context.srvTable[mip].IsValid() || !context.uavTable[mip].IsValid()) {
        spdlog::error("HZB RG: missing persistent descriptors for mip {}", mip);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetComputeRootSignature(bindings.rootSignature);
    cmdList->SetPipelineState(pipeline->GetPipelineState());
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootConstantBufferView(bindings.frameConstantsRoot, context.frameConstants);
    cmdList->SetComputeRootDescriptorTable(bindings.srvTableRoot, context.srvTable[mip].gpu);
    cmdList->SetComputeRootDescriptorTable(bindings.uavTableRoot, context.uavTable[mip].gpu);
    return true;
}

void DispatchMip(ID3D12GraphicsCommandList* cmdList,
                 const GraphContext& context,
                 DX12ComputePipeline* pipeline,
                 uint32_t mip,
                 uint32_t width,
                 uint32_t height) {
    if (!BindCommon(cmdList, context, pipeline, mip)) {
        return;
    }

    const UINT groupX = (width + 7u) / 8u;
    const UINT groupY = (height + 7u) / 8u;
    cmdList->Dispatch(groupX, groupY, 1);
}

} // namespace

void AddFromDepth(RenderGraph& graph,
                  RGResourceHandle depthHandle,
                  RGResourceHandle hzbHandle,
                  const GraphContext& context) {
    if (!depthHandle.IsValid() || !hzbHandle.IsValid() ||
        !context.initPipeline || !context.downsamplePipeline ||
        context.width == 0 || context.height == 0 || context.mipCount == 0) {
        return;
    }

    graph.AddPass(
        "HZB_InitMip0",
        [depthHandle, hzbHandle](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Compute);
            builder.Read(depthHandle, RGResourceUsage::ShaderResource | RGResourceUsage::DepthStencilRead);
            builder.Write(hzbHandle, RGResourceUsage::UnorderedAccess, 0);
        },
        [context](ID3D12GraphicsCommandList* cmdList, const RenderGraph&) {
            DispatchMip(cmdList, context, context.initPipeline, 0, context.width, context.height);
        });

    uint32_t mipW = context.width;
    uint32_t mipH = context.height;
    for (uint32_t mip = 1; mip < context.mipCount; ++mip) {
        mipW = (mipW + 1u) / 2u;
        mipH = (mipH + 1u) / 2u;

        const uint32_t inMip = mip - 1u;
        const uint32_t outMip = mip;
        const uint32_t outW = mipW;
        const uint32_t outH = mipH;

        graph.AddPass(
            "HZB_DownsampleMip" + std::to_string(mip),
            [hzbHandle, inMip, outMip](RGPassBuilder& builder) {
                builder.SetType(RGPassType::Compute);
                builder.Read(hzbHandle, RGResourceUsage::ShaderResource, inMip);
                builder.Write(hzbHandle, RGResourceUsage::UnorderedAccess, outMip);
            },
            [context, outMip, outW, outH](ID3D12GraphicsCommandList* cmdList, const RenderGraph&) {
                DispatchMip(cmdList, context, context.downsamplePipeline, outMip, outW, outH);
            });
    }

    graph.AddPass(
        "HZB_Finalize",
        [hzbHandle](RGPassBuilder& builder) {
            builder.SetType(RGPassType::Compute);
            builder.Read(hzbHandle, RGResourceUsage::ShaderResource);
        },
        [](ID3D12GraphicsCommandList*, const RenderGraph&) {});
}

} // namespace Cortex::Graphics::HZBPass
