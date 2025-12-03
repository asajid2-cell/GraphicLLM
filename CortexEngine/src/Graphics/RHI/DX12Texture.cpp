#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstdio>

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
    m_isCubeMap = (desc.arraySize == 6);

    // Create resource description
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.arraySize;
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
        char hrBuf[64];
        sprintf_s(hrBuf, "0x%08X", static_cast<unsigned int>(hr));
        HRESULT removed = device->GetDeviceRemovedReason();
        char remBuf[64];
        sprintf_s(remBuf, "0x%08X", static_cast<unsigned int>(removed));
        return Result<void>::Err(std::string("Failed to create texture resource (hr=") + hrBuf + ", removed=" + remBuf + ")");
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
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandQueue* graphicsQueue,
    const uint8_t* data,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const std::string& debugName)
{
    std::vector<std::vector<uint8_t>> mips;
    mips.emplace_back(data, data + static_cast<size_t>(width) * height * 4);
    return InitializeFromMipChain(device, copyQueue, graphicsQueue, mips, width, height, format, debugName);
}

Result<void> DX12Texture::InitializeFromMipChain(
    ID3D12Device* device,
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandQueue* graphicsQueue,
    const std::vector<std::vector<uint8_t>>& mipData,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const std::string& debugName)
{
    if (mipData.empty()) {
        return Result<void>::Err("Mip chain is empty");
    }

    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    desc.mipLevels = static_cast<uint32_t>(mipData.size());

    auto initResult = Initialize(device, desc, debugName);
    if (initResult.IsErr()) {
        return initResult;
    }

    D3D12_RESOURCE_DESC textureDesc = m_resource->GetDesc();
    UINT numSubresources = desc.mipLevels;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizes(numSubresources);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, numSubresources, 0, layouts.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadSize;
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
        return Result<void>::Err("Failed to create upload buffer for mips");
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE range{0, 0};
    hr = uploadBuffer->Map(0, &range, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map upload buffer for mips");
    }

    for (UINT i = 0; i < numSubresources; ++i) {
        const auto& mip = mipData[i];
        const auto& layout = layouts[i];
        uint8_t* dst = mapped + layout.Offset;
        const uint8_t* src = mip.data();

        // Footprint width is the texel width of this mip level. Since all of
        // our uploaded images are tightly packed RGBA8, the source row pitch
        // is simply width * 4 bytes. Using this instead of the aggregated
        // rowSizes[i] avoids subtle mismatches that can smear the image if
        // the layout ever includes padding.
        UINT64 rowPitchDst = layout.Footprint.RowPitch;
        UINT64 rowPitchSrc = static_cast<UINT64>(layout.Footprint.Width) * 4u;

        for (UINT row = 0; row < numRows[i]; ++row) {
            memcpy(dst + rowPitchDst * row, src + rowPitchSrc * row, rowPitchSrc);
        }
    }

    uploadBuffer->Unmap(0, nullptr);

    // Record copy + transition on a single DIRECT command list executed on the
    // graphics queue. This keeps texture upload logic simple and avoids extra
    // per-texture fences that can interact poorly with TDR on some drivers.
    if (!graphicsQueue) {
        return Result<void>::Err("Graphics queue is null for texture upload");
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command allocator for mip upload");
    }

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command list for mip upload");
    }

    for (UINT i = 0; i < numSubresources; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = i;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layouts[i];

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    cmdList->Close();
    ID3D12CommandList* cmdLists[] = { cmdList.Get() };
    graphicsQueue->ExecuteCommandLists(1, cmdLists);

    // CRITICAL: Wait for GPU to finish before upload buffer goes out of scope
    // Without this fence, the upload buffer is destroyed while GPU is still reading from it
    ComPtr<ID3D12Fence> fence;
    HRESULT hrFence = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to create fence for mip upload");
    }

    const uint64_t fenceValue = 1;
    hrFence = graphicsQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to signal fence for mip upload");
    }

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!evt) {
        return Result<void>::Err("Failed to create upload completion event for mips");
    }

    fence->SetEventOnCompletion(fenceValue, evt);
    WaitForSingleObject(evt, INFINITE);
    CloseHandle(evt);

    return Result<void>::Ok();
}

Result<void> DX12Texture::InitializeFromCompressedMipChain(
    ID3D12Device* device,
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandQueue* graphicsQueue,
    const std::vector<std::vector<uint8_t>>& mipData,
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format,
    const std::string& debugName)
{
    if (mipData.empty()) {
        return Result<void>::Err("Compressed mip chain is empty");
    }

    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    desc.mipLevels = static_cast<uint32_t>(mipData.size());

    auto initResult = Initialize(device, desc, debugName);
    if (initResult.IsErr()) {
        return initResult;
    }

    D3D12_RESOURCE_DESC textureDesc = m_resource->GetDesc();
    UINT numSubresources = desc.mipLevels;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizes(numSubresources);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, numSubresources, 0, layouts.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadBufferDesc = {};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.SampleDesc.Quality = 0;
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
        return Result<void>::Err("Failed to create upload buffer for compressed mips");
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE range{0, 0};
    hr = uploadBuffer->Map(0, &range, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map upload buffer for compressed mips");
    }

    for (UINT i = 0; i < numSubresources; ++i) {
        const auto& mip = mipData[i];
        const auto& layout = layouts[i];
        uint8_t* dst = mapped + layout.Offset;
        const uint8_t* src = mip.data();

        const UINT64 rowPitchDst = layout.Footprint.RowPitch;
        const UINT64 rowPitchSrc = rowSizes[i];

        if (mip.size() < rowPitchSrc * numRows[i]) {
            uploadBuffer->Unmap(0, nullptr);
            return Result<void>::Err("Compressed mip data is smaller than expected footprint");
        }

        for (UINT row = 0; row < numRows[i]; ++row) {
            memcpy(dst + rowPitchDst * row, src + rowPitchSrc * row, rowPitchSrc);
        }
    }

    uploadBuffer->Unmap(0, nullptr);

    // Record copy + transition on a single DIRECT command list executed on the
    // graphics queue. As with the uncompressed path, this avoids per-texture
    // fences and keeps behavior predictable on TDR-prone drivers.
    if (!graphicsQueue) {
        return Result<void>::Err("Graphics queue is null for compressed texture upload");
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command allocator for compressed mip upload");
    }

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command list for compressed mip upload");
    }

    for (UINT i = 0; i < numSubresources; ++i) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = i;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = uploadBuffer.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = layouts[i];

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList.Get() };
    graphicsQueue->ExecuteCommandLists(1, lists);

    // CRITICAL: Wait for GPU to finish before upload buffer goes out of scope
    // Without this fence, the upload buffer is destroyed while GPU is still reading from it
    ComPtr<ID3D12Fence> fence;
    HRESULT hrFence = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to create fence for compressed mip upload");
    }

    const uint64_t fenceValue = 1;
    hrFence = graphicsQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to signal fence for compressed mip upload");
    }

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!evt) {
        return Result<void>::Err("Failed to create upload completion event for compressed mips");
    }

    fence->SetEventOnCompletion(fenceValue, evt);
    WaitForSingleObject(evt, INFINITE);
    CloseHandle(evt);

    return Result<void>::Ok();
}

