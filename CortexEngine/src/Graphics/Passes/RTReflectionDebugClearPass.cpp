#include "Graphics/Passes/RTReflectionDebugClearPass.h"

namespace Cortex::Graphics::RTReflectionDebugClearPass {

bool ClearForDebugView(const ClearContext& context) {
    if (context.clearMode == 0) {
        return true;
    }
    if (!context.commandList ||
        !context.device ||
        !context.descriptorHeap ||
        !context.reflectionColor ||
        !context.reflectionState ||
        !context.shaderVisibleUav.IsValid() ||
        !context.cpuUav.IsValid()) {
        return false;
    }

    if (*context.reflectionState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = context.reflectionColor;
        barrier.Transition.StateBefore = *context.reflectionState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &barrier);
        *context.reflectionState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    context.device->CreateUnorderedAccessView(
        context.reflectionColor,
        nullptr,
        &uavDesc,
        context.shaderVisibleUav.cpu);

    ID3D12DescriptorHeap* heaps[] = { context.descriptorHeap };
    context.commandList->SetDescriptorHeaps(1, heaps);

    const float magenta[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float* clear = (context.clearMode == 2) ? magenta : black;
    context.commandList->ClearUnorderedAccessViewFloat(
        context.shaderVisibleUav.gpu,
        context.cpuUav.cpu,
        context.reflectionColor,
        clear,
        0,
        nullptr);

    D3D12_RESOURCE_BARRIER clearBarrier{};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = context.reflectionColor;
    context.commandList->ResourceBarrier(1, &clearBarrier);

    if (*context.reflectionState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = context.reflectionColor;
        barrier.Transition.StateBefore = *context.reflectionState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &barrier);
        *context.reflectionState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    return true;
}

} // namespace Cortex::Graphics::RTReflectionDebugClearPass
