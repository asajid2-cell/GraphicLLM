#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <glm/glm.hpp>
#include "Graphics/Renderer_ConstantBuffer.h"

namespace Cortex::Graphics {

struct DebugLineVertex {
    glm::vec3 position;
    glm::vec4 color;
};

struct DebugLineRenderState {
    std::vector<DebugLineVertex> lines;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    uint32_t vertexCapacity = 0;
    bool disabled = false;

    [[nodiscard]] bool NeedsVertexCapacity(uint32_t requiredCapacity) const {
        return !vertexBuffer || vertexCapacity < requiredCapacity;
    }

    [[nodiscard]] HRESULT EnsureVertexBuffer(ID3D12Device* device,
                                             uint32_t requiredCapacity,
                                             uint32_t minCapacity) {
        if (!device || requiredCapacity == 0) {
            return E_INVALIDARG;
        }
        if (!NeedsVertexCapacity(requiredCapacity)) {
            return S_OK;
        }

        const uint32_t newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(DebugLineVertex);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            return hr;
        }

        vertexBuffer = buffer;
        vertexCapacity = newCapacity;
        return S_OK;
    }

    [[nodiscard]] HRESULT UploadVertices(const DebugLineVertex* vertices,
                                         uint32_t vertexCount,
                                         UINT& bytesWritten) {
        bytesWritten = 0;
        if (!vertexBuffer || !vertices || vertexCount == 0) {
            return E_INVALIDARG;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        bytesWritten = vertexCount * sizeof(DebugLineVertex);
        const HRESULT hr = vertexBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr)) {
            bytesWritten = 0;
            return hr;
        }

        std::memcpy(mapped, vertices, bytesWritten);
        vertexBuffer->Unmap(0, nullptr);
        return S_OK;
    }

    [[nodiscard]] D3D12_VERTEX_BUFFER_VIEW VertexBufferView(uint32_t vertexCount,
                                                           UINT vertexBytes) const {
        D3D12_VERTEX_BUFFER_VIEW view{};
        if (!vertexBuffer || vertexCount == 0 || vertexBytes == 0) {
            return view;
        }
        view.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        view.StrideInBytes = sizeof(DebugLineVertex);
        view.SizeInBytes = vertexBytes;
        return view;
    }

    void ResetFrame() {
        lines.clear();
    }

    void ResetResources() {
        lines.clear();
        vertexBuffer.Reset();
        vertexCapacity = 0;
        disabled = false;
    }
};

struct RendererDebugViewState {
    uint32_t mode = 0;
};

} // namespace Cortex::Graphics
