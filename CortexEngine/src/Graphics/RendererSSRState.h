#pragma once

#include <array>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES SSRDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

struct SSRResources {
    ComPtr<ID3D12Resource> color;
    DescriptorHandle rtv;
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            UINT width,
                                            UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for SSR target creation");
        }

        color.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        const auto heapProps = SSRDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&color));

        if (FAILED(hr)) {
            color.Reset();
            return Result<void>::Err("Failed to create SSR color buffer");
        }

        resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!rtv.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for SSR buffer: " + rtvResult.Error());
            }
            rtv = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(color.Get(), &rtvDesc, rtv.cpu);

        if (!srv.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for SSR buffer: " + srvResult.Error());
            }
            srv = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(color.Get(), &srvDesc, srv.cpu);
        return Result<void>::Ok();
    }

    void ResetResources() {
        color.Reset();
        resourceState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct SSRDescriptorTables {
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> srvTables{};
    bool srvTableValid = false;

    void Reset() {
        srvTableValid = false;
    }
};

struct SSRControls {
    bool enabled = true;
    float maxDistance = 30.0f;
    float thickness = 0.20f;
    float strength = 1.0f;
};

struct SSRFrameState {
    bool activeThisFrame = false;
};

struct SSRPassState {
    SSRControls controls;
    SSRResources resources;
    SSRDescriptorTables descriptors;
    SSRFrameState frame;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
