#pragma once

#include <cstdint>

#include <wrl/client.h>

#include "Graphics/RHI/D3D12Includes.h"
#include "Utils/Result.h"

namespace Cortex::Graphics {

enum class GpuMarker : uint32_t {
    None              = 0,
    BeginFrame        = 1,
    ShadowPass        = 2,
    Skybox            = 3,
    OpaqueGeometry    = 4,
    TransparentGeom   = 5,
    MotionVectors     = 6,
    TAAResolve        = 7,
    SSR               = 8,
    Particles         = 9,
    SSAO              = 10,
    Bloom             = 11,
    PostProcess       = 12,
    DebugLines        = 13,
    EndFrame          = 14,
};

struct RendererBreadcrumbState {
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    uint32_t* mappedValue = nullptr;

    [[nodiscard]] Result<void> CreateBuffer(ID3D12Device* device) {
        if (!device) {
            return Result<void>::Err("Renderer not initialized for breadcrumb buffer creation");
        }
        if (buffer) {
            return Result<void>::Ok();
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(uint32_t);
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
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create GPU breadcrumb buffer");
        }

        const HRESULT mapHr = buffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedValue));
        if (FAILED(mapHr)) {
            buffer.Reset();
            mappedValue = nullptr;
            return Result<void>::Err("Failed to map GPU breadcrumb buffer");
        }

        if (mappedValue) {
            *mappedValue = static_cast<uint32_t>(GpuMarker::None);
        }

        return Result<void>::Ok();
    }

    void Write(ID3D12GraphicsCommandList* commandList, GpuMarker marker) {
        if (!buffer || !commandList) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
        if (FAILED(commandList->QueryInterface(IID_PPV_ARGS(&list4))) || !list4) {
            return;
        }

        D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{};
        param.Dest = buffer->GetGPUVirtualAddress();
        param.Value = static_cast<uint32_t>(marker);

        list4->WriteBufferImmediate(1, &param, nullptr);
    }
};

} // namespace Cortex::Graphics
