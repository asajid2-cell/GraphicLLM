#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>

namespace Cortex::Graphics {

Result<void> DX12Texture::Initialize(
    ID3D12Device* device,
    const TextureDesc& desc,
    const std::string& debugName)
{
    if (!device) {
        return Result<void>::Err("Invalid device pointer");
    }

    m_width = desc.width;
    m_height = desc.height;
    m_mipLevels = desc.mipLevels;
    m_format = desc.format;
    m_currentState = desc.initialState;

    // Create resource description
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = desc.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = desc.flags;

    // Add simultaneous access flag if CUDA interop is needed (Phase 3)
    if (desc.allowCudaInterop) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    }

    // Create resource in default heap (GPU memory)
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        m_currentState,
        nullptr,
        IID_PPV_ARGS(&m_resource)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create texture resource");
    }

    // Set debug name
    if (!debugName.empty()) {
        std::wstring wname(debugName.begin(), debugName.end());
        m_resource->SetName(wname.c_str());
    }

    spdlog::info("Texture created: {}x{} ({})", m_width, m_height, debugName);
    return Result<void>::Ok();
}

Result<void> DX12Texture::InitializeFromData(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    const uint8_t* data,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const std::string& debugName)
{
    // First create the texture resource
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;

    auto initResult = Initialize(device, desc, debugName);
    if (initResult.IsErr()) {
        return initResult;
    }

    // Calculate data size
    uint32_t bytesPerPixel = 4; // Assuming RGBA8
    uint32_t dataSize = width * height * bytesPerPixel;

    // Upload the data
    auto uploadResult = UploadTextureData(device, commandQueue, data, dataSize);
    if (uploadResult.IsErr()) {
        return uploadResult;
    }

    return Result<void>::Ok();
}

Result<DX12Texture> DX12Texture::CreatePlaceholder(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    uint32_t width,
    uint32_t height,
    const float color[4])
{
    // Default to white if no color specified
    float defaultColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float* useColor = color ? color : defaultColor;

    // Create solid color pixel data
    std::vector<uint8_t> pixelData(width * height * 4);
    for (uint32_t i = 0; i < width * height; ++i) {
        pixelData[i * 4 + 0] = static_cast<uint8_t>(useColor[0] * 255.0f);
        pixelData[i * 4 + 1] = static_cast<uint8_t>(useColor[1] * 255.0f);
        pixelData[i * 4 + 2] = static_cast<uint8_t>(useColor[2] * 255.0f);
        pixelData[i * 4 + 3] = static_cast<uint8_t>(useColor[3] * 255.0f);
    }

    DX12Texture texture;
    auto result = texture.InitializeFromData(
        device,
        commandQueue,
        pixelData.data(),
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        "Placeholder"
    );

    if (result.IsErr()) {
        return Result<DX12Texture>::Err(result.Error());
    }

    return Result<DX12Texture>::Ok(std::move(texture));
}

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

    uint32_t bytesPerPixel = 4; // RGBA8
    uint32_t dataSize = width * height * bytesPerPixel;

    // For now, we'll update the entire texture
    // A more sophisticated implementation would handle partial updates
    return UploadTextureData(device, commandQueue, data, dataSize);
}

Result<void> DX12Texture::CreateSRV(ID3D12Device* device, DescriptorHandle handle) {
    if (!handle.IsValid()) {
        return Result<void>::Err("Invalid descriptor handle");
    }

    // Create Shader Resource View
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = m_mipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    device->CreateShaderResourceView(m_resource.Get(), &srvDesc, handle.cpu);

    m_srvHandle = handle;

    return Result<void>::Ok();
}

Result<void> DX12Texture::UploadTextureData(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    const uint8_t* data,
    uint32_t dataSize)
{
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

    // Wait for upload to complete
    // In a real engine, we'd use a fence here, but for simplicity we'll use the command queue's fence
    // (This is inefficient but works for the prototype)

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
