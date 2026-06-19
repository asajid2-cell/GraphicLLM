#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

constexpr UINT64 AlignConstantBuffer(UINT64 size) {
    return (size + 255u) & ~255u;
}

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES props{};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

void TransitionBuffer(ID3D12GraphicsCommandList* commandList,
                      ID3D12Resource* resource,
                      D3D12_RESOURCE_STATES& current,
                      D3D12_RESOURCE_STATES desired) {
    if (!commandList || !resource || current == desired) {
        current = desired;
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = current;
    barrier.Transition.StateAfter = desired;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    current = desired;
}

} // namespace

Result<void> Renderer::CreateExposureResources() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for exposure resources");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device not available for exposure resources");
    }

    m_exposureState.Reset();
    m_exposureState.adaptedExposure = std::max(m_qualityRuntimeState.exposure, 0.01f);
    m_exposureState.targetExposure = m_exposureState.adaptedExposure;

    auto stateDesc = BufferDesc(sizeof(ExposureStateGpuData), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &stateDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_exposureState.stateBuffer));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create exposure state buffer");
    }
    m_exposureState.stateBuffer->SetName(L"ExposureAdaptationState");
    m_exposureState.stateBufferState = D3D12_RESOURCE_STATE_COPY_DEST;

    auto uploadHeap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &stateDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_exposureState.initUploadBuffer));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create exposure init upload buffer");
    }

    ExposureStateGpuData initial{};
    initial.exposure = m_exposureState.adaptedExposure;
    initial.meteredLuminance = 0.18f;
    initial.targetExposure = m_exposureState.adaptedExposure;
    initial.initialized = 1.0f;
    void* mappedInit = nullptr;
    D3D12_RANGE noRead{0, 0};
    if (SUCCEEDED(m_exposureState.initUploadBuffer->Map(0, &noRead, &mappedInit)) && mappedInit) {
        std::memcpy(mappedInit, &initial, sizeof(initial));
        m_exposureState.initUploadBuffer->Unmap(0, nullptr);
    }

    const UINT64 cbSize = AlignConstantBuffer(sizeof(ExposureDispatchConstants));
    auto cbDesc = BufferDesc(cbSize);
    auto readbackHeap = HeapProps(D3D12_HEAP_TYPE_READBACK);
    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_exposureState.constantBuffers[frame]));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create exposure constants buffer");
        }
        m_exposureState.constantBuffers[frame]->SetName(L"ExposureDispatchConstants");
        if (FAILED(m_exposureState.constantBuffers[frame]->Map(
                0, &noRead, reinterpret_cast<void**>(&m_exposureState.mappedConstants[frame])))) {
            return Result<void>::Err("Failed to map exposure constants buffer");
        }

        hr = device->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &stateDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_exposureState.readbackBuffers[frame]));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create exposure readback buffer");
        }
        m_exposureState.readbackBuffers[frame]->SetName(L"ExposureReadback");
    }

    auto srvRange = m_services.descriptorManager->AllocateCBV_SRV_UAVRange(kFrameCount);
    if (srvRange.IsErr()) {
        return Result<void>::Err("Failed to allocate exposure SRV table: " + srvRange.Error());
    }
    auto uavRange = m_services.descriptorManager->AllocateCBV_SRV_UAVRange(kFrameCount);
    if (uavRange.IsErr()) {
        return Result<void>::Err("Failed to allocate exposure UAV table: " + uavRange.Error());
    }

    for (uint32_t frame = 0; frame < kFrameCount; ++frame) {
        m_exposureState.srvTables[frame] =
            m_services.descriptorManager->GetCBV_SRV_UAVHandle(srvRange.Value().index + frame);
        m_exposureState.uavTables[frame] =
            m_services.descriptorManager->GetCBV_SRV_UAVHandle(uavRange.Value().index + frame);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = 1;
        uavDesc.Buffer.StructureByteStride = sizeof(ExposureStateGpuData);
        device->CreateUnorderedAccessView(
            m_exposureState.stateBuffer.Get(),
            nullptr,
            &uavDesc,
            m_exposureState.uavTables[frame].cpu);
    }

    m_exposureState.descriptorsValid = true;
    spdlog::info("Exposure histogram/adaptation resources created");
    return Result<void>::Ok();
}

