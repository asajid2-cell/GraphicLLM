#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES TemporalScreenDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

struct TemporalAAState {
    bool enabled = true; // Re-enabled now that memory issues are fixed
    float blendFactor = 0.06f;
    bool cameraIsMoving = false;
    glm::vec2 jitterPrevPixels{0.0f, 0.0f};
    glm::vec2 jitterCurrPixels{0.0f, 0.0f};
    uint32_t sampleIndex = 0;
    glm::mat4 prevViewProjMatrix{1.0f};
    bool hasPrevViewProj = false;

    void ResetAccumulation() {
        sampleIndex = 0;
        jitterPrevPixels = glm::vec2(0.0f);
        jitterCurrPixels = glm::vec2(0.0f);
        hasPrevViewProj = false;
    }
};

struct TemporalScreenPassState {
    ComPtr<ID3D12Resource> velocityBuffer;
    DescriptorHandle velocityRTV;
    DescriptorHandle velocitySRV;
    D3D12_RESOURCE_STATES velocityState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> historyColor;
    DescriptorHandle historySRV;
    D3D12_RESOURCE_STATES historyState = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> taaIntermediate;
    DescriptorHandle taaIntermediateRTV;
    D3D12_RESOURCE_STATES taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

    std::array<std::array<DescriptorHandle, 13>, kFrameCount> taaResolveSrvTables{};
    bool taaResolveSrvTableValid = false;
    std::array<std::array<DescriptorHandle, 10>, kFrameCount> motionVectorSrvTables{};
    bool motionVectorSrvTableValid = false;
    std::array<std::array<DescriptorHandle, 13>, kFrameCount> postProcessSrvTables{};
    bool postProcessSrvTableValid = false;

    [[nodiscard]] Result<void> CreateHistoryColor(ID3D12Device* device,
                                                  DescriptorHeapManager* descriptorManager,
                                                  UINT width,
                                                  UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for TAA history creation");
        }

        historyColor.Reset();
        historyState = D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        const auto heapProps = TemporalScreenDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&historyColor));

        if (FAILED(hr)) {
            historyColor.Reset();
            return Result<void>::Err("Failed to create TAA history buffer");
        }

        historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        if (!historySRV.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for TAA history: " + srvResult.Error());
            }
            historySRV = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(historyColor.Get(), &srvDesc, historySRV.cpu);
        return Result<void>::Ok();
    }

    [[nodiscard]] Result<void> CreateTAAIntermediate(ID3D12Device* device,
                                                     DescriptorHeapManager* descriptorManager,
                                                     UINT width,
                                                     UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for TAA intermediate creation");
        }

        taaIntermediate.Reset();
        taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

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

        const auto heapProps = TemporalScreenDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&taaIntermediate));

        if (FAILED(hr)) {
            taaIntermediate.Reset();
            return Result<void>::Err("Failed to create TAA intermediate HDR target");
        }

        taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!taaIntermediateRTV.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for TAA intermediate: " + rtvResult.Error());
            }
            taaIntermediateRTV = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(taaIntermediate.Get(), &rtvDesc, taaIntermediateRTV.cpu);
        return Result<void>::Ok();
    }

    [[nodiscard]] Result<void> CreateVelocityBuffer(ID3D12Device* device,
                                                    DescriptorHeapManager* descriptorManager,
                                                    UINT width,
                                                    UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for motion vector buffer creation");
        }

        velocityBuffer.Reset();
        velocityState = D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = desc.Format;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 0.0f;

        const auto heapProps = TemporalScreenDefaultHeapProperties();
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&velocityBuffer));

        if (FAILED(hr)) {
            velocityBuffer.Reset();
            return Result<void>::Err("Failed to create motion vector buffer");
        }

        velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!velocityRTV.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate RTV for motion vector buffer: " + rtvResult.Error());
            }
            velocityRTV = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(velocityBuffer.Get(), &rtvDesc, velocityRTV.cpu);

        if (!velocitySRV.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate staging SRV for motion vector buffer: " + srvResult.Error());
            }
            velocitySRV = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(velocityBuffer.Get(), &srvDesc, velocitySRV.cpu);
        return Result<void>::Ok();
    }

    void ResetResources() {
        velocityBuffer.Reset();
        velocityState = D3D12_RESOURCE_STATE_COMMON;
        historyColor.Reset();
        historyState = D3D12_RESOURCE_STATE_COMMON;
        taaIntermediate.Reset();
        taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;
        taaResolveSrvTableValid = false;
        motionVectorSrvTableValid = false;
        postProcessSrvTableValid = false;
    }
};

} // namespace Cortex::Graphics
