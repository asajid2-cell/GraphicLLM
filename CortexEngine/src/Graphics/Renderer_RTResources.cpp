#include "Renderer.h"

#include "BudgetPlanner.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

Result<void> Renderer::CreateRTShadowMask() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT shadow mask creation");
    }

    UINT width = GetInternalRenderWidth();
    UINT height = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        width = static_cast<UINT>(hdrDesc.Width);
        height = hdrDesc.Height;
    }

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT shadow mask");
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    m_rtShadowTargets.mask.Reset();
    m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_rtShadowTargets.mask));
    if (FAILED(hr)) {
        m_rtShadowTargets.mask.Reset();
        return Result<void>::Err("Failed to create RT shadow mask texture");
    }

    m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!m_rtShadowTargets.maskSRV.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            m_rtShadowTargets.mask.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT shadow mask: " + srvResult.Error());
        }
        m_rtShadowTargets.maskSRV = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtShadowTargets.mask.Get(),
        &srvDesc,
        m_rtShadowTargets.maskSRV.cpu);

    if (!m_rtShadowTargets.maskUAV.IsValid()) {
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            m_rtShadowTargets.mask.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT shadow mask: " + uavResult.Error());
        }
        m_rtShadowTargets.maskUAV = uavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtShadowTargets.mask.Get(),
        nullptr,
        &uavDesc,
        m_rtShadowTargets.maskUAV.cpu);

    m_rtShadowTargets.history.Reset();
    m_rtShadowTargets.historyState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC historyDesc = desc;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&m_rtShadowTargets.history));
    if (FAILED(hr)) {
        m_rtShadowTargets.history.Reset();
        return Result<void>::Err("Failed to create RT shadow mask history texture");
    }

    m_rtShadowTargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!m_rtShadowTargets.historySRV.IsValid()) {
        auto historySrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (historySrvResult.IsErr()) {
            m_rtShadowTargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT shadow mask history: " + historySrvResult.Error());
        }
        m_rtShadowTargets.historySRV = historySrvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc{};
    historySrvDesc.Format = historyDesc.Format;
    historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    historySrvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtShadowTargets.history.Get(),
        &historySrvDesc,
        m_rtShadowTargets.historySRV.cpu);

    if (!m_rtShadowTargets.historyUAV.IsValid()) {
        auto historyUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (historyUavResult.IsErr()) {
            m_rtShadowTargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT shadow mask history: " + historyUavResult.Error());
        }
        m_rtShadowTargets.historyUAV = historyUavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC historyUavDesc{};
    historyUavDesc.Format = historyDesc.Format;
    historyUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtShadowTargets.history.Get(),
        nullptr,
        &historyUavDesc,
        m_rtShadowTargets.historyUAV.cpu);

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        ID3D12Device* device = m_services.device->GetDevice();
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[3].cpu,
            m_rtShadowTargets.maskSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_rtShadowTargets.historySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_environmentState.shadowAndEnvDescriptors[4].cpu,
                m_rtShadowTargets.historySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    InvalidateRTShadowHistory("resource_recreated");

    return Result<void>::Ok();
}

