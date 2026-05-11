#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

#include <cstdio>
#include <functional>
#include <string>

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES MainTargetDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

struct HDRRenderTargetResources {
    ComPtr<ID3D12Resource> color;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        color.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct HDRRenderTargetDescriptors {
    DescriptorHandle rtv;
    DescriptorHandle srv;

    void Reset() {
        rtv = {};
        srv = {};
    }
};

struct GBufferNormalRoughnessResources {
    ComPtr<ID3D12Resource> texture;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        texture.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct GBufferNormalRoughnessDescriptors {
    DescriptorHandle rtv;
    DescriptorHandle srv;

    void Reset() {
        rtv = {};
        srv = {};
    }
};

struct HDRRenderTargetState {
    HDRRenderTargetResources resources;
    HDRRenderTargetDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            UINT width,
                                            UINT height,
                                            float renderScale,
                                            std::function<void(HRESULT)> reportDeviceRemoved) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for HDR target creation");
        }

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
        clearValue.Color[3] = 1.0f;

        const auto heapProps = MainTargetDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&resources.color));

        if (FAILED(hr)) {
            Reset();
            if (reportDeviceRemoved) {
                reportDeviceRemoved(hr);
            }

            char buf[64];
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            char dim[64];
            sprintf_s(dim, "%ux%u", width, height);
            return Result<void>::Err(std::string("Failed to create HDR color target (")
                                     + dim + ", scale=" + std::to_string(renderScale)
                                     + ", hr=" + buf + ")");
        }

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!descriptors.rtv.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for HDR target: " + rtvResult.Error());
            }
            descriptors.rtv = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(resources.color.Get(), &rtvDesc, descriptors.rtv.cpu);

        if (!descriptors.srv.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for HDR target: " + srvResult.Error());
            }
            descriptors.srv = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resources.color.Get(), &srvDesc, descriptors.srv.cpu);

        return Result<void>::Ok();
    }

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct GBufferNormalRoughnessTargetState {
    GBufferNormalRoughnessResources resources;
    GBufferNormalRoughnessDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            UINT width,
                                            UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for normal/roughness G-buffer creation");
        }

        resources.Reset();

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
        clearValue.Color[0] = 0.5f;
        clearValue.Color[1] = 0.5f;
        clearValue.Color[2] = 1.0f;
        clearValue.Color[3] = 1.0f;

        const auto heapProps = MainTargetDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&resources.texture));

        if (FAILED(hr)) {
            resources.Reset();
            return Result<void>::Err("Failed to create normal/roughness G-buffer target");
        }

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!descriptors.rtv.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for normal/roughness G-buffer: " + rtvResult.Error());
            }
            descriptors.rtv = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(resources.texture.Get(), &rtvDesc, descriptors.rtv.cpu);

        if (!descriptors.srv.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for normal/roughness G-buffer: " + srvResult.Error());
            }
            descriptors.srv = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resources.texture.Get(), &srvDesc, descriptors.srv.cpu);

        return Result<void>::Ok();
    }

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct MainRenderTargetState {
    HDRRenderTargetState hdr;
    GBufferNormalRoughnessTargetState normalRoughness;

    void ResetHDR() {
        hdr.Reset();
    }

    void ResetGBufferNormalRoughness() {
        normalRoughness.Reset();
    }

    void ResetResources() {
        ResetHDR();
        ResetGBufferNormalRoughness();
    }
};

} // namespace Cortex::Graphics
