#include "Graphics/Passes/TAACopyPass.h"

#include <array>

namespace Cortex::Graphics::TAACopyPass {

namespace {

bool IsValid(const ResourceStateRef& resource) {
    return resource.resource != nullptr && resource.state != nullptr;
}

bool AddTransitionIfNeeded(
    const ResourceStateRef& resource,
    D3D12_RESOURCE_STATES after,
    D3D12_RESOURCE_BARRIER* barriers,
    UINT& barrierCount,
    UINT barrierCapacity) {
    if (!resource.resource) {
        return true;
    }
    if (!resource.state || !barriers || barrierCount >= barrierCapacity) {
        return false;
    }
    if (*resource.state == after) {
        return true;
    }

    D3D12_RESOURCE_BARRIER& barrier = barriers[barrierCount++];
    barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.resource;
    barrier.Transition.StateBefore = *resource.state;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return true;
}

void SetState(const ResourceStateRef& resource, D3D12_RESOURCE_STATES state) {
    if (resource.state) {
        *resource.state = state;
    }
}

bool ApplyTransitions(
    ID3D12GraphicsCommandList* commandList,
    const std::array<ResourceStateRef, 3>& resources,
    const std::array<D3D12_RESOURCE_STATES, 3>& states,
    size_t count) {
    D3D12_RESOURCE_BARRIER barriers[3]{};
    UINT barrierCount = 0;
    for (size_t index = 0; index < count; ++index) {
        if (!AddTransitionIfNeeded(resources[index], states[index], barriers, barrierCount, 3)) {
            return false;
        }
    }
    if (barrierCount > 0) {
        commandList->ResourceBarrier(barrierCount, barriers);
    }
    return true;
}

} // namespace

bool CopyHdrToHistory(const HistoryCopyContext& context) {
    if (!context.commandList || !IsValid(context.hdrColor) || !IsValid(context.historyColor)) {
        return false;
    }

    if (!context.skipTransitions) {
        std::array<ResourceStateRef, 3> resources{};
        std::array<D3D12_RESOURCE_STATES, 3> states{};
        size_t count = 0;
        if (context.transitionIntermediateToRenderTarget && context.taaIntermediate.resource) {
            resources[count] = context.taaIntermediate;
            states[count] = D3D12_RESOURCE_STATE_RENDER_TARGET;
            ++count;
        }
        resources[count] = context.hdrColor;
        states[count] = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ++count;
        resources[count] = context.historyColor;
        states[count] = D3D12_RESOURCE_STATE_COPY_DEST;
        ++count;
        if (!ApplyTransitions(context.commandList, resources, states, count)) {
            return false;
        }
    }

    if (context.transitionIntermediateToRenderTarget && context.taaIntermediate.resource) {
        SetState(context.taaIntermediate, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    SetState(context.hdrColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
    SetState(context.historyColor, D3D12_RESOURCE_STATE_COPY_DEST);
    context.commandList->CopyResource(context.historyColor.resource, context.hdrColor.resource);

    if (context.returnHdrAndHistoryToShaderResource) {
        std::array<ResourceStateRef, 3> resources{context.hdrColor, context.historyColor, {}};
        std::array<D3D12_RESOURCE_STATES, 3> states{
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COMMON,
        };
        if (!ApplyTransitions(context.commandList, resources, states, 2)) {
            return false;
        }
        SetState(context.hdrColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        SetState(context.historyColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    return true;
}

bool CopyIntermediateToHdr(const IntermediateCopyContext& context) {
    if (!context.commandList || !IsValid(context.taaIntermediate) || !IsValid(context.hdrColor)) {
        return false;
    }

    if (!context.skipTransitions) {
        const std::array<ResourceStateRef, 3> resources{context.taaIntermediate, context.hdrColor, {}};
        const std::array<D3D12_RESOURCE_STATES, 3> states{
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON,
        };
        if (!ApplyTransitions(context.commandList, resources, states, 2)) {
            return false;
        }
    }

    SetState(context.taaIntermediate, D3D12_RESOURCE_STATE_COPY_SOURCE);
    SetState(context.hdrColor, D3D12_RESOURCE_STATE_COPY_DEST);
    context.commandList->CopyResource(context.hdrColor.resource, context.taaIntermediate.resource);
    return true;
}

} // namespace Cortex::Graphics::TAACopyPass