void Renderer::UpdateAutoExposureFromReadback() {
    if (!m_exposureState.hasReadback || m_exposureState.readbackBuffers.empty()) {
        return;
    }

    const uint32_t frameSlot = m_frameRuntime.frameIndex % kFrameCount;
    if (!m_exposureState.readbackValid[frameSlot] || !m_exposureState.readbackBuffers[frameSlot]) {
        return;
    }

    ExposureStateGpuData data{};
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, sizeof(ExposureStateGpuData)};
    if (SUCCEEDED(m_exposureState.readbackBuffers[frameSlot]->Map(0, &readRange, &mapped)) && mapped) {
        std::memcpy(&data, mapped, sizeof(data));
        D3D12_RANGE noWrite{0, 0};
        m_exposureState.readbackBuffers[frameSlot]->Unmap(0, &noWrite);
    }

    if (data.initialized > 0.5f &&
        std::isfinite(data.exposure) &&
        data.exposure > 0.0f) {
        m_exposureState.adaptedExposure = std::clamp(data.exposure, 0.08f, 8.0f);
        m_exposureState.meteredLuminance = std::max(data.meteredLuminance, 1e-4f);
        m_exposureState.targetExposure = std::clamp(data.targetExposure, 0.08f, 8.0f);
    }
}

void Renderer::DispatchAutoExposure() {
    if (!m_pipelineState.exposureHistogram ||
        !m_pipelineState.exposureHistogram->GetPipelineState() ||
        !m_pipelineState.computeRootSignature ||
        !m_exposureState.stateBuffer ||
        !m_exposureState.descriptorsValid ||
        !m_mainTargets.hdr.resources.color ||
        !m_mainTargets.hdr.descriptors.srv.IsValid() ||
        !m_commandResources.graphicsList ||
        !m_services.device ||
        !m_services.descriptorManager) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = m_commandResources.graphicsList.Get();
    ID3D12Device* device = m_services.device->GetDevice();
    const uint32_t frameSlot = m_frameRuntime.frameIndex % kFrameCount;

    if (!m_exposureState.initializedOnGpu) {
        commandList->CopyBufferRegion(
            m_exposureState.stateBuffer.Get(),
            0,
            m_exposureState.initUploadBuffer.Get(),
            0,
            sizeof(ExposureStateGpuData));
        m_exposureState.initializedOnGpu = true;
    }

    if (m_exposureState.stateBufferState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        TransitionBuffer(commandList,
                         m_exposureState.stateBuffer.Get(),
                         m_exposureState.stateBufferState,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (m_mainTargets.hdr.resources.state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER hdrBarrier{};
        hdrBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        hdrBarrier.Transition.pResource = m_mainTargets.hdr.resources.color.Get();
        hdrBarrier.Transition.StateBefore = m_mainTargets.hdr.resources.state;
        hdrBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        hdrBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &hdrBarrier);
        m_mainTargets.hdr.resources.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    ExposureDispatchConstants constants{};
    constants.manualExposureCompensation = std::clamp(m_qualityRuntimeState.exposure, 0.05f, 8.0f);
    constants.deltaTime = std::max(m_constantBuffers.frameCPU.timeAndExposure.y, 1.0f / 120.0f);
    constants.width = GetInternalRenderWidth();
    constants.height = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC desc = m_mainTargets.hdr.resources.color->GetDesc();
        constants.width = static_cast<uint32_t>(std::max<UINT64>(desc.Width, 1u));
        constants.height = std::max<uint32_t>(desc.Height, 1u);
    }
    if (m_exposureState.mappedConstants[frameSlot]) {
        std::memcpy(m_exposureState.mappedConstants[frameSlot], &constants, sizeof(constants));
    }

    device->CopyDescriptorsSimple(
        1,
        m_exposureState.srvTables[frameSlot].cpu,
        m_mainTargets.hdr.descriptors.srv.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    commandList->SetComputeRootSignature(m_pipelineState.computeRootSignature->GetRootSignature());
    commandList->SetPipelineState(m_pipelineState.exposureHistogram->GetPipelineState());
    commandList->SetComputeRootConstantBufferView(
        0,
        m_exposureState.constantBuffers[frameSlot]->GetGPUVirtualAddress());
    commandList->SetComputeRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
    commandList->SetComputeRootDescriptorTable(3, m_exposureState.srvTables[frameSlot].gpu);
    commandList->SetComputeRootDescriptorTable(6, m_exposureState.uavTables[frameSlot].gpu);
    commandList->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_exposureState.stateBuffer.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    TransitionBuffer(commandList,
                     m_exposureState.stateBuffer.Get(),
                     m_exposureState.stateBufferState,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        m_exposureState.readbackBuffers[frameSlot].Get(),
        0,
        m_exposureState.stateBuffer.Get(),
        0,
        sizeof(ExposureStateGpuData));

    TransitionBuffer(commandList,
                     m_exposureState.stateBuffer.Get(),
                     m_exposureState.stateBufferState,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_exposureState.readbackValid[frameSlot] = true;
    m_exposureState.hasReadback = true;
}

} // namespace Cortex::Graphics
