#include "Renderer.h"

#include "Graphics/Passes/DescriptorTable.h"

#include <algorithm>
#include <array>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
namespace {

constexpr D3D12_RESOURCE_STATES kComputeSrvState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

void Transition(ID3D12GraphicsCommandList* cmdList,
                ID3D12Resource* resource,
                D3D12_RESOURCE_STATES& state,
                D3D12_RESOURCE_STATES next) {
    if (!cmdList || !resource || state == next) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = next;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    state = next;
}

void UAVBarrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* resource) {
    if (!cmdList || !resource) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    cmdList->ResourceBarrier(1, &barrier);
}

void WriteTexture3DSRV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format) {
    if (!device || !handle.IsValid()) {
        return;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture3D.MipLevels = 1;
    device->CreateShaderResourceView(resource, &desc, handle.cpu);
}

void WriteTexture3DUAV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format) {
    if (!device || !handle.IsValid()) {
        return;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    desc.Texture3D.WSize = RendererVolumetricState::kFroxelDepth;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, handle.cpu);
}

void WriteTexture2DUAV(ID3D12Device* device,
                       DescriptorHandle handle,
                       ID3D12Resource* resource,
                       DXGI_FORMAT format) {
    if (!device || !handle.IsValid()) {
        return;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(resource, nullptr, &desc, handle.cpu);
}

} // namespace

void Renderer::RenderVolumetrics() {
    if (!m_volumetrics.enabled ||
        !m_pipelineState.computeRootSignature ||
        !m_pipelineState.volumetricInjectCompute ||
        !m_pipelineState.volumetricIntegrateCompute ||
        !m_pipelineState.volumetricCompositeCompute ||
        !m_commandResources.graphicsList ||
        !m_services.device ||
        !m_services.descriptorManager ||
        !m_depthResources.resources.buffer ||
        !m_mainTargets.hdr.resources.color ||
        !m_volumetrics.descriptorTablesValid) {
        return;
    }

    if (!m_volumetrics.resourcesValid) {
        auto createResult = CreateVolumetricResources();
        if (createResult.IsErr()) {
            spdlog::warn("RenderVolumetrics: {}", createResult.Error());
            return;
        }
    }

    ID3D12Device* device = m_services.device->GetDevice();
    ID3D12GraphicsCommandList* cmdList = m_commandResources.graphicsList.Get();
    const uint32_t frame = m_frameRuntime.frameIndex % kFrameCount;
    auto& srvTable = m_volumetrics.srvTables[frame];
    auto& uavTable = m_volumetrics.uavTables[frame];

    const uint32_t historyWrite = m_volumetrics.historyWriteIndex & 1u;
    const uint32_t historyRead = historyWrite ^ 1u;

    Transition(cmdList,
               m_depthResources.resources.buffer.Get(),
               m_depthResources.resources.resourceState,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList,
               m_mainTargets.hdr.resources.color.Get(),
               m_mainTargets.hdr.resources.state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmdList,
               m_volumetrics.injected.texture.Get(),
               m_volumetrics.injected.state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmdList,
               m_volumetrics.integrated.texture.Get(),
               m_volumetrics.integrated.state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmdList,
               m_volumetrics.history[historyRead].texture.Get(),
               m_volumetrics.history[historyRead].state,
               kComputeSrvState);
    Transition(cmdList,
               m_volumetrics.history[historyWrite].texture.Get(),
               m_volumetrics.history[historyWrite].state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (m_shadows.Resources().resources.map) {
        Transition(cmdList,
                   m_shadows.Resources().resources.map.Get(),
                   m_shadows.Resources().resources.resourceState,
                   kComputeSrvState);
    }

    DescriptorTable::WriteTexture2DSRV(device, srvTable[0], m_depthResources.resources.buffer.Get(), DXGI_FORMAT_R32_FLOAT);
    WriteTexture3DSRV(device, srvTable[1], m_volumetrics.history[historyRead].texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture3DSRV(device, srvTable[2], m_volumetrics.injected.texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture3DSRV(device, srvTable[3], m_volumetrics.integrated.texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture3DUAV(device, uavTable[0], m_volumetrics.injected.texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture3DUAV(device, uavTable[1], m_volumetrics.integrated.texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture3DUAV(device, uavTable[2], m_volumetrics.history[historyWrite].texture.Get(), RendererVolumetricState::kFormat);
    WriteTexture2DUAV(device, uavTable[3], m_mainTargets.hdr.resources.color.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);

    if (!DescriptorTable::BindCBVSRVUAVHeap(cmdList, m_services.descriptorManager.get())) {
        return;
    }

    cmdList->SetComputeRootSignature(m_pipelineState.computeRootSignature->GetRootSignature());
    cmdList->SetComputeRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
    cmdList->SetComputeRootDescriptorTable(3, srvTable[0].gpu);
    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        cmdList->SetComputeRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }
    cmdList->SetComputeRootDescriptorTable(6, uavTable[0].gpu);

    cmdList->SetPipelineState(m_pipelineState.volumetricInjectCompute->GetPipelineState());
    cmdList->Dispatch((RendererVolumetricState::kFroxelWidth + 3u) / 4u,
                      (RendererVolumetricState::kFroxelHeight + 3u) / 4u,
                      (RendererVolumetricState::kFroxelDepth + 3u) / 4u);
    UAVBarrier(cmdList, m_volumetrics.injected.texture.Get());

    Transition(cmdList, m_volumetrics.injected.texture.Get(), m_volumetrics.injected.state, kComputeSrvState);
    Transition(cmdList, m_volumetrics.integrated.texture.Get(), m_volumetrics.integrated.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->SetPipelineState(m_pipelineState.volumetricIntegrateCompute->GetPipelineState());
    cmdList->Dispatch((RendererVolumetricState::kFroxelWidth + 7u) / 8u,
                      (RendererVolumetricState::kFroxelHeight + 7u) / 8u,
                      1u);
    UAVBarrier(cmdList, m_volumetrics.integrated.texture.Get());
    UAVBarrier(cmdList, m_volumetrics.history[historyWrite].texture.Get());

    Transition(cmdList, m_volumetrics.integrated.texture.Get(), m_volumetrics.integrated.state, kComputeSrvState);
    cmdList->SetPipelineState(m_pipelineState.volumetricCompositeCompute->GetPipelineState());
    cmdList->Dispatch((GetInternalRenderWidth() + 7u) / 8u,
                      (GetInternalRenderHeight() + 7u) / 8u,
                      1u);
    UAVBarrier(cmdList, m_mainTargets.hdr.resources.color.Get());

    Transition(cmdList,
               m_mainTargets.hdr.resources.color.Get(),
               m_mainTargets.hdr.resources.state,
               D3D12_RESOURCE_STATE_RENDER_TARGET);

    m_volumetrics.historyValid = true;
    m_volumetrics.historyWriteIndex = historyRead;
}

} // namespace Cortex::Graphics
