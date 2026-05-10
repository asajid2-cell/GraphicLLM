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
    if (desc.width == 0 || desc.height == 0 || desc.mipLevels == 0 || desc.arraySize == 0) {
        return Result<void>::Err("Texture dimensions, mip levels, and array size must be non-zero");
    }
    if (desc.mipLevels > 0xffffu || desc.arraySize > 0xffffu) {
        return Result<void>::Err("Texture mip levels or array size exceed D3D12 limits");
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
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arraySize);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
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

} // namespace Cortex::Graphics
