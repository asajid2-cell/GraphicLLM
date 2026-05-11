#pragma once

#include "Graphics/Renderer_ConstantBuffer.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#include "Scene/VegetationTypes.h"
#include "ShaderTypes.h"
#include "Utils/Result.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Cortex::Graphics {

struct VegetationInstanceBufferState {
    ComPtr<ID3D12Resource> buffer;
    DescriptorHandle srv;
    UINT capacity = 0;
    UINT count = 0;

    void Reset() {
        buffer.Reset();
        srv = {};
        capacity = 0;
        count = 0;
    }

    template <typename InstanceT>
    [[nodiscard]] Result<void> CreateStructuredUploadBuffer(ID3D12Device* device,
                                                            DescriptorHeapManager* descriptorManager,
                                                            UINT newCapacity,
                                                            const char* label) {
        if (!device || newCapacity == 0) {
            return Result<void>::Err(std::string("Failed to create ") + label + " buffer: invalid device or capacity");
        }

        buffer.Reset();
        capacity = 0;
        count = 0;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(InstanceT);
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
            IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            buffer.Reset();
            return Result<void>::Err(std::string("Failed to create ") + label + " buffer");
        }

        capacity = newCapacity;

        if (descriptorManager) {
            if (!srv.IsValid()) {
                auto srvResult = descriptorManager->AllocateCBV_SRV_UAV();
                if (srvResult.IsErr()) {
                    buffer.Reset();
                    capacity = 0;
                    return Result<void>::Err(std::string("Failed to allocate SRV for ") + label +
                                             " buffer: " + srvResult.Error());
                }
                srv = srvResult.Value();
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = newCapacity;
            srvDesc.Buffer.StructureByteStride = sizeof(InstanceT);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            device->CreateShaderResourceView(buffer.Get(), &srvDesc, srv.cpu);
        }

        return Result<void>::Ok();
    }

    template <typename InstanceT>
    [[nodiscard]] Result<void> Upload(const std::vector<InstanceT>& instances, const char* label) {
        count = static_cast<UINT>(instances.size());
        if (count == 0) {
            return Result<void>::Ok();
        }
        if (!buffer || capacity < count) {
            return Result<void>::Err(std::string("Failed to upload ") + label + " instances: buffer is too small");
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        const HRESULT hr = buffer->Map(0, &readRange, &mapped);
        if (FAILED(hr) || !mapped) {
            return Result<void>::Err(std::string("Failed to map ") + label + " instance buffer");
        }

        std::memcpy(mapped, instances.data(), instances.size() * sizeof(InstanceT));
        buffer->Unmap(0, nullptr);
        return Result<void>::Ok();
    }
};

struct VegetationRenderState {
    std::unique_ptr<DX12Pipeline> meshPipeline;
    std::unique_ptr<DX12Pipeline> meshShadowPipeline;
    std::unique_ptr<DX12Pipeline> billboardPipeline;
    std::unique_ptr<DX12Pipeline> grassCardPipeline;
    ConstantBuffer<VegetationConstants> constants;
    VegetationInstanceBufferState meshInstances;
    VegetationInstanceBufferState billboardInstances;
    VegetationInstanceBufferState grassInstances;
    std::shared_ptr<DX12Texture> atlas;
    Scene::WindParams wind;
    Scene::VegetationStats stats;
    bool enabled = true;

    void ResetResources() {
        meshPipeline.reset();
        meshShadowPipeline.reset();
        billboardPipeline.reset();
        grassCardPipeline.reset();
        meshInstances.Reset();
        billboardInstances.Reset();
        grassInstances.Reset();
        atlas.reset();
        wind = {};
        stats = {};
        enabled = true;
    }
};

} // namespace Cortex::Graphics
