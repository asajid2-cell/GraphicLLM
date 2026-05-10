#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstdio>

namespace Cortex::Graphics {
Result<void> DX12Texture::UpdateData(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    const uint8_t* data,
    uint32_t width,
    uint32_t height,
    uint32_t offsetX,
    uint32_t offsetY)
{
    // This is the KEY function for hot-swapping textures from AI generation
    // Similar to Metal's replaceRegion

    if (width + offsetX > m_width || height + offsetY > m_height) {
        return Result<void>::Err("Update region out of bounds");
    }
    if (offsetX != 0 || offsetY != 0 || width != m_width || height != m_height) {
        return Result<void>::Err("Partial texture updates are not implemented yet");
    }

    const uint32_t bytesPerPixel = 4; // RGBA8
    const uint64_t dataSize64 = static_cast<uint64_t>(width) * height * bytesPerPixel;
    if (dataSize64 > 0xffffffffull) {
        return Result<void>::Err("Texture update data is too large");
    }
    const uint32_t dataSize = static_cast<uint32_t>(dataSize64);

    // For now, we'll update the entire texture
    // A more sophisticated implementation would handle partial updates
    return UploadTextureData(device, commandQueue, data, dataSize);
}

Result<void> DX12Texture::UploadTextureData(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    const uint8_t* data,
    uint32_t dataSize)
{
    if (!device || !commandQueue || !data) {
        return Result<void>::Err("Invalid texture upload inputs");
    }
    const uint64_t expectedSize = static_cast<uint64_t>(m_width) * m_height * 4u;
    if (dataSize < expectedSize) {
        return Result<void>::Err("Texture upload data is smaller than the target texture");
    }

    // Create upload buffer (CPU-accessible staging buffer)
    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    // Get required size for upload buffer
    UINT64 uploadBufferSize;
    D3D12_RESOURCE_DESC textureDesc = m_resource->GetDesc();
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> uploadBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create upload buffer");
    }

    // Copy data to upload buffer
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = m_width * 4; // 4 bytes per pixel (RGBA8)
    subresourceData.SlicePitch = subresourceData.RowPitch * m_height;

    // Map and copy
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 }; // Not reading from CPU
    hr = uploadBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map upload buffer");
    }

    // Calculate layout
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    UINT64 rowSizeInBytes;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, nullptr);

    // Copy row by row
    uint8_t* destData = reinterpret_cast<uint8_t*>(mappedData);
    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(subresourceData.pData);
    for (UINT i = 0; i < numRows; ++i) {
        memcpy(
            destData + layout.Footprint.RowPitch * i,
            srcData + subresourceData.RowPitch * i,
            subresourceData.RowPitch
        );
    }

    uploadBuffer->Unmap(0, nullptr);

    // Create command list for copy operation
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command allocator");
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command list");
    }

    // Transition texture to copy dest state if needed
    if (m_currentState != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_resource.Get();
        barrier.Transition.StateBefore = m_currentState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(1, &barrier);
        m_currentState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    // Copy from upload buffer to texture
    D3D12_TEXTURE_COPY_LOCATION destLocation = {};
    destLocation.pResource = m_resource.Get();
    destLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = layout;

    commandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition to shader resource state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    commandList->Close();

    // Execute and wait
    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);

    // Ensure the GPU has finished using the upload buffer before we return and let it go out of scope.
    // This prevents use-after-free on the GPU when hot-swapping textures.
    ComPtr<ID3D12Fence> fence;
    HRESULT hrFence = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to create fence for texture upload");
    }

    const uint64_t fenceValue = 1;
    hrFence = commandQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to signal fence for texture upload");
    }

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!evt) {
        return Result<void>::Err("Failed to create upload completion event");
    }

    fence->SetEventOnCompletion(fenceValue, evt);
    WaitForSingleObject(evt, INFINITE);
    CloseHandle(evt);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
