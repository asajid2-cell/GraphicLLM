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

struct FullSceneLightingV3TargetResources {
    ComPtr<ID3D12Resource> directLighting;
    ComPtr<ID3D12Resource> directLightingUnshadowed;
    ComPtr<ID3D12Resource> shadowVisibility;
    ComPtr<ID3D12Resource> shadowLoss;
    ComPtr<ID3D12Resource> indirectLighting;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        directLighting.Reset();
        directLightingUnshadowed.Reset();
        shadowVisibility.Reset();
        shadowLoss.Reset();
        indirectLighting.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct FullSceneLightingV3TargetDescriptors {
    DescriptorHandle directLightingRTV;
    DescriptorHandle directLightingSRV;
    DescriptorHandle directLightingUnshadowedRTV;
    DescriptorHandle directLightingUnshadowedSRV;
    DescriptorHandle shadowVisibilityRTV;
    DescriptorHandle shadowVisibilitySRV;
    DescriptorHandle shadowLossRTV;
    DescriptorHandle shadowLossSRV;
    DescriptorHandle indirectLightingRTV;
    DescriptorHandle indirectLightingSRV;

    void Reset() {
        directLightingRTV = {};
        directLightingSRV = {};
        directLightingUnshadowedRTV = {};
        directLightingUnshadowedSRV = {};
        shadowVisibilityRTV = {};
        shadowVisibilitySRV = {};
        shadowLossRTV = {};
        shadowLossSRV = {};
        indirectLightingRTV = {};
        indirectLightingSRV = {};
    }
};

struct FullSceneCandidateBeautyV3TargetResources {
    ComPtr<ID3D12Resource> ldrOutput;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        ldrOutput.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct FullSceneCandidateBeautyV3TargetDescriptors {
    DescriptorHandle ldrOutputRTV;
    DescriptorHandle ldrOutputSRV;

    void Reset() {
        ldrOutputRTV = {};
        ldrOutputSRV = {};
    }
};

struct FullSceneCompositeV3TargetResources {
    ComPtr<ID3D12Resource> hdrSceneColor;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        hdrSceneColor.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct FullSceneCompositeV3TargetDescriptors {
    DescriptorHandle hdrSceneColorRTV;
    DescriptorHandle hdrSceneColorSRV;

    void Reset() {
        hdrSceneColorRTV = {};
        hdrSceneColorSRV = {};
    }
};

struct FullSceneReflectionV3TargetResources {
    ComPtr<ID3D12Resource> radiance;
    ComPtr<ID3D12Resource> confidence;
    ComPtr<ID3D12Resource> sourceId;
    ComPtr<ID3D12Resource> rejectedSourceMask;
    ComPtr<ID3D12Resource> temporalDelta;
    ComPtr<ID3D12Resource> ssrSourceSignal;
    ComPtr<ID3D12Resource> rtSourceSignal;
    ComPtr<ID3D12Resource> historyCurr;
    ComPtr<ID3D12Resource> historyPrev;
    ComPtr<ID3D12Resource> historyValidity;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES radianceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES confidenceState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES sourceIdState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES rejectedSourceMaskState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES temporalDeltaState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES ssrSourceSignalState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES rtSourceSignalState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES historyCurrState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES historyPrevState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES historyValidityState = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        radiance.Reset();
        confidence.Reset();
        sourceId.Reset();
        rejectedSourceMask.Reset();
        temporalDelta.Reset();
        ssrSourceSignal.Reset();
        rtSourceSignal.Reset();
        historyCurr.Reset();
        historyPrev.Reset();
        historyValidity.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
        radianceState = D3D12_RESOURCE_STATE_COMMON;
        confidenceState = D3D12_RESOURCE_STATE_COMMON;
        sourceIdState = D3D12_RESOURCE_STATE_COMMON;
        rejectedSourceMaskState = D3D12_RESOURCE_STATE_COMMON;
        temporalDeltaState = D3D12_RESOURCE_STATE_COMMON;
        ssrSourceSignalState = D3D12_RESOURCE_STATE_COMMON;
        rtSourceSignalState = D3D12_RESOURCE_STATE_COMMON;
        historyCurrState = D3D12_RESOURCE_STATE_COMMON;
        historyPrevState = D3D12_RESOURCE_STATE_COMMON;
        historyValidityState = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct FullSceneReflectionV3TargetDescriptors {
    DescriptorHandle radianceRTV;
    DescriptorHandle radianceSRV;
    DescriptorHandle confidenceRTV;
    DescriptorHandle confidenceSRV;
    DescriptorHandle sourceIdRTV;
    DescriptorHandle sourceIdSRV;
    DescriptorHandle rejectedSourceMaskRTV;
    DescriptorHandle rejectedSourceMaskSRV;
    DescriptorHandle temporalDeltaRTV;
    DescriptorHandle temporalDeltaSRV;
    DescriptorHandle ssrSourceSignalRTV;
    DescriptorHandle ssrSourceSignalSRV;
    DescriptorHandle rtSourceSignalRTV;
    DescriptorHandle rtSourceSignalSRV;
    DescriptorHandle historyCurrRTV;
    DescriptorHandle historyCurrSRV;
    DescriptorHandle historyPrevRTV;
    DescriptorHandle historyPrevSRV;
    DescriptorHandle historyValidityRTV;
    DescriptorHandle historyValiditySRV;

    void Reset() {
        radianceRTV = {};
        radianceSRV = {};
        confidenceRTV = {};
        confidenceSRV = {};
        sourceIdRTV = {};
        sourceIdSRV = {};
        rejectedSourceMaskRTV = {};
        rejectedSourceMaskSRV = {};
        temporalDeltaRTV = {};
        temporalDeltaSRV = {};
        ssrSourceSignalRTV = {};
        ssrSourceSignalSRV = {};
        rtSourceSignalRTV = {};
        rtSourceSignalSRV = {};
        historyCurrRTV = {};
        historyCurrSRV = {};
        historyPrevRTV = {};
        historyPrevSRV = {};
        historyValidityRTV = {};
        historyValiditySRV = {};
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

struct FullSceneCandidateBeautyV3TargetState {
    FullSceneCandidateBeautyV3TargetResources resources;
    FullSceneCandidateBeautyV3TargetDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            UINT width,
                                            UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for FullSceneCandidateBeautyV3 target creation");
        }

        resources.Reset();

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
            IID_PPV_ARGS(&resources.ldrOutput));
        if (FAILED(hr)) {
            resources.Reset();
            return Result<void>::Err("Failed to create FullSceneCandidateBeautyV3 LDR target");
        }

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!descriptors.ldrOutputRTV.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate RTV for FullSceneCandidateBeautyV3 target: ") +
                                         rtvResult.Error());
            }
            descriptors.ldrOutputRTV = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(resources.ldrOutput.Get(), &rtvDesc, descriptors.ldrOutputRTV.cpu);

