#include "BloomPass.h"

#include "FullscreenPass.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics::BloomPass {

uint32_t BaseDownsampleSlot() {
    return 0u;
}

uint32_t DownsampleChainSlot(uint32_t level) {
    return 1u + (level - 1u);
}

uint32_t BlurHSlot(uint32_t level, uint32_t stageLevels) {
    return 1u + (stageLevels - 1u) + (2u * level);
}

uint32_t BlurVSlot(uint32_t level, uint32_t stageLevels) {
    return BlurHSlot(level, stageLevels) + 1u;
}

uint32_t CompositeSlot(uint32_t compositeIndex, uint32_t stageLevels) {
    return 1u + (stageLevels - 1u) + (2u * stageLevels) + compositeIndex;
}

RGResourceDesc MakeTextureDesc(ID3D12Resource* resource, const std::string& name) {
    RGResourceDesc desc{};
    if (!resource) {
        return desc;
    }

    const D3D12_RESOURCE_DESC d3d = resource->GetDesc();
    desc = RGResourceDesc::Texture2D(
        static_cast<uint32_t>(d3d.Width),
        d3d.Height,
        d3d.Format,
        d3d.Flags,
        name);
    desc.mipLevels = std::max<uint32_t>(1u, d3d.MipLevels);
    desc.arraySize = std::max<uint32_t>(1u, d3d.DepthOrArraySize);
    return desc;
}

void SetFullscreenViewport(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource) {
    FullscreenPass::SetViewportAndScissor(commandList, resource);
}

bool BindAndClearTarget(const TargetContext& context) {
    if (!context.commandList || !context.target || !context.targetRtv.IsValid()) {
        return false;
    }

    SetFullscreenViewport(context.commandList, context.target);
    context.commandList->OMSetRenderTargets(1, &context.targetRtv.cpu, FALSE, nullptr);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    context.commandList->ClearRenderTargetView(context.targetRtv.cpu, clearColor, 0, nullptr);
    return true;
}

bool TransitionResource(ID3D12GraphicsCommandList* commandList,
                        const ResourceStateRef& resource,
                        D3D12_RESOURCE_STATES desiredState,
                        bool skipTransitions,
                        D3D12_RESOURCE_BARRIER* barriers,
                        UINT& barrierCount) {
    if (!commandList || !resource.resource || !resource.state) {
        return false;
    }

    if (!skipTransitions && *resource.state != desiredState) {
        D3D12_RESOURCE_BARRIER& barrier = barriers[barrierCount++];
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource.resource;
        barrier.Transition.StateBefore = *resource.state;
        barrier.Transition.StateAfter = desiredState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    *resource.state = desiredState;
    return true;
}

bool PrepareSourceToRenderTarget(const StageTransitionContext& context) {
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT barrierCount = 0;

    if (!TransitionResource(context.commandList,
                            context.source,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            context.skipTransitions,
                            barriers,
                            barrierCount) ||
        !TransitionResource(context.commandList,
                            context.target,
                            D3D12_RESOURCE_STATE_RENDER_TARGET,
                            context.skipTransitions,
                            barriers,
                            barrierCount)) {
        return false;
    }

    if (!context.skipTransitions && barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }
    return true;
}

bool TransitionToShaderResource(ID3D12GraphicsCommandList* commandList,
                                const ResourceStateRef& resource,
                                bool skipTransitions) {
    D3D12_RESOURCE_BARRIER barrier = {};
    UINT barrierCount = 0;
    if (!TransitionResource(commandList,
                            resource,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            skipTransitions,
                            &barrier,
                            barrierCount)) {
        return false;
    }

    if (!skipTransitions && barrierCount > 0) {
        commandList->ResourceBarrier(1, &barrier);
    }
    return true;
}

bool PrepareCompositeTargets(const CompositeTransitionContext& context) {
    if (!context.commandList || context.sources.size() > 8) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[9] = {};
    UINT barrierCount = 0;
    for (const ResourceStateRef& source : context.sources) {
        if (!source.resource) {
            continue;
        }
        if (!TransitionResource(context.commandList,
                                source,
                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                context.skipTransitions,
                                barriers,
                                barrierCount)) {
            return false;
        }
    }

    if (!TransitionResource(context.commandList,
                            context.target,
                            D3D12_RESOURCE_STATE_RENDER_TARGET,
                            context.skipTransitions,
                            barriers,
                            barrierCount)) {
        return false;
    }

    if (!context.skipTransitions && barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }
    return true;
}

bool CopyCompositeToCombined(const CopyContext& context) {
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT barrierCount = 0;
    if (!TransitionResource(context.commandList,
                            context.source,
                            D3D12_RESOURCE_STATE_COPY_SOURCE,
                            context.skipTransitions,
                            barriers,
                            barrierCount) ||
        !TransitionResource(context.commandList,
                            context.target,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            context.skipTransitions,
                            barriers,
                            barrierCount)) {
        return false;
    }

    if (!context.skipTransitions && barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }

    context.commandList->CopyResource(context.target.resource, context.source.resource);
    return TransitionToShaderResource(context.commandList, context.target, context.skipTransitions);
}

bool PrepareFullscreenState(const FullscreenContext& context) {
    return FullscreenPass::BindGraphicsState({
        context.commandList,
        context.descriptorManager,
        context.rootSignature,
        context.frameConstants,
    });
}

bool BindGraphTexture(const FullscreenContext& context,
                      ID3D12Resource* source,
                      const char* label,
                      uint32_t tableSlot) {
    if (!source || !context.device || !context.commandList || !context.descriptorManager) {
        spdlog::warn("Bloom RG: invalid source texture for {}", label ? label : "pass");
        return false;
    }

    DescriptorHandle dst{};
    if (context.srvTableValid && context.srvTable && tableSlot < context.srvTableCount) {
        dst = context.srvTable[tableSlot];
    } else {
        auto handleResult = context.descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (handleResult.IsErr()) {
            spdlog::warn("Bloom RG: failed to allocate SRV for {}: {}",
                         label ? label : "pass",
                         handleResult.Error());
            return false;
        }
        dst = handleResult.Value();
    }

    if (!dst.IsValid()) {
        return false;
    }

    const D3D12_RESOURCE_DESC resourceDesc = source->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = resourceDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    context.device->CreateShaderResourceView(source, &srvDesc, dst.cpu);
    context.commandList->SetGraphicsRootDescriptorTable(3, dst.gpu);
    return true;
}

bool EnsureGraphRTV(const FullscreenContext& context,
                    ID3D12Resource* target,
                    DescriptorHandle& cachedRtv) {
    if (!target || !context.device || !context.descriptorManager) {
        return false;
    }

    if (!cachedRtv.IsValid()) {
        auto rtvResult = context.descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) {
            spdlog::warn("Bloom RG: failed to allocate graph RTV: {}", rtvResult.Error());
            return false;
        }
        cachedRtv = rtvResult.Value();
    }

    const D3D12_RESOURCE_DESC desc = target->GetDesc();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = desc.Format;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    context.device->CreateRenderTargetView(target, &rtvDesc, cachedRtv.cpu);
    return cachedRtv.IsValid();
}

