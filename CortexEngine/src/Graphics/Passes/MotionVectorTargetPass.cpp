#include "Graphics/Passes/MotionVectorTargetPass.h"

namespace Cortex::Graphics::MotionVectorTargetPass {

namespace {

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

bool ApplyVelocityTransition(
    ID3D12GraphicsCommandList* commandList,
    const ResourceStateRef& velocity,
    D3D12_RESOURCE_STATES targetState) {
    if (!commandList || !velocity.resource || !velocity.state) {
        return false;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    UINT barrierCount = 0;
    if (!AddTransitionIfNeeded(velocity, targetState, &barrier, barrierCount, 1)) {
        return false;
    }
    if (barrierCount > 0) {
        commandList->ResourceBarrier(1, &barrier);
    }
    SetState(velocity, targetState);
    return true;
}

} // namespace

bool TransitionVelocityToUnorderedAccess(const VelocityUAVContext& context) {
    return ApplyVelocityTransition(
        context.commandList,
        context.velocity,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

bool TransitionCameraTargets(const CameraTargetContext& context) {
    if (!context.commandList || !context.velocity.resource || !context.velocity.state) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT barrierCount = 0;
    if (!AddTransitionIfNeeded(
            context.velocity,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            barriers,
            barrierCount,
            2)) {
        return false;
    }
    if (!AddTransitionIfNeeded(
            context.depth,
            context.depthSampleState,
            barriers,
            barrierCount,
            2)) {
        return false;
    }
    if (barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }

    SetState(context.velocity, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (context.depth.resource) {
        SetState(context.depth, context.depthSampleState);
    }
    return true;
}

} // namespace Cortex::Graphics::MotionVectorTargetPass
