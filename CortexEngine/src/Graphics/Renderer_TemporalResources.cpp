#include "Renderer.h"

#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateTemporalRejectionMaskResources() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for temporal rejection mask creation");
    }

    const UINT width = GetInternalRenderWidth();
    const UINT height = GetInternalRenderHeight();
    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create temporal rejection mask");
    }

    m_temporalMaskState.texture.Reset();
    m_temporalMaskState.statsBuffer.Reset();
    for (auto& readback : m_temporalMaskState.statsReadback) {
        readback.Reset();
    }
    m_temporalMaskState.resourceState = D3D12_RESOURCE_STATE_COMMON;
    m_temporalMaskState.statsState = D3D12_RESOURCE_STATE_COMMON;
    m_temporalMaskState.statsReadbackPending.fill(false);
    m_temporalMaskState.statsSampleFrame.fill(0);
    m_frameDiagnostics.contract.temporalMask = {};
    m_temporalMaskState.builtThisFrame = false;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_temporalMaskState.texture));
    if (FAILED(hr)) {
        m_temporalMaskState.texture.Reset();
        CORTEX_REPORT_DEVICE_REMOVED("CreateTemporalRejectionMaskResources", hr);
        return Result<void>::Err("Failed to create temporal rejection mask texture");
    }

    if (!m_temporalMaskState.srv.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate temporal rejection mask SRV: " + srvResult.Error());
        }
        m_temporalMaskState.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_services.device->GetDevice()->CreateShaderResourceView(
        m_temporalMaskState.texture.Get(),
        &srvDesc,
        m_temporalMaskState.srv.cpu);

    if (!m_temporalMaskState.uav.IsValid()) {
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            return Result<void>::Err("Failed to allocate temporal rejection mask UAV: " + uavResult.Error());
        }
        m_temporalMaskState.uav = uavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_temporalMaskState.texture.Get(),
        nullptr,
        &uavDesc,
        m_temporalMaskState.uav.cpu);

    constexpr UINT64 kStatsBufferBytes = 32;
    D3D12_RESOURCE_DESC statsDesc{};
    statsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    statsDesc.Width = kStatsBufferBytes;
    statsDesc.Height = 1;
    statsDesc.DepthOrArraySize = 1;
    statsDesc.MipLevels = 1;
    statsDesc.Format = DXGI_FORMAT_UNKNOWN;
    statsDesc.SampleDesc.Count = 1;
    statsDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    statsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &statsDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_temporalMaskState.statsBuffer));
    if (FAILED(hr)) {
        m_temporalMaskState.statsBuffer.Reset();
        CORTEX_REPORT_DEVICE_REMOVED("CreateTemporalRejectionMaskStats", hr);
        return Result<void>::Err("Failed to create temporal rejection mask stats buffer");
    }

    if (!m_temporalMaskState.statsUAV.IsValid()) {
        auto statsUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (statsUavResult.IsErr()) {
            return Result<void>::Err("Failed to allocate temporal rejection mask stats UAV: " +
                                     statsUavResult.Error());
        }
        m_temporalMaskState.statsUAV = statsUavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc{};
    statsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    statsUavDesc.Buffer.NumElements = static_cast<UINT>(kStatsBufferBytes / sizeof(uint32_t));
    statsUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_temporalMaskState.statsBuffer.Get(),
        nullptr,
        &statsUavDesc,
        m_temporalMaskState.statsUAV.cpu);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = statsDesc;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    for (auto& readback : m_temporalMaskState.statsReadback) {
        hr = m_services.device->GetDevice()->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback));
        if (FAILED(hr)) {
            readback.Reset();
            CORTEX_REPORT_DEVICE_REMOVED("CreateTemporalRejectionMaskStatsReadback", hr);
            return Result<void>::Err("Failed to create temporal rejection mask stats readback buffer");
        }
    }

    spdlog::info("Temporal rejection mask created: {}x{}", width, height);
    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
