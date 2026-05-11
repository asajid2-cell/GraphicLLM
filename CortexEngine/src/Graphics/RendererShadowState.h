#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

template <uint32_t ShadowArraySize>
struct ShadowMapResources {
    ComPtr<ID3D12Resource> map;
    std::array<DescriptorHandle, ShadowArraySize> dsvs{};
    DescriptorHandle srv;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    bool initializedForEditor = false;

    [[nodiscard]] Result<void> CreateMap(ID3D12Device* device,
                                         DescriptorHeapManager* descriptorManager,
                                         UINT shadowDim) {
        if (!device || !descriptorManager || shadowDim == 0) {
            return Result<void>::Err("Renderer not initialized for shadow map creation");
        }

        D3D12_RESOURCE_DESC shadowDesc = {};
        shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        shadowDesc.Width = shadowDim;
        shadowDesc.Height = shadowDim;
        shadowDesc.DepthOrArraySize = ShadowArraySize;
        shadowDesc.MipLevels = 1;
        shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        shadowDesc.SampleDesc.Count = 1;
        shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        clearValue.DepthStencil.Depth = 1.0f;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &shadowDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&map));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create shadow map resource");
        }

        resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

        for (uint32_t i = 0; i < ShadowArraySize; ++i) {
            auto dsvResult = descriptorManager->AllocateDSV();
            if (dsvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate DSV for shadow cascade: " + dsvResult.Error());
            }
            dsvs[i] = dsvResult.Value();

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.MipSlice = 0;
            dsvDesc.Texture2DArray.FirstArraySlice = i;
            dsvDesc.Texture2DArray.ArraySize = 1;
            dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
            device->CreateDepthStencilView(map.Get(), &dsvDesc, dsvs[i].cpu);
        }

        auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for shadow map: " + srvResult.Error());
        }
        srv = srvResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = ShadowArraySize;
        device->CreateShaderResourceView(map.Get(), &srvDesc, srv.cpu);
        return Result<void>::Ok();
    }

    void Reset() {
        map.Reset();
        srv = {};
        for (auto& dsv : dsvs) {
            dsv = {};
        }
        resourceState = D3D12_RESOURCE_STATE_COMMON;
        initializedForEditor = false;
    }
};

struct ShadowMapRasterState {
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
};

template <uint32_t ShadowCascadeCount = 3>
struct ShadowMapControls {
    bool enabled = true;
    bool pcssEnabled = false;
    float mapSize = 2048.0f;
    float bias = 0.002f; // Increased for terrain (was 0.0005f)
    float pcfRadius = 1.5f;
    float cascadeSplitLambda = 0.5f;
    std::array<float, ShadowCascadeCount> cascadeResolutionScale{1.0f, 1.0f, 1.0f};
};

template <uint32_t ShadowArraySize, uint32_t ShadowCascadeCount = 3>
struct ShadowMapPassState {
    ShadowMapResources<ShadowArraySize> resources;
    ShadowMapRasterState raster;
    ShadowMapControls<ShadowCascadeCount> controls;

    void ResetResources() {
        resources.Reset();
    }
};

template <uint32_t ShadowCascadeCount = 3>
struct ShadowCascadeFrameState {
    float orthoRange = 20.0f;
    float nearPlane = 1.0f;
    float farPlane = 100.0f;

    glm::mat4 lightViewMatrix{1.0f};
    std::array<glm::mat4, ShadowCascadeCount> lightProjectionMatrices{};
    std::array<glm::mat4, ShadowCascadeCount> lightViewProjectionMatrices{};
    std::array<float, ShadowCascadeCount> cascadeSplits{};
};

} // namespace Cortex::Graphics