Result<DX12Texture> DX12Texture::CreatePlaceholder(
    ID3D12Device* device,
    ID3D12CommandQueue* copyQueue,
    ID3D12CommandQueue* graphicsQueue,
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
        copyQueue,
        graphicsQueue,
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
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (m_isCubeMap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = m_mipLevels;
        srvDesc.TextureCube.MostDetailedMip = 0;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = m_mipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
    }

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

Result<void> DX12Texture::InitializeCubeFromFaces(
    ID3D12Device* device,
    ID3D12CommandQueue* graphicsQueue,
    const std::vector<std::vector<uint8_t>>& faceData,
    uint32_t faceSize,
    DXGI_FORMAT format,
    const std::string& debugName)
{
    if (!device || !graphicsQueue) {
        return Result<void>::Err("InitializeCubeFromFaces: invalid device or graphics queue");
    }
    if (faceData.size() != 6) {
        return Result<void>::Err("InitializeCubeFromFaces: expected 6 faces");
    }

    // Create cube texture resource (array of 6 2D slices)
    m_width = faceSize;
    m_height = faceSize;
    m_mipLevels = 1;
    m_format = format;
    m_currentState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_isCubeMap = true;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = faceSize;
    texDesc.Height = faceSize;
    texDesc.DepthOrArraySize = 6; // 6 faces
    texDesc.MipLevels = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        m_currentState,
        nullptr,
        IID_PPV_ARGS(&m_resource)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create cubemap texture resource");
    }

    if (!debugName.empty()) {
        std::wstring wname(debugName.begin(), debugName.end());
        m_resource->SetName(wname.c_str());
    }

    // Calculate footprints for 6 subresources (one per face)
    UINT numSubresources = 6;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizes(numSubresources);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, numSubresources, 0, layouts.data(), numRows.data(), rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeapProps.CreationNodeMask = 1;
    uploadHeapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> uploadBuffer;
    hr = device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create cubemap upload buffer");
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE range{0, 0};
    hr = uploadBuffer->Map(0, &range, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map cubemap upload buffer");
    }

    for (UINT face = 0; face < numSubresources; ++face) {
        const auto& srcFace = faceData[face];
        const auto& layout = layouts[face];
        uint8_t* dst = mapped + layout.Offset;
        const uint8_t* src = srcFace.data();
        UINT64 rowPitch = layout.Footprint.RowPitch;
        UINT64 srcPitch = static_cast<UINT64>(faceSize) * 4; // RGBA8

        for (UINT row = 0; row < numRows[face]; ++row) {
            memcpy(dst + rowPitch * row, src + srcPitch * row, srcPitch);
        }
    }

    uploadBuffer->Unmap(0, nullptr);

    // Create command list on graphics queue (direct)
    ID3D12CommandQueue* queue = graphicsQueue;

    ComPtr<ID3D12CommandAllocator> allocator;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create cubemap command allocator");
    }

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmdList));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create cubemap command list");
    }

    // Copy each face
    for (UINT face = 0; face < numSubresources; ++face) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = face; // mip 0, array slice = face

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = layouts[face];

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    // Transition to shader resource
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    m_currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    cmdList->Close();

    ID3D12CommandList* lists[] = { cmdList.Get() };
    queue->ExecuteCommandLists(1, lists);

    // Ensure the GPU has finished executing the copy and transition before
    // we return. This keeps the device in a well-defined state so that
    // later resource creation (e.g., vertex buffers for scene geometry)
    // does not see a removed/hung device if something went wrong in the
    // upload path.
    ComPtr<ID3D12Fence> fence;
    HRESULT hrFence = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to create fence for cubemap upload");
    }

    const uint64_t fenceValue = 1;
    hrFence = queue->Signal(fence.Get(), fenceValue);
    if (FAILED(hrFence)) {
        return Result<void>::Err("Failed to signal fence for cubemap upload");
    }

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!evt) {
        return Result<void>::Err("Failed to create upload completion event for cubemap");
    }

    fence->SetEventOnCompletion(fenceValue, evt);
    WaitForSingleObject(evt, INFINITE);
    CloseHandle(evt);

    spdlog::info("Cubemap texture created: {}x{} ({})", m_width, m_height, debugName);
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