        if (!descriptors.ldrOutputSRV.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate SRV for FullSceneCandidateBeautyV3 target: ") +
                                         srvResult.Error());
            }
            descriptors.ldrOutputSRV = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resources.ldrOutput.Get(), &srvDesc, descriptors.ldrOutputSRV.cpu);

        return Result<void>::Ok();
    }

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct FullSceneCompositeV3TargetState {
    FullSceneCompositeV3TargetResources resources;
    FullSceneCompositeV3TargetDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTarget(ID3D12Device* device,
                                            DescriptorHeapManager* descriptorManager,
                                            UINT width,
                                            UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for FullSceneCompositeV3 target creation");
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
            IID_PPV_ARGS(&resources.hdrSceneColor));
        if (FAILED(hr)) {
            resources.Reset();
            return Result<void>::Err("Failed to create FullSceneCompositeV3 HDR scene-color target");
        }

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (!descriptors.hdrSceneColorRTV.IsValid()) {
            auto rtvResult = descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate RTV for FullSceneCompositeV3 target: ") +
                                         rtvResult.Error());
            }
            descriptors.hdrSceneColorRTV = rtvResult.Value();
        }

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(resources.hdrSceneColor.Get(), &rtvDesc, descriptors.hdrSceneColorRTV.cpu);

        if (!descriptors.hdrSceneColorSRV.IsValid()) {
            auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err(std::string("Failed to allocate SRV for FullSceneCompositeV3 target: ") +
                                         srvResult.Error());
            }
            descriptors.hdrSceneColorSRV = srvResult.Value();
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resources.hdrSceneColor.Get(), &srvDesc, descriptors.hdrSceneColorSRV.cpu);

        return Result<void>::Ok();
    }

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct FullSceneReflectionV3TargetState {
    FullSceneReflectionV3TargetResources resources;
    FullSceneReflectionV3TargetDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTargets(ID3D12Device* device,
                                             DescriptorHeapManager* descriptorManager,
                                             UINT width,
                                             UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for FullSceneReflectionV3 target creation");
        }

        resources.Reset();

        auto createTarget = [&](const char* label,
                                ComPtr<ID3D12Resource>& target,
                                DescriptorHandle& rtv,
                                DescriptorHandle& srv) -> Result<void> {
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
                IID_PPV_ARGS(&target));
            if (FAILED(hr)) {
                resources.Reset();
                return Result<void>::Err(std::string("Failed to create FullSceneReflectionV3 target: ") + label);
            }

            if (!rtv.IsValid()) {
                auto rtvResult = descriptorManager->AllocateRTV();
                if (rtvResult.IsErr()) {
                    return Result<void>::Err(std::string("Failed to allocate RTV for FullSceneReflectionV3 target: ") +
                                             label + ": " + rtvResult.Error());
                }
                rtv = rtvResult.Value();
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = desc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(target.Get(), &rtvDesc, rtv.cpu);

            if (!srv.IsValid()) {
                auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    return Result<void>::Err(std::string("Failed to allocate SRV for FullSceneReflectionV3 target: ") +
                                             label + ": " + srvResult.Error());
                }
                srv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.Get(), &srvDesc, srv.cpu);

            return Result<void>::Ok();
        };

        auto radiance = createTarget("reflection_radiance",
                                     resources.radiance,
                                     descriptors.radianceRTV,
                                     descriptors.radianceSRV);
        if (radiance.IsErr()) return radiance;

        auto confidence = createTarget("reflection_confidence",
                                       resources.confidence,
                                       descriptors.confidenceRTV,
                                       descriptors.confidenceSRV);
        if (confidence.IsErr()) return confidence;

        auto sourceId = createTarget("reflection_source_id",
                                     resources.sourceId,
                                     descriptors.sourceIdRTV,
                                     descriptors.sourceIdSRV);
        if (sourceId.IsErr()) return sourceId;

        auto rejected = createTarget("reflection_rejected_source_mask",
                                     resources.rejectedSourceMask,
                                     descriptors.rejectedSourceMaskRTV,
                                     descriptors.rejectedSourceMaskSRV);
        if (rejected.IsErr()) return rejected;

        auto temporal = createTarget("reflection_temporal_delta",
                                     resources.temporalDelta,
                                     descriptors.temporalDeltaRTV,
                                     descriptors.temporalDeltaSRV);
        if (temporal.IsErr()) return temporal;

        auto ssrSignal = createTarget("reflection_ssr_source_signal",
                                      resources.ssrSourceSignal,
                                      descriptors.ssrSourceSignalRTV,
                                      descriptors.ssrSourceSignalSRV);
        if (ssrSignal.IsErr()) return ssrSignal;

        auto rtSignal = createTarget("reflection_rt_source_signal",
                                     resources.rtSourceSignal,
                                     descriptors.rtSourceSignalRTV,
                                     descriptors.rtSourceSignalSRV);
        if (rtSignal.IsErr()) return rtSignal;

        auto historyCurr = createTarget("reflection_history_v3_curr",
                                        resources.historyCurr,
                                        descriptors.historyCurrRTV,
                                        descriptors.historyCurrSRV);
        if (historyCurr.IsErr()) return historyCurr;

        auto historyPrev = createTarget("reflection_history_v3_prev",
                                        resources.historyPrev,
                                        descriptors.historyPrevRTV,
                                        descriptors.historyPrevSRV);
        if (historyPrev.IsErr()) return historyPrev;

        auto historyValidity = createTarget("reflection_history_v3_validity",
                                            resources.historyValidity,
                                            descriptors.historyValidityRTV,
                                            descriptors.historyValiditySRV);
        if (historyValidity.IsErr()) return historyValidity;

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.radianceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.confidenceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.sourceIdState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.rejectedSourceMaskState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.temporalDeltaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.ssrSourceSignalState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.rtSourceSignalState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.historyCurrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.historyPrevState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        resources.historyValidityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return Result<void>::Ok();
    }

    void Reset() {
        resources.Reset();
        descriptors.Reset();
    }
};

