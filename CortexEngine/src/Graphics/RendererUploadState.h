#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Graphics/MeshBuffers.h"
#include "Graphics/Renderer_ConstantBuffer.h"
#include "Graphics/ShaderTypes.h"
#include "RHI/DescriptorHeap.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

struct UploadCommandPoolState {
    static constexpr uint32_t kPoolSize = 4;

    std::array<ComPtr<ID3D12CommandAllocator>, kPoolSize> commandAllocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, kPoolSize> commandLists;
    uint32_t allocatorIndex = 0;
    std::array<uint64_t, kPoolSize> fences{};
    uint64_t pendingFence = 0;

    void ResetFenceBookkeeping() {
        fences.fill(0);
        pendingFence = 0;
    }

    void ResetResources() {
        for (auto& allocator : commandAllocators) {
            allocator.Reset();
        }
        for (auto& list : commandLists) {
            list.Reset();
        }
        allocatorIndex = 0;
        ResetFenceBookkeeping();
    }
};

struct MeshUploadResourceError {
    std::string message;
    HRESULT hr = S_OK;
    const char* deviceRemovedContext = nullptr;
};

struct MeshUploadResourceBundle {
    std::shared_ptr<MeshBuffers> gpuBuffers;
    ComPtr<ID3D12Resource> vertexUpload;
    ComPtr<ID3D12Resource> indexUpload;
    UINT64 vertexBytes = 0;
    UINT64 indexBytes = 0;
    std::string descriptorWarning;
};

struct MeshUploadResourceState {
    [[nodiscard]] static Result<void> EnsureRawSRVs(ID3D12Device* device,
                                                    DescriptorHeapManager* descriptorManager,
                                                    MeshBuffers& buffers) {
        if (buffers.vbRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex &&
            buffers.ibRawSRVIndex != MeshBuffers::kInvalidDescriptorIndex) {
            return Result<void>::Ok();
        }
        if (!device || !descriptorManager || !buffers.vertexBuffer || !buffers.indexBuffer) {
            return Result<void>::Err("Mesh raw SRV creation requires a device, descriptor manager, and vertex/index buffers");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC rawSrv{};
        rawSrv.Format = DXGI_FORMAT_R32_TYPELESS;
        rawSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        rawSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        rawSrv.Buffer.FirstElement = 0;
        rawSrv.Buffer.StructureByteStride = 0;
        rawSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

        const UINT64 vertexBytes = buffers.vertexBuffer->GetDesc().Width;
        const UINT64 indexBytes = buffers.indexBuffer->GetDesc().Width;

        auto vbSrvResult = descriptorManager->AllocateCBV_SRV_UAV();
        auto ibSrvResult = descriptorManager->AllocateCBV_SRV_UAV();
        if (vbSrvResult.IsErr() || ibSrvResult.IsErr()) {
            return Result<void>::Err("failed to allocate persistent mesh SRVs (vb=" +
                                     std::string(vbSrvResult.IsErr() ? vbSrvResult.Error() : "ok") +
                                     ", ib=" +
                                     std::string(ibSrvResult.IsErr() ? ibSrvResult.Error() : "ok") +
                                     ")");
        }

        DescriptorHandle vbSrv = vbSrvResult.Value();
        DescriptorHandle ibSrv = ibSrvResult.Value();

        rawSrv.Buffer.NumElements = static_cast<UINT>(vertexBytes / 4u);
        device->CreateShaderResourceView(buffers.vertexBuffer.Get(), &rawSrv, vbSrv.cpu);

        rawSrv.Buffer.NumElements = static_cast<UINT>(indexBytes / 4u);
        device->CreateShaderResourceView(buffers.indexBuffer.Get(), &rawSrv, ibSrv.cpu);

        buffers.vbRawSRVIndex = vbSrv.index;
        buffers.ibRawSRVIndex = ibSrv.index;
        buffers.vertexStrideBytes = static_cast<uint32_t>(sizeof(Vertex));
        buffers.indexFormat = 0u; // R32_UINT
        return Result<void>::Ok();
    }

    [[nodiscard]] static Result<MeshUploadResourceBundle, MeshUploadResourceError> CreateResources(
        ID3D12Device* device,
        DescriptorHeapManager* descriptorManager,
        const std::vector<Vertex>& vertices,
        const std::vector<uint32_t>& indices) {
        if (!device) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Renderer is not initialized",
                E_POINTER,
                nullptr,
            });
        }

        const UINT64 vertexBytes = static_cast<UINT64>(vertices.size()) * static_cast<UINT64>(sizeof(Vertex));
        const UINT64 indexBytes = static_cast<UINT64>(indices.size()) * static_cast<UINT64>(sizeof(uint32_t));
        if (vertexBytes == 0 || indexBytes == 0) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Mesh has no vertices or indices",
                E_INVALIDARG,
                nullptr,
            });
        }

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        defaultHeap.CreationNodeMask = 1;
        defaultHeap.VisibleNodeMask = 1;

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        uploadHeap.CreationNodeMask = 1;
        uploadHeap.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vbDesc{};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = vertexBytes;
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.Format = DXGI_FORMAT_UNKNOWN;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_RESOURCE_DESC ibDesc = vbDesc;
        ibDesc.Width = indexBytes;

        MeshUploadResourceBundle bundle{};
        bundle.gpuBuffers = std::make_shared<MeshBuffers>();
        bundle.vertexBytes = vertexBytes;
        bundle.indexBytes = indexBytes;

        HRESULT hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&bundle.gpuBuffers->vertexBuffer));
        if (FAILED(hr)) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to create default-heap vertex buffer",
                hr,
                "UploadMesh_CreateVertexBuffer",
            });
        }

        hr = device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&bundle.gpuBuffers->indexBuffer));
        if (FAILED(hr)) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to create default-heap index buffer",
                hr,
                "UploadMesh_CreateIndexBuffer",
            });
        }

        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&bundle.vertexUpload));
        if (FAILED(hr)) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to create staging vertex upload buffer",
                hr,
                "UploadMesh_CreateVertexUploadBuffer",
            });
        }

        hr = device->CreateCommittedResource(
            &uploadHeap,
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&bundle.indexUpload));
        if (FAILED(hr)) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to create staging index upload buffer",
                hr,
                "UploadMesh_CreateIndexUploadBuffer",
            });
        }

        D3D12_RANGE readRange{0, 0};
        void* mappedData = nullptr;
        hr = bundle.vertexUpload->Map(0, &readRange, &mappedData);
        if (FAILED(hr) || !mappedData) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to map staging vertex buffer",
                hr,
                nullptr,
            });
        }
        std::memcpy(mappedData, vertices.data(), static_cast<size_t>(vertexBytes));
        bundle.vertexUpload->Unmap(0, nullptr);

        mappedData = nullptr;
        hr = bundle.indexUpload->Map(0, &readRange, &mappedData);
        if (FAILED(hr) || !mappedData) {
            return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Err({
                "Failed to map staging index buffer",
                hr,
                nullptr,
            });
        }
        std::memcpy(mappedData, indices.data(), static_cast<size_t>(indexBytes));
        bundle.indexUpload->Unmap(0, nullptr);

        if (descriptorManager) {
            auto rawSrvResult = EnsureRawSRVs(device, descriptorManager, *bundle.gpuBuffers);
            if (rawSrvResult.IsErr()) {
                bundle.descriptorWarning = "UploadMesh: " + rawSrvResult.Error();
            }
        }

        return Result<MeshUploadResourceBundle, MeshUploadResourceError>::Ok(std::move(bundle));
    }
};

} // namespace Cortex::Graphics
