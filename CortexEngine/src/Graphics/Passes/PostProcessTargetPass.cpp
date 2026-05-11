#include "Graphics/Passes/PostProcessTargetPass.h"

#include <vector>

namespace Cortex::Graphics::PostProcessTargetPass {

namespace {

bool AddTransitionIfNeeded(
    const ResourceStateRef& resource,
    std::vector<D3D12_RESOURCE_BARRIER>& barriers) {
    if (!resource.resource) {
        return true;
    }
    if (!resource.state) {
        return false;
    }
    if (*resource.state == resource.desiredState) {
        return true;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource.resource;
    barrier.Transition.StateBefore = *resource.state;
    barrier.Transition.StateAfter = resource.desiredState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers.push_back(barrier);
    return true;
}

} // namespace

bool PrepareInputsAndBackBuffer(const PrepareContext& context) {
    if (!context.commandList || !context.backBuffer || !context.backBufferUsedAsRenderTarget) {
        return false;
    }
    if (context.shaderResourceCount > 0 && !context.shaderResources) {
        return false;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(context.shaderResourceCount + 1);

    for (size_t index = 0; index < context.shaderResourceCount; ++index) {
        if (!AddTransitionIfNeeded(context.shaderResources[index], barriers)) {
            return false;
        }
    }

    D3D12_RESOURCE_BARRIER backBufferBarrier{};
    backBufferBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    backBufferBarrier.Transition.pResource = context.backBuffer;
    backBufferBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    backBufferBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    backBufferBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers.push_back(backBufferBarrier);
    *context.backBufferUsedAsRenderTarget = true;

    if (!barriers.empty()) {
        context.commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }

    for (size_t index = 0; index < context.shaderResourceCount; ++index) {
        const ResourceStateRef& resource = context.shaderResources[index];
        if (resource.resource && resource.state) {
            *resource.state = resource.desiredState;
        }
    }
    return true;
}

} // namespace Cortex::Graphics::PostProcessTargetPass
