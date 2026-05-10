#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstdio>

namespace Cortex::Graphics {
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
    (void)copyQueue;

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
    (void)copyQueue;

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

} // namespace Cortex::Graphics
