#pragma once

#include <array>
#include <cstdint>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

inline D3D12_HEAP_PROPERTIES VolumetricDefaultHeapProperties() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    return heapProps;
}

struct VolumetricFroxelResource {
    ComPtr<ID3D12Resource> texture;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Reset() {
        texture.Reset();
        state = D3D12_RESOURCE_STATE_COMMON;
    }
};

struct RendererVolumetricState {
    static constexpr uint32_t kFroxelWidth = 160;
    static constexpr uint32_t kFroxelHeight = 90;
    static constexpr uint32_t kFroxelDepth = 64;
    static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    bool enabled = true;
    bool resourcesValid = false;
    bool historyValid = false;
    uint32_t historyWriteIndex = 0;

    VolumetricFroxelResource injected;
    VolumetricFroxelResource integrated;
    std::array<VolumetricFroxelResource, 2> history;

    std::array<std::array<DescriptorHandle, 11>, kFrameCount> srvTables{};
    std::array<std::array<DescriptorHandle, 4>, kFrameCount> uavTables{};
    bool descriptorTablesValid = false;

    [[nodiscard]] Result<void> CreateResources(ID3D12Device* device) {
        if (!device) {
            return Result<void>::Err("Renderer not initialized for volumetric froxel resources");
        }

        ResetResourcesOnly();

        auto createTexture = [&](VolumetricFroxelResource& dst, const wchar_t* name) -> Result<void> {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            desc.Width = kFroxelWidth;
            desc.Height = kFroxelHeight;
            desc.DepthOrArraySize = static_cast<UINT16>(kFroxelDepth);
            desc.MipLevels = 1;
            desc.Format = kFormat;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            const auto heapProps = VolumetricDefaultHeapProperties();
            HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                nullptr,
                IID_PPV_ARGS(&dst.texture));
            if (FAILED(hr)) {
                dst.Reset();
                return Result<void>::Err("Failed to create volumetric froxel texture");
            }
            dst.texture->SetName(name);
            dst.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            return Result<void>::Ok();
        };

        auto scatterResult = createTexture(injected, L"VolumetricFroxelInjected");
        if (scatterResult.IsErr()) return scatterResult;
        auto integratedResult = createTexture(integrated, L"VolumetricFroxelIntegrated");
        if (integratedResult.IsErr()) return integratedResult;
        auto history0Result = createTexture(history[0], L"VolumetricFroxelHistory0");
        if (history0Result.IsErr()) return history0Result;
        auto history1Result = createTexture(history[1], L"VolumetricFroxelHistory1");
        if (history1Result.IsErr()) return history1Result;

        resourcesValid = true;
        historyValid = false;
        historyWriteIndex = 0;
        return Result<void>::Ok();
    }

    void ResetResourcesOnly() {
        injected.Reset();
        integrated.Reset();
        for (auto& h : history) {
            h.Reset();
        }
        resourcesValid = false;
        historyValid = false;
    }

    void ResetDescriptors() {
        descriptorTablesValid = false;
        for (auto& table : srvTables) {
            for (auto& handle : table) {
                handle = {};
            }
        }
        for (auto& table : uavTables) {
            for (auto& handle : table) {
                handle = {};
            }
        }
    }
};

} // namespace Cortex::Graphics
