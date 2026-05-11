#include "Graphics/Passes/RTReflectionDispatchPass.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/RHI/DX12Texture.h"

namespace Cortex::Graphics::RTReflectionDispatchPass {

namespace {

constexpr D3D12_RESOURCE_STATES kShaderResourceState =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

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

void InsertUAVBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource) {
    if (!commandList || !resource) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commandList->ResourceBarrier(1, &barrier);
}

} // namespace

bool PrepareInputsAndOutput(const PrepareContext& context) {
    if (!context.commandList ||
        !context.depth.resource ||
        !context.depth.state ||
        !context.reflectionOutput.resource ||
        !context.reflectionOutput.state) {
        return false;
    }

    TransitionResource(context.commandList, context.depth, kDepthSampleState);
    if (context.transitionNormal) {
        TransitionResource(context.commandList, context.normalRoughness, kShaderResourceState);
    }
    TransitionResource(context.commandList,
                       context.reflectionOutput,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
}

bool ClearOutputForDebugView(const DebugClearContext& context) {
    if (context.clearMode == 0) {
        return true;
    }
    if (!context.commandList ||
        !context.device ||
        !context.descriptorHeap ||
        !context.reflectionOutput.resource ||
        !context.reflectionOutput.state ||
        !context.shaderVisibleUav.IsValid() ||
        !context.cpuUav.IsValid()) {
        return false;
    }

    TransitionResource(context.commandList,
                       context.reflectionOutput,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    context.device->CreateUnorderedAccessView(
        context.reflectionOutput.resource,
        nullptr,
        &uavDesc,
        context.shaderVisibleUav.cpu);

    ID3D12DescriptorHeap* heaps[] = {context.descriptorHeap};
    context.commandList->SetDescriptorHeaps(1, heaps);

    const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float* clear = (context.clearMode == 2) ? magenta : black;
    context.commandList->ClearUnorderedAccessViewFloat(
        context.shaderVisibleUav.gpu,
        context.cpuUav.cpu,
        context.reflectionOutput.resource,
        clear,
        0,
        nullptr);
    InsertUAVBarrier(context.commandList, context.reflectionOutput.resource);
    return true;
}

void EnsureTextureNonPixelReadable(ID3D12GraphicsCommandList* commandList,
                                   const std::shared_ptr<DX12Texture>& texture) {
    if (!commandList || !texture || !texture->GetResource()) {
        return;
    }

    const D3D12_RESOURCE_STATES current = texture->GetCurrentState();
    if ((current & kShaderResourceState) == kShaderResourceState) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture->GetResource();
    barrier.Transition.StateBefore = current;
    barrier.Transition.StateAfter = kShaderResourceState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    texture->SetState(kShaderResourceState);
}

bool FinalizeOutputWrites(ID3D12GraphicsCommandList* commandList,
                          ID3D12Resource* reflectionOutput) {
    if (!commandList || !reflectionOutput) {
        return false;
    }
    InsertUAVBarrier(commandList, reflectionOutput);
    return true;
}

} // namespace Cortex::Graphics::RTReflectionDispatchPass
