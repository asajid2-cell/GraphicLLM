#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstdio>

namespace Cortex::Graphics {
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
