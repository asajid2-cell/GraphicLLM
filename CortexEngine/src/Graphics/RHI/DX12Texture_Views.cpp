#include "DX12Texture.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <cstdio>

namespace Cortex::Graphics {
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

Result<void> DX12Texture::CreateBindlessSRV(BindlessResourceManager* bindlessManager) {
    if (!bindlessManager) {
        return Result<void>::Err("BindlessResourceManager is null");
    }

    if (!m_resource) {
        return Result<void>::Err("Texture resource not initialized");
    }

    // Build SRV description
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

    // Allocate in bindless heap
    auto indexResult = bindlessManager->AllocateTextureIndex(m_resource.Get(), &srvDesc);
    if (indexResult.IsErr()) {
        return Result<void>::Err(indexResult.Error());
    }

    m_bindlessIndex = indexResult.Value();
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
