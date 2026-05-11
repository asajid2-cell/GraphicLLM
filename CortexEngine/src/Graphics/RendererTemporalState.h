#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/TemporalManager.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES TemporalMaskDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

inline D3D12_HEAP_PROPERTIES TemporalMaskReadbackHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    return heapProps;
}

struct TemporalMaskPassState {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> statsBuffer;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> statsReadback{};
    DescriptorHandle statsUAV;
    D3D12_RESOURCE_STATES statsState = D3D12_RESOURCE_STATE_COMMON;
    std::array<bool, kFrameCount> statsReadbackPending{};
    std::array<uint64_t, kFrameCount> statsSampleFrame{};

    std::array<std::array<DescriptorHandle, 11>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    std::array<std::array<DescriptorHandle, 11>, kFrameCount> statsSrvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> statsUavTables{};
    bool descriptorTablesValid = false;
    bool statsDescriptorTablesValid = false;
    bool builtThisFrame = false;

    [[nodiscard]] Result<void> CreateResources(
        ID3D12Device* device,
        DescriptorHeapManager* descriptorManager,
        UINT width,
        UINT height,
        const std::function<void(const char*, HRESULT)>& reportDeviceRemoved) {
        if (!device || !descriptorManager) {
            return Result<void>::Err("Renderer not initialized for temporal rejection mask creation");
        }
        if (width == 0 || height == 0) {
            return Result<void>::Err("Window size is zero; cannot create temporal rejection mask");
        }

        ResetResources();

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

        const auto heapProps = TemporalMaskDefaultHeapProperties();
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&texture));
        if (FAILED(hr)) {
            texture.Reset();
            if (reportDeviceRemoved) {
                reportDeviceRemoved("CreateTemporalRejectionMaskResources", hr);
            }
            return Result<void>::Err("Failed to create temporal rejection mask texture");
        }

        if (!srv.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate temporal rejection mask SRV: " + srvResult.Error());
            }
            srv = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &srvDesc, srv.cpu);

        if (!uav.IsValid()) {
            auto uavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (uavResult.IsErr()) {
                return Result<void>::Err("Failed to allocate temporal rejection mask UAV: " + uavResult.Error());
            }
            uav = uavResult.Value();
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(texture.Get(), nullptr, &uavDesc, uav.cpu);

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

        hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &statsDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&statsBuffer));
        if (FAILED(hr)) {
            statsBuffer.Reset();
            if (reportDeviceRemoved) {
                reportDeviceRemoved("CreateTemporalRejectionMaskStats", hr);
            }
            return Result<void>::Err("Failed to create temporal rejection mask stats buffer");
        }

        if (!statsUAV.IsValid()) {
            auto statsUavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (statsUavResult.IsErr()) {
                return Result<void>::Err("Failed to allocate temporal rejection mask stats UAV: " +
                                         statsUavResult.Error());
            }
            statsUAV = statsUavResult.Value();
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC statsUavDesc{};
        statsUavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        statsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        statsUavDesc.Buffer.NumElements = static_cast<UINT>(kStatsBufferBytes / sizeof(uint32_t));
        statsUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(statsBuffer.Get(), nullptr, &statsUavDesc, statsUAV.cpu);

        const auto readbackHeap = TemporalMaskReadbackHeapProperties();
        D3D12_RESOURCE_DESC readbackDesc = statsDesc;
        readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        for (auto& readback : statsReadback) {
            hr = device->CreateCommittedResource(
                &readbackHeap,
                D3D12_HEAP_FLAG_NONE,
                &readbackDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readback));
            if (FAILED(hr)) {
                readback.Reset();
                if (reportDeviceRemoved) {
                    reportDeviceRemoved("CreateTemporalRejectionMaskStatsReadback", hr);
                }
                return Result<void>::Err("Failed to create temporal rejection mask stats readback buffer");
            }
        }

        return Result<void>::Ok();
    }

    void ResetResources() {
        texture.Reset();
        statsBuffer.Reset();
        for (auto& readback : statsReadback) {
            readback.Reset();
        }
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        statsState = D3D12_RESOURCE_STATE_COMMON;
        statsReadbackPending.fill(false);
        statsSampleFrame.fill(0);
        builtThisFrame = false;
    }
};

struct RendererTemporalHistoryState {
    TemporalManager manager;
};

} // namespace Cortex::Graphics
