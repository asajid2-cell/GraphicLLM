#include "Graphics/Passes/ParticleGpuPreparePass.h"

namespace Cortex::Graphics::ParticleGpuPreparePass {

PrepareResult Dispatch(const PrepareContext& context) {
    PrepareResult result{};
    if (!context.device ||
        !context.commandList ||
        !context.rootSignature ||
        !context.pipeline ||
        !context.pipeline->GetPipelineState() ||
        !context.descriptorManager ||
        !context.resources ||
        !context.sources ||
        context.sourceCount == 0 ||
        !context.constants ||
        !context.resources->gpuSourceBuffer ||
        !context.resources->gpuInstanceBuffer ||
        !context.resources->gpuPrepareConstantsInitialized) {
        return result;
    }

    UINT sourceBytes = 0;
    const HRESULT sourceHr =
        context.resources->UploadGpuSources(context.sources, context.sourceCount, sourceBytes);
    if (FAILED(sourceHr)) {
        return result;
    }

    const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress =
        context.resources->WriteGpuPrepareConstants(*context.constants);
    if (constantsAddress == 0) {
        return result;
    }

    auto descriptorRange = context.descriptorManager->AllocateTransientCBV_SRV_UAVRange(2);
    if (descriptorRange.IsErr()) {
        return result;
    }

    DescriptorHandle srvHandle = descriptorRange.Value();
    DescriptorHandle uavHandle =
        context.descriptorManager->GetCBV_SRV_UAVHandle(srvHandle.index + 1u);
    if (!srvHandle.IsValid() || !uavHandle.IsValid()) {
        return result;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = context.sourceCount;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleGpuSource);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = context.sourceCount;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleInstance);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    context.device->CreateShaderResourceView(context.resources->gpuSourceBuffer.Get(), &srvDesc, srvHandle.cpu);
    context.device->CreateUnorderedAccessView(context.resources->gpuInstanceBuffer.Get(), nullptr, &uavDesc, uavHandle.cpu);

    if (context.resources->gpuInstanceState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER toUav{};
        toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toUav.Transition.pResource = context.resources->gpuInstanceBuffer.Get();
        toUav.Transition.StateBefore = context.resources->gpuInstanceState;
        toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        context.commandList->ResourceBarrier(1, &toUav);
        context.resources->gpuInstanceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ID3D12DescriptorHeap* heaps[] = { context.descriptorManager->GetCBV_SRV_UAV_Heap() };
    context.commandList->SetDescriptorHeaps(1, heaps);
    context.commandList->SetComputeRootSignature(context.rootSignature);
    context.commandList->SetPipelineState(context.pipeline->GetPipelineState());
    context.commandList->SetComputeRootConstantBufferView(0, constantsAddress);
    context.commandList->SetComputeRootDescriptorTable(1, srvHandle.gpu);
    context.commandList->SetComputeRootDescriptorTable(2, uavHandle.gpu);

    const UINT dispatchGroups = (context.sourceCount + 127u) / 128u;
    context.commandList->Dispatch(dispatchGroups, 1, 1);

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = context.resources->gpuInstanceBuffer.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = context.resources->gpuInstanceBuffer.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    context.commandList->ResourceBarrier(2, barriers);
    context.resources->gpuInstanceState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    context.resources->gpuPreparedThisFrame = true;

    result.executed = true;
    result.dispatchGroups = dispatchGroups;
    result.sourceBytes = sourceBytes;
    return result;
}

} // namespace Cortex::Graphics::ParticleGpuPreparePass
