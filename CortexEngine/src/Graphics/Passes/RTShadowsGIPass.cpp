#include "Graphics/Passes/RTShadowsGIPass.h"

#include "Graphics/RendererGeometryUtils.h"

namespace Cortex::Graphics::RTShadowsGIPass {

namespace {

void TransitionResource(ID3D12GraphicsCommandList* commandList,
                        const ResourceStateRef& target,
                        D3D12_RESOURCE_STATES desired) {
    if (!commandList || !target.resource || !target.state || *target.state == desired) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.resource;
    barrier.Transition.StateBefore = *target.state;
    barrier.Transition.StateAfter = desired;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    *target.state = desired;
}

} // namespace

bool PrepareShadowInputs(const ShadowInputContext& context) {
    if (!context.commandList) {
        return false;
    }

    TransitionResource(context.commandList, context.depth, kDepthSampleState);
    TransitionResource(context.commandList,
                       context.shadowMask,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}

bool PrepareGIOutput(const GIOutputContext& context) {
    if (!context.commandList) {
        return false;
    }

    TransitionResource(context.commandList,
                       context.color,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}

} // namespace Cortex::Graphics::RTShadowsGIPass
