#include "Graphics/Passes/RTHistoryCopyPass.h"

namespace Cortex::Graphics::RTHistoryCopyPass {

bool CopyToHistoryAndReturnToShaderResource(const CopyContext& context) {
    if (!context.commandList ||
        !context.source ||
        !context.sourceState ||
        !context.history ||
        !context.historyState) {
        return false;
    }

    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT barrierCount = 0;

    if (*context.sourceState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = context.source;
        barriers[barrierCount].Transition.StateBefore = *context.sourceState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        *context.sourceState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }

    if (*context.historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = context.history;
        barriers[barrierCount].Transition.StateBefore = *context.historyState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        *context.historyState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    if (barrierCount > 0) {
        context.commandList->ResourceBarrier(barrierCount, barriers);
    }

    context.commandList->CopyResource(context.history, context.source);

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = context.source;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = context.history;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    context.commandList->ResourceBarrier(2, barriers);
    *context.sourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    *context.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

} // namespace Cortex::Graphics::RTHistoryCopyPass
