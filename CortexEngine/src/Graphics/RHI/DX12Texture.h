#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint> // For uint8_t
#include <string>
#include "DescriptorHeap.h"
#include "Utils/Result.h"

using Microsoft::WRL::ComPtr;

namespace Cortex::Graphics {

class DX12Device;
class DX12CommandQueue;

// Texture description
struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    bool allowCudaInterop = false;  // For Phase 3: AI texture generation
    // Optional array size / cubemap support (arraySize=6 => cube)
    uint32_t arraySize = 1;
};

// DX12 Texture wrapper - supports hot-swapping via replaceRegion-style updates
class DX12Texture {
public:
    DX12Texture() = default;
    ~DX12Texture() = default;

    DX12Texture(const DX12Texture&) = delete;
    DX12Texture& operator=(const DX12Texture&) = delete;
    DX12Texture(DX12Texture&&) = default;
    DX12Texture& operator=(DX12Texture&&) = default;

    // Create texture from description
    Result<void> Initialize(
        ID3D12Device* device,
        const TextureDesc& desc,
        const std::string& debugName = ""
    );

    // Create texture from raw pixel data (RGBA8)
    Result<void> InitializeFromData(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandQueue* graphicsQueue,
        const uint8_t* data,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
        const std::string& debugName = ""
    );
    Result<void> InitializeFromMipChain(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandQueue* graphicsQueue,
        const std::vector<std::vector<uint8_t>>& mipData,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format,
        const std::string& debugName = ""
    );

    // Create texture from a pre-compressed BCn mip chain (BC1/BC3/BC5/BC7).
    // The mip data is expected to be tightly packed BC blocks with no
    // per-row padding; row strides are derived from GetCopyableFootprints.
    Result<void> InitializeFromCompressedMipChain(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandQueue* graphicsQueue,
        const std::vector<std::vector<uint8_t>>& mipData,
        uint32_t width,
        uint32_t height,
        DXGI_FORMAT format,
        const std::string& debugName = ""
    );

    // Create a "placeholder" texture (solid color)
    static Result<DX12Texture> CreatePlaceholder(
        ID3D12Device* device,
        ID3D12CommandQueue* copyQueue,
        ID3D12CommandQueue* graphicsQueue,
        uint32_t width,
        uint32_t height,
        const float color[4] = nullptr  // nullptr = white
    );

    // Hot-swap texture data (Phase 3: for AI-generated textures)
    // This is the CRITICAL function for real-time texture generation
    Result<void> UpdateData(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        const uint8_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t offsetX = 0,
        uint32_t offsetY = 0
    );

    // Create Shader Resource View (SRV) for binding to shaders
    Result<void> CreateSRV(ID3D12Device* device, DescriptorHandle handle);

    // Cubemap initialization from 6 RGBA8 faces (order: +X,-X,+Y,-Y,+Z,-Z)
    Result<void> InitializeCubeFromFaces(
        ID3D12Device* device,
        ID3D12CommandQueue* graphicsQueue,
        const std::vector<std::vector<uint8_t>>& faceData,
        uint32_t faceSize,
        DXGI_FORMAT format,
        const std::string& debugName = ""
    );

    // Accessors
    [[nodiscard]] ID3D12Resource* GetResource() const { return m_resource.Get(); }
    [[nodiscard]] uint32_t GetWidth() const { return m_width; }
    [[nodiscard]] uint32_t GetHeight() const { return m_height; }
    [[nodiscard]] uint32_t GetMipLevels() const { return m_mipLevels; }
    [[nodiscard]] DXGI_FORMAT GetFormat() const { return m_format; }
    [[nodiscard]] D3D12_RESOURCE_STATES GetCurrentState() const { return m_currentState; }
    [[nodiscard]] const DescriptorHandle& GetSRV() const { return m_srvHandle; }

    // State tracking (for resource barriers)
    void SetState(D3D12_RESOURCE_STATES state) { m_currentState = state; }

private:
    ComPtr<ID3D12Resource> m_resource;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_mipLevels = 1;
    DXGI_FORMAT m_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON;

    DescriptorHandle m_srvHandle;  // Shader Resource View handle
    bool m_isCubeMap = false;

    // Helper for uploading data via upload buffer
    Result<void> UploadTextureData(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        const uint8_t* data,
        uint32_t dataSize
    );
};

} // namespace Cortex::Graphics