bool RenderFullscreen(const FullscreenContext& context,
                      ID3D12Resource* target,
                      DX12Pipeline* pipeline,
                      ID3D12Resource* source,
                      uint32_t sourceSlot,
                      const char* label,
                      DescriptorHandle& targetRtv) {
    if (!target || !pipeline || !pipeline->GetPipelineState()) {
        return false;
    }
    if (!PrepareFullscreenState(context)) {
        return false;
    }
    if (!EnsureGraphRTV(context, target, targetRtv)) {
        return false;
    }

    if (!BindAndClearTarget({context.commandList, target, targetRtv})) {
        return false;
    }
    context.commandList->SetPipelineState(pipeline->GetPipelineState());

    if (!BindGraphTexture(context, source, label, sourceSlot)) {
        return false;
    }

    FullscreenPass::DrawTriangle(context.commandList);
    return true;
}

bool RenderComposite(const FullscreenContext& context,
                     const RenderGraph& graph,
                     RGResourceHandle targetHandle,
                     std::span<const RGResourceHandle> bloomSources,
                     uint32_t activeLevels,
                     uint32_t stageLevels,
                     DX12Pipeline* pipeline,
                     DescriptorHandle& targetRtv) {
    ID3D12Resource* target = graph.GetResource(targetHandle);
    if (!target || !pipeline || !pipeline->GetPipelineState()) {
        spdlog::warn("Bloom RG: composite prerequisites missing target={} pipeline={} pso={}",
                     target ? 1 : 0,
                     pipeline ? 1 : 0,
                     (pipeline && pipeline->GetPipelineState()) ? 1 : 0);
        return false;
    }
    if (!PrepareFullscreenState(context)) {
        spdlog::warn("Bloom RG: composite fullscreen state unavailable");
        return false;
    }
    if (!EnsureGraphRTV(context, target, targetRtv)) {
        spdlog::warn("Bloom RG: composite RTV unavailable");
        return false;
    }

    if (!BindAndClearTarget({context.commandList, target, targetRtv})) {
        return false;
    }
    context.commandList->SetPipelineState(pipeline->GetPipelineState());

    const uint32_t clampedLevels = std::min<uint32_t>(activeLevels, static_cast<uint32_t>(bloomSources.size()));
    for (int level = static_cast<int>(clampedLevels) - 1; level >= 0; --level) {
        const RGResourceHandle sourceHandle = bloomSources[static_cast<size_t>(level)];
        if (!sourceHandle.IsValid()) {
            continue;
        }

        ID3D12Resource* source = graph.GetResource(sourceHandle);
        const uint32_t compositeIndex = static_cast<uint32_t>((clampedLevels - 1u) - static_cast<uint32_t>(level));
        if (!BindGraphTexture(context, source, "composite", CompositeSlot(compositeIndex, stageLevels))) {
            spdlog::warn("Bloom RG: composite source unavailable level={} handle={} slot={}",
                         level,
                         sourceHandle.id,
                         CompositeSlot(compositeIndex, stageLevels));
            return false;
        }
        FullscreenPass::DrawTriangle(context.commandList);
    }

    return true;
}

} // namespace Cortex::Graphics::BloomPass
