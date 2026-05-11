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

constexpr D3D12_RESOURCE_STATES kDepthSampleState =
    D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

[[nodiscard]] RootBindings ResolveRootBindings(ID3D12RootSignature* compactRootSignature,
                                               DX12ComputeRootSignature* fallbackRootSignature) {
    if (compactRootSignature) {
        return {compactRootSignature, 0u, 1u, 2u};
    }
    if (fallbackRootSignature && fallbackRootSignature->GetRootSignature()) {
        return {fallbackRootSignature->GetRootSignature(), 1u, 3u, 6u};
    }
    return {};
}

[[nodiscard]] RootBindings ResolveRootBindings(const GraphContext& context) {
    return ResolveRootBindings(context.compactRootSignature, context.fallbackRootSignature);
}

[[nodiscard]] bool TransitionResource(ID3D12GraphicsCommandList* commandList,
                                      const ResourceStateRef& resource,
                                      D3D12_RESOURCE_STATES desiredState,
                                      D3D12_RESOURCE_BARRIER& barrier) {
    if (!commandList || !resource.resource || !resource.state) {
        return false;
    }
    if (*resource.state == desiredState) {
        return true;
    }

    barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.resource;
    barrier.Transition.StateBefore = *resource.state;
    barrier.Transition.StateAfter = desiredState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    *resource.state = desiredState;
    return true;
}

void TransitionHZBMipToShaderResource(ID3D12GraphicsCommandList* commandList,
                                      ID3D12Resource* hzb,
                                      uint32_t mip) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = hzb;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = mip;
    commandList->ResourceBarrier(1, &barrier);
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

bool BuildFromDepth(const BuildContext& context) {
    if (!context.commandList || !context.descriptorManager || !context.depth.resource || !context.hzb.resource ||
        !context.initPipeline || !context.downsamplePipeline || !context.initPipeline->GetPipelineState() ||
        !context.downsamplePipeline->GetPipelineState() || !context.frameConstants ||
        !context.dispatchTablesValid || !context.compactRootSignature ||
        context.width == 0 || context.height == 0 || context.mipCount == 0 ||
        context.mipSrvStaging.size() < context.mipCount ||
        context.mipUavStaging.size() < context.mipCount ||
        context.dispatchSrvTable.size() < context.mipCount ||
        context.dispatchUavTable.size() < context.mipCount ||
        !context.depthSrv.IsValid()) {
        spdlog::error("BuildHZBFromDepth: missing HZB pass prerequisites");
        return false;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    if (!TransitionResource(context.commandList, context.depth, kDepthSampleState, barrier) ||
        !TransitionResource(context.commandList, context.hzb, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, barrier)) {
        return false;
    }

    const RootBindings bindings = ResolveRootBindings(context.compactRootSignature, context.fallbackRootSignature);
    if (!bindings.rootSignature) {
        spdlog::error("BuildHZBFromDepth: HZB root signature unavailable");
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    context.commandList->SetComputeRootSignature(bindings.rootSignature);
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetComputeRootConstantBufferView(bindings.frameConstantsRoot, context.frameConstants);

    auto bindTables = [&](DescriptorHandle sourceSrv, DescriptorHandle sourceUav, uint32_t dispatchIndex) -> bool {
        if (!sourceSrv.IsValid() || !sourceUav.IsValid()) {
            spdlog::error("BuildHZBFromDepth: invalid staging descriptor for dispatch {}", dispatchIndex);
            return false;
        }
        if (dispatchIndex >= context.dispatchSrvTable.size() || dispatchIndex >= context.dispatchUavTable.size() ||
            !context.dispatchSrvTable[dispatchIndex].IsValid() || !context.dispatchUavTable[dispatchIndex].IsValid()) {
            spdlog::error("BuildHZBFromDepth: missing persistent descriptor for dispatch {}", dispatchIndex);
            return false;
        }

        context.commandList->SetComputeRootDescriptorTable(bindings.srvTableRoot, context.dispatchSrvTable[dispatchIndex].gpu);
        context.commandList->SetComputeRootDescriptorTable(bindings.uavTableRoot, context.dispatchUavTable[dispatchIndex].gpu);
        return true;
    };

    auto dispatchForDims = [&](uint32_t width, uint32_t height) {
        const UINT groupX = (width + 7u) / 8u;
        const UINT groupY = (height + 7u) / 8u;
        context.commandList->Dispatch(groupX, groupY, 1);
    };

    context.commandList->SetPipelineState(context.initPipeline->GetPipelineState());
    if (!bindTables(context.depthSrv, context.mipUavStaging[0], 0)) {
        return false;
    }
    dispatchForDims(context.width, context.height);
    TransitionHZBMipToShaderResource(context.commandList, context.hzb.resource, 0);

    uint32_t mipW = context.width;
    uint32_t mipH = context.height;
    for (uint32_t mip = 1; mip < context.mipCount; ++mip) {
        mipW = (mipW + 1u) / 2u;
        mipH = (mipH + 1u) / 2u;

        context.commandList->SetPipelineState(context.downsamplePipeline->GetPipelineState());
        if (!bindTables(context.mipSrvStaging[mip - 1], context.mipUavStaging[mip], mip)) {
            return false;
        }
        dispatchForDims(mipW, mipH);
        TransitionHZBMipToShaderResource(context.commandList, context.hzb.resource, mip);
    }

    *context.hzb.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return true;
}

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
