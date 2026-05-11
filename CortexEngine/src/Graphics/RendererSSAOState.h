#pragma once

#include <algorithm>
#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct SSAOResources {
    ComPtr<ID3D12Resource> texture;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    DescriptorHandle uav;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            uint32_t width,
                                            uint32_t height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for SSAO target creation");
        }

        texture.Reset();

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8_UNORM;
        clearValue.Color[0] = 1.0f;
        clearValue.Color[1] = 1.0f;
        clearValue.Color[2] = 1.0f;
        clearValue.Color[3] = 1.0f;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&texture));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create SSAO render target");
        }

        resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!rtv.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for SSAO target: " + rtvResult.Error());
            }
            rtv = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(texture.Get(), &rtvDesc, rtv.cpu);

        if (!srv.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for SSAO target: " + srvResult.Error());
            }
            srv = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &srvDesc, srv.cpu);

        if (!uav.IsValid()) {
            auto uavResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (uavResult.IsErr()) {
                return Result<void>::Err("Failed to allocate UAV for SSAO target: " + uavResult.Error());
            }
            uav = uavResult.Value();
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(texture.Get(), nullptr, &uavDesc, uav.cpu);
        return Result<void>::Ok();
    }

    void ResetResources() {
        texture.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct SSAODescriptorTables {
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    bool descriptorTablesValid = false;

    void Reset() {
        descriptorTablesValid = false;
    }
};

struct SSAOControls {
    bool enabled = true;
    float radius = 0.25f;
    float bias = 0.03f;
    float intensity = 0.35f;
};

struct SSAOPassState {
    SSAOControls controls;
    SSAOResources resources;
    SSAODescriptorTables descriptors;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