struct FullSceneLightingV3TargetState {
    FullSceneLightingV3TargetResources resources;
    FullSceneLightingV3TargetDescriptors descriptors;

    [[nodiscard]] Result<void> CreateTargets(ID3D12Device* device,
                                             DescriptorHeapManager* descriptorManager,
                                             UINT width,
                                             UINT height) {
        if (!device || !descriptorManager || width == 0 || height == 0) {
            return Result<void>::Err("Renderer not initialized for FullSceneLightingV3 target creation");
        }

        resources.Reset();

        auto createTarget = [&](const char* label,
                                ComPtr<ID3D12Resource>& target,
                                DescriptorHandle& rtv,
                                DescriptorHandle& srv) -> Result<void> {
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
                IID_PPV_ARGS(&target));
            if (FAILED(hr)) {
                resources.Reset();
                return Result<void>::Err(std::string("Failed to create FullSceneLightingV3 target: ") + label);
            }

            if (!rtv.IsValid()) {
                auto rtvResult = descriptorManager->AllocateRTV();
                if (rtvResult.IsErr()) {
                    return Result<void>::Err(std::string("Failed to allocate RTV for FullSceneLightingV3 target: ") +
                                             label + ": " + rtvResult.Error());
                }
                rtv = rtvResult.Value();
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = desc.Format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(target.Get(), &rtvDesc, rtv.cpu);

            if (!srv.IsValid()) {
                auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    return Result<void>::Err(std::string("Failed to allocate SRV for FullSceneLightingV3 target: ") +
                                             label + ": " + srvResult.Error());
                }
                srv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(target.Get(), &srvDesc, srv.cpu);

            return Result<void>::Ok();
        };

        auto direct = createTarget("direct_lighting",
                                   resources.directLighting,
                                   descriptors.directLightingRTV,
                                   descriptors.directLightingSRV);
        if (direct.IsErr()) return direct;

        auto unshadowed = createTarget("direct_lighting_unshadowed",
                                       resources.directLightingUnshadowed,
                                       descriptors.directLightingUnshadowedRTV,
                                       descriptors.directLightingUnshadowedSRV);
        if (unshadowed.IsErr()) return unshadowed;

        auto visibility = createTarget("shadow_visibility",
                                       resources.shadowVisibility,
                                       descriptors.shadowVisibilityRTV,
                                       descriptors.shadowVisibilitySRV);
        if (visibility.IsErr()) return visibility;

        auto loss = createTarget("shadow_loss",
                                 resources.shadowLoss,
                                 descriptors.shadowLossRTV,
                                 descriptors.shadowLossSRV);
        if (loss.IsErr()) return loss;

        auto indirect = createTarget("indirect_lighting",
                                     resources.indirectLighting,
                                     descriptors.indirectLightingRTV,
                                     descriptors.indirectLightingSRV);
        if (indirect.IsErr()) return indirect;

        resources.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
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
    FullSceneLightingV3TargetState lightingV3;
    FullSceneReflectionV3TargetState reflectionV3;
    FullSceneCompositeV3TargetState compositeV3;
    FullSceneCandidateBeautyV3TargetState candidateBeautyV3;

    void ResetHDR() {
        hdr.Reset();
    }

    void ResetGBufferNormalRoughness() {
        normalRoughness.Reset();
    }

    void ResetLightingV3() {
        lightingV3.Reset();
    }

    void ResetReflectionV3() {
        reflectionV3.Reset();
    }

    void ResetCompositeV3() {
        compositeV3.Reset();
    }

    void ResetCandidateBeautyV3() {
        candidateBeautyV3.Reset();
    }

    void ResetResources() {
        ResetHDR();
        ResetGBufferNormalRoughness();
        ResetLightingV3();
        ResetReflectionV3();
        ResetCompositeV3();
        ResetCandidateBeautyV3();
    }
};

} // namespace Cortex::Graphics
