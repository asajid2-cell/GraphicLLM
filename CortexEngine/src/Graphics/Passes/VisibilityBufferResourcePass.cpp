#include "Graphics/Passes/VisibilityBufferResourcePass.h"

#include "Graphics/RendererGeometryUtils.h"

namespace Cortex::Graphics::VisibilityBufferResourcePass {

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

void EnsureStateIncludes(ID3D12GraphicsCommandList* commandList,
                         const ResourceStateRef& target,
                         D3D12_RESOURCE_STATES required) {
    if (!commandList || !target.resource || !target.state || ((*target.state & required) == required)) {
        return;
    }

    TransitionResource(commandList, target, required);
}

} // namespace

bool PrepareDepthForVisibility(ID3D12GraphicsCommandList* commandList,
                               const ResourceStateRef& depth) {
    if (!commandList) {
        return false;
    }
    TransitionResource(commandList, depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    return true;
}

bool PrepareDepthForSampling(ID3D12GraphicsCommandList* commandList,
                             const ResourceStateRef& depth) {
    if (!commandList) {
        return false;
    }
    TransitionResource(commandList, depth, kDepthSampleState);
    return true;
}

bool PrepareHZBForCulling(ID3D12GraphicsCommandList* commandList,
                          const ResourceStateRef& hzb) {
    if (!commandList) {
        return false;
    }
    EnsureStateIncludes(commandList, hzb, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return true;
}

} // namespace Cortex::Graphics::VisibilityBufferResourcePass
