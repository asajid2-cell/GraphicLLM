#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <wrl/client.h>

#include "RHI/D3D12Includes.h"
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

// Number of frames in flight (triple buffering)
static constexpr uint32_t kFrameCount = 3;

// Constant buffer wrapper with triple-buffering support
template<typename T>
struct ConstantBuffer {
    ComPtr<ID3D12Resource> buffer;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    uint8_t* mappedBytes = nullptr;
    size_t bufferSize = 0;
    size_t alignedSize = 0;
    size_t offset = 0;
    // Triple-buffering: each frame index gets its own region to avoid overwriting
    // data that the GPU is still reading from previous frames
    size_t perFrameSize = 0;
    size_t frameRegionStart = 0;
    size_t frameRegionEnd = 0;

    static constexpr size_t Align256(size_t value) {
        return (value + 255) & ~static_cast<size_t>(255);
    }

    Result<void> Initialize(ID3D12Device* device, size_t elementCount = 1) {
        // Create upload heap buffer sized for the requested element count
        // Multiply by kFrameCount for triple-buffering so each frame has its own region
        alignedSize = Align256(sizeof(T));
        perFrameSize = Align256(alignedSize * elementCount);
        bufferSize = Align256(perFrameSize * kFrameCount);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)
        );

        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create constant buffer");
        }

        gpuAddress = buffer->GetGPUVirtualAddress();

        // Map persistently (upload heap allows this)
        D3D12_RANGE readRange = { 0, 0 };
        hr = buffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedBytes));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to map constant buffer");
        }

        offset = 0;
        frameRegionStart = 0;
        frameRegionEnd = perFrameSize;
        return Result<void>::Ok();
    }

    // Reset offset to the start of the given frame's region (for triple-buffering)
    void ResetOffset(uint32_t frameIndex = 0) {
        frameRegionStart = (frameIndex % kFrameCount) * perFrameSize;
        frameRegionEnd = frameRegionStart + perFrameSize;
        offset = frameRegionStart;
    }

    // Write data into the next slice of the current frame's region
    // Each frame has its own isolated region, preventing overwrites while GPU reads
    D3D12_GPU_VIRTUAL_ADDRESS AllocateAndWrite(const T& data) {
        if (!mappedBytes || alignedSize == 0) {
            return gpuAddress;
        }
        if (offset + alignedSize > frameRegionEnd) {
            // Wrap within current frame's region if we run out of space
            offset = frameRegionStart;
        }
        memcpy(mappedBytes + offset, &data, sizeof(T));
        D3D12_GPU_VIRTUAL_ADDRESS addr = gpuAddress + offset;
        offset += alignedSize;
        return addr;
    }

    // Write data to a specific slot (frame-indexed) - use this for frame constants
    // The slotIndex should be m_frameIndex to ensure proper synchronization with GPU
    D3D12_GPU_VIRTUAL_ADDRESS WriteToSlot(const T& data, uint32_t slotIndex) {
        if (!mappedBytes || alignedSize == 0) {
            return gpuAddress;
        }
        size_t slotOffset = (slotIndex % (bufferSize / alignedSize)) * alignedSize;
        memcpy(mappedBytes + slotOffset, &data, sizeof(T));
        return gpuAddress + slotOffset;
    }

    // Convenience for single-slot buffers (frame constants)
    void UpdateData(const T& data) {
        if (mappedBytes) {
            memcpy(mappedBytes, &data, sizeof(T));
        }
    }

    ~ConstantBuffer() {
        if (buffer && mappedBytes) {
            buffer->Unmap(0, nullptr);
            mappedBytes = nullptr;
        }
    }
};

} // namespace Cortex::Graphics