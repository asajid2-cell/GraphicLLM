#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct VoxelRenderState {
    bool backendEnabled = false;

    std::vector<uint32_t> gridCPU;
    uint32_t gridDim = 384;
    bool gridDirty = true;
    ComPtr<ID3D12Resource> gridBuffer;
    DescriptorHandle gridSRV{};

    std::unordered_map<std::string, uint8_t> materialIds;
    uint8_t nextMaterialId = 1;

    void ResetGrid() {
        gridCPU.clear();
        gridDirty = true;
        gridBuffer.Reset();
        gridSRV = {};
        materialIds.clear();
        nextMaterialId = 1;
    }

    void ResetMaterialPalette() {
        materialIds.clear();
        nextMaterialId = 1;
    }

    [[nodiscard]] Result<void> UploadGridToGPU(ID3D12Device* device,
                                               DescriptorHeapManager* descriptorManager) {
        if (gridCPU.empty()) {
            return Result<void>::Ok();
        }
        if (!device) {
            return Result<void>::Err("UploadVoxelGridToGPU: device is null");
        }

        const UINT64 byteSize = static_cast<UINT64>(gridCPU.size() * sizeof(uint32_t));

        bool recreate = !gridBuffer;
        if (gridBuffer) {
            const auto desc = gridBuffer->GetDesc();
            recreate = desc.Width < byteSize;
        }

        if (recreate) {
            gridBuffer.Reset();

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = byteSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;

            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&gridBuffer));
            if (FAILED(hr)) {
                gridBuffer.Reset();
                return Result<void>::Err("Failed to create voxel grid buffer");
            }

            if (!gridSRV.IsValid() && descriptorManager) {
                auto srvResult = descriptorManager->AllocateCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    gridBuffer.Reset();
                    return Result<void>::Err("Failed to allocate SRV for voxel grid: " + srvResult.Error());
                }
                gridSRV = srvResult.Value();
            }

            if (gridSRV.IsValid()) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements = static_cast<UINT>(gridCPU.size());
                srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
                srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

                device->CreateShaderResourceView(gridBuffer.Get(), &srvDesc, gridSRV.cpu);
            }
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        const HRESULT mapHr = gridBuffer->Map(0, &readRange, &mapped);
        if (FAILED(mapHr) || !mapped) {
            return Result<void>::Err("Failed to map voxel grid buffer");
        }

        std::memcpy(mapped, gridCPU.data(), static_cast<size_t>(byteSize));
        gridBuffer->Unmap(0, nullptr);
        return Result<void>::Ok();
    }
};

} // namespace Cortex::Graphics