Result<void> Renderer::CreateRTGIResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT GI creation");
    }

    UINT fullWidth = GetInternalRenderWidth();
    UINT fullHeight = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT GI buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        fullWidth,
        fullHeight);
    const UINT halfWidth = std::max<UINT>(1, fullWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, fullHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    m_rtGITargets.color.Reset();
    m_rtGITargets.colorState = D3D12_RESOURCE_STATE_COMMON;

    m_rtGITargets.history.Reset();
    m_rtGITargets.historyState = D3D12_RESOURCE_STATE_COMMON;
    InvalidateRTGIHistory("resource_recreated");

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_rtGITargets.color));
    if (FAILED(hr)) {
        m_rtGITargets.color.Reset();
        return Result<void>::Err("Failed to create RT GI buffer");
    }

    m_rtGITargets.colorState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!m_rtGITargets.srv.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            m_rtGITargets.color.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT GI buffer: " + srvResult.Error());
        }
        m_rtGITargets.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtGITargets.color.Get(),
        &srvDesc,
        m_rtGITargets.srv.cpu);

    if (!m_rtGITargets.uav.IsValid()) {
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            m_rtGITargets.color.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT GI buffer: " + uavResult.Error());
        }
        m_rtGITargets.uav = uavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtGITargets.color.Get(),
        nullptr,
        &uavDesc,
        m_rtGITargets.uav.cpu);

    ComPtr<ID3D12Resource> history;
    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&history));
    if (FAILED(hr)) {
        m_rtGITargets.color.Reset();
        return Result<void>::Err("Failed to create RT GI history buffer");
    }
    m_rtGITargets.history = history;
    m_rtGITargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!m_rtGITargets.historySRV.IsValid()) {
        auto historySrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (historySrvResult.IsErr()) {
            m_rtGITargets.color.Reset();
            m_rtGITargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT GI history buffer: " + historySrvResult.Error());
        }
        m_rtGITargets.historySRV = historySrvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc{};
    historySrvDesc.Format = desc.Format;
    historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    historySrvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtGITargets.history.Get(),
        &historySrvDesc,
        m_rtGITargets.historySRV.cpu);

    if (!m_rtGITargets.historyUAV.IsValid()) {
        auto historyUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (historyUavResult.IsErr()) {
            m_rtGITargets.color.Reset();
            m_rtGITargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT GI history buffer: " + historyUavResult.Error());
        }
        m_rtGITargets.historyUAV = historyUavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC historyUavDesc{};
    historyUavDesc.Format = desc.Format;
    historyUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtGITargets.history.Get(),
        nullptr,
        &historyUavDesc,
        m_rtGITargets.historyUAV.cpu);

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid() && m_rtGITargets.srv.IsValid()) {
        ID3D12Device* device = m_services.device->GetDevice();
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[5].cpu,
            m_rtGITargets.srv.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_rtGITargets.historySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_environmentState.shadowAndEnvDescriptors[6].cpu,
                m_rtGITargets.historySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    return Result<void>::Ok();
}

