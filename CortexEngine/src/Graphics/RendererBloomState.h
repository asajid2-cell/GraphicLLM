#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

template <uint32_t BloomLevels>
struct BloomPyramidResources {
    ComPtr<ID3D12Resource> texA[BloomLevels];
    ComPtr<ID3D12Resource> texB[BloomLevels];
    DescriptorHandle rtv[BloomLevels][2];
    DescriptorHandle srv[BloomLevels][2];
    DescriptorHandle graphRtv[BloomLevels][2];
    DescriptorHandle graphSrv[BloomLevels][2];
    D3D12_RESOURCE_STATES resourceState[BloomLevels][2] = {};
    uint32_t activeLevels = BloomLevels;
    DescriptorHandle combinedSrv;
    ID3D12Resource* postProcessOverride = nullptr;

    [[nodiscard]] Result<void> CreateTargets(ID3D12Device* device,
                                             DescriptorHeapManager* descriptorManager,
                                             uint32_t fullWidth,
                                             uint32_t fullHeight,
                                             uint32_t requestedLevels) {
        if (!device || !descriptorManager || fullWidth == 0 || fullHeight == 0) {
            return Result<void>::Err("Renderer not initialized for bloom target creation");
        }

        activeLevels = std::clamp<uint32_t>(requestedLevels, 1u, BloomLevels);

        for (uint32_t level = 0; level < activeLevels; ++level) {
            texA[level].Reset();
            texB[level].Reset();
            resourceState[level][0] = D3D12_RESOURCE_STATE_COMMON;
            resourceState[level][1] = D3D12_RESOURCE_STATE_COMMON;
        }

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        for (uint32_t level = 0; level < activeLevels; ++level) {
            const UINT div = 1u << (level + 1u);
            const UINT width = std::max<UINT>(1u, fullWidth / div);
            const UINT height = std::max<UINT>(1u, fullHeight / div);

            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = desc.Format;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 0.0f;

            for (uint32_t ping = 0; ping < 2u; ++ping) {
                ComPtr<ID3D12Resource>& tex = (ping == 0u) ? texA[level] : texB[level];
                const HRESULT hr = device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    &clearValue,
                    IID_PPV_ARGS(&tex));
                if (FAILED(hr)) {
                    return Result<void>::Err("Failed to create bloom render target");
                }

                resourceState[level][ping] = D3D12_RESOURCE_STATE_RENDER_TARGET;

                if (!rtv[level][ping].IsValid()) {
                    auto rtvResult = descriptorManager->AllocateRTV();
                    if (rtvResult.IsErr()) {
                        return Result<void>::Err("Failed to allocate RTV for bloom target: " + rtvResult.Error());
                    }
                    rtv[level][ping] = rtvResult.Value();
                }

                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                rtvDesc.Format = desc.Format;
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                device->CreateRenderTargetView(tex.Get(), &rtvDesc, rtv[level][ping].cpu);

                if (!srv[level][ping].IsValid()) {
                    auto srvResult = descriptorManager->AllocateStagingCBV_SRV_UAV();
                    if (srvResult.IsErr()) {
                        return Result<void>::Err("Failed to allocate staging SRV for bloom target: " + srvResult.Error());
                    }
                    srv[level][ping] = srvResult.Value();
                }

                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(tex.Get(), &srvDesc, srv[level][ping].cpu);
            }
        }

        const uint32_t combinedLevel = (activeLevels > 1u) ? 1u : 0u;
        combinedSrv = srv[combinedLevel][0];
        return Result<void>::Ok();
    }

    void ResetResources() {
        for (uint32_t level = 0; level < BloomLevels; ++level) {
            texA[level].Reset();
            texB[level].Reset();
            resourceState[level][0] = D3D12_RESOURCE_STATE_COMMON;
            resourceState[level][1] = D3D12_RESOURCE_STATE_COMMON;
        }
        activeLevels = BloomLevels;
        combinedSrv = {};
        postProcessOverride = nullptr;
    }
};

template <uint32_t BloomDescriptorSlots>
struct BloomDescriptorTables {
    std::array<std::array<DescriptorHandle, BloomDescriptorSlots>, kFrameCount> srvTables{};
    bool srvTableValid = false;

    void Reset() {
        srvTableValid = false;
    }
};

struct BloomPassControls {
    float intensity = 0.25f;
    float threshold = 1.0f;
    float softKnee = 0.5f;
    float maxContribution = 4.0f;
};

template <uint32_t BloomLevels, uint32_t BloomDescriptorSlots>
struct BloomPassState {
    BloomPassControls controls;
    BloomPyramidResources<BloomLevels> resources;
    BloomDescriptorTables<BloomDescriptorSlots> descriptors;

    void ResetResources() {
        resources.ResetResources();
        descriptors.Reset();
    }
};

} // namespace Cortex::Graphics