Result<void> Renderer::CreateRTReflectionResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT reflection creation");
    }

    UINT baseWidth = GetInternalRenderWidth();
    UINT baseHeight = GetInternalRenderHeight();

    if (m_mainTargets.hdr.resources.color) {
        D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        baseWidth = static_cast<UINT>(hdrDesc.Width);
        baseHeight = static_cast<UINT>(hdrDesc.Height);
    }

    if (baseWidth == 0 || baseHeight == 0) {
        return Result<void>::Err("Render target size is zero; cannot create RT reflection buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        baseWidth,
        baseHeight);
    const UINT halfWidth = std::max<UINT>(1, baseWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, baseHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    m_rtReflectionTargets.color.Reset();
    m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_COMMON;
    m_rtReflectionSignalState.ResetResources();
    m_rtReflectionTargets.history.Reset();
    m_rtReflectionTargets.historyState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_rtReflectionTargets.color));
    if (FAILED(hr)) {
        m_rtReflectionTargets.color.Reset();
        return Result<void>::Err("Failed to create RT reflection color buffer");
    }

    m_rtReflectionTargets.colorState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (!m_rtReflectionTargets.srv.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT reflection buffer: " + srvResult.Error());
        }
        m_rtReflectionTargets.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtReflectionTargets.color.Get(),
        &srvDesc,
        m_rtReflectionTargets.srv.cpu);

    if (!m_rtReflectionTargets.uav.IsValid()) {
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT reflection buffer: " + uavResult.Error());
        }
        m_rtReflectionTargets.uav = uavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtReflectionTargets.color.Get(),
        nullptr,
        &uavDesc,
        m_rtReflectionTargets.uav.cpu);

    D3D12_RESOURCE_DESC statsDesc{};
    statsDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    statsDesc.Width = RTReflectionSignalStats::kStatsBytes;
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
        IID_PPV_ARGS(&m_rtReflectionSignalState.rawResources.statsBuffer));
    if (FAILED(hr)) {
        m_rtReflectionTargets.color.Reset();
        m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
        return Result<void>::Err("Failed to create RT reflection signal stats buffer");
    }

    if (!m_rtReflectionSignalState.rawResources.statsUAV.IsValid()) {
        auto statsUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (statsUavResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
            return Result<void>::Err("Failed to allocate RT reflection signal stats UAV: " +
                                     statsUavResult.Error());
        }
        m_rtReflectionSignalState.rawResources.statsUAV = statsUavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc{};
    statsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    statsUavDesc.Buffer.NumElements = RTReflectionSignalStats::kStatsWords;
    statsUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtReflectionSignalState.rawResources.statsBuffer.Get(),
        nullptr,
        &statsUavDesc,
        m_rtReflectionSignalState.rawResources.statsUAV.cpu);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = statsDesc;
    readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    for (auto& readback : m_rtReflectionSignalState.rawResources.readback) {
        hr = m_services.device->GetDevice()->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback));
        if (FAILED(hr)) {
            readback.Reset();
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
            return Result<void>::Err("Failed to create RT reflection signal stats readback buffer");
        }
    }

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &statsDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_rtReflectionSignalState.historyResources.statsBuffer));
    if (FAILED(hr)) {
        m_rtReflectionTargets.color.Reset();
        m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
        m_rtReflectionSignalState.historyResources.statsBuffer.Reset();
        return Result<void>::Err("Failed to create RT reflection history signal stats buffer");
    }

    if (!m_rtReflectionSignalState.historyResources.statsUAV.IsValid()) {
        auto statsUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (statsUavResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
            m_rtReflectionSignalState.historyResources.statsBuffer.Reset();
            return Result<void>::Err("Failed to allocate RT reflection history signal stats UAV: " +
                                     statsUavResult.Error());
        }
        m_rtReflectionSignalState.historyResources.statsUAV = statsUavResult.Value();
    }

    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtReflectionSignalState.historyResources.statsBuffer.Get(),
        nullptr,
        &statsUavDesc,
        m_rtReflectionSignalState.historyResources.statsUAV.cpu);

    for (auto& readback : m_rtReflectionSignalState.historyResources.readback) {
        hr = m_services.device->GetDevice()->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback));
        if (FAILED(hr)) {
            readback.Reset();
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionSignalState.rawResources.statsBuffer.Reset();
            m_rtReflectionSignalState.historyResources.statsBuffer.Reset();
            return Result<void>::Err("Failed to create RT reflection history signal stats readback buffer");
        }
    }

    ComPtr<ID3D12Resource> reflHistory;
    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&reflHistory));
    if (FAILED(hr)) {
        m_rtReflectionTargets.color.Reset();
        return Result<void>::Err("Failed to create RT reflection history buffer");
    }
    m_rtReflectionTargets.history = reflHistory;
    m_rtReflectionTargets.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (!m_rtReflectionTargets.historySRV.IsValid()) {
        auto reflHistorySrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (reflHistorySrvResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionTargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging SRV for RT reflection history buffer: " + reflHistorySrvResult.Error());
        }
        m_rtReflectionTargets.historySRV = reflHistorySrvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC reflHistorySrvDesc{};
    reflHistorySrvDesc.Format = desc.Format;
    reflHistorySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    reflHistorySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    reflHistorySrvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_rtReflectionTargets.history.Get(),
        &reflHistorySrvDesc,
        m_rtReflectionTargets.historySRV.cpu);

    if (!m_rtReflectionTargets.historyUAV.IsValid()) {
        auto reflHistoryUavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (reflHistoryUavResult.IsErr()) {
            m_rtReflectionTargets.color.Reset();
            m_rtReflectionTargets.history.Reset();
            return Result<void>::Err("Failed to allocate staging UAV for RT reflection history buffer: " + reflHistoryUavResult.Error());
        }
        m_rtReflectionTargets.historyUAV = reflHistoryUavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC reflHistoryUavDesc{};
    reflHistoryUavDesc.Format = desc.Format;
    reflHistoryUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_rtReflectionTargets.history.Get(),
        nullptr,
        &reflHistoryUavDesc,
        m_rtReflectionTargets.historyUAV.cpu);

    InvalidateRTReflectionHistory("resource_recreated");

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
