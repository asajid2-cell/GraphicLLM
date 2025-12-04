    // CRITICAL FIX: Cannot copy from shader-visible heap (CPU write-only).
    // Instead, CREATE SRVs directly in transient descriptors by calling CreateShaderResourceView.
    // The root signature expects 10 consecutive SRV descriptors (t0-t9).

    if (!m_hdrColor) {
        spdlog::error("RenderPostProcess: HDR color buffer is invalid");
        return;
    }

    // Allocate a contiguous block of 10 transient descriptors
    std::array<DescriptorHandle, 10> srvTable;
    for (int i = 0; i < 10; ++i) {
        auto allocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (allocResult.IsErr()) {
            spdlog::error("RenderPostProcess: failed to allocate transient SRV slot {}: {}", i, allocResult.Error());
            return;
        }
        srvTable[i] = allocResult.Value();
    }

    ID3D12Device* device = m_device->GetDevice();

    // Create SRVs directly in the transient descriptors (NOT copying!)

    // t0: HDR color (required)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_hdrColor.Get(), &srvDesc, srvTable[0].cpu);
    }

    // t1: Bloom (optional)
    if (m_bloomTex[0][0]) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_bloomTex[0][0].Get(), &srvDesc, srvTable[1].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[1].cpu);
    }

    // t2: SSAO (optional)
    if (m_ssaoTex) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_ssaoTex.Get(), &srvDesc, srvTable[2].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[2].cpu);
        m_frameDataCPU.aoParams.x = 0.0f;
        m_frameConstantBuffer.UpdateData(m_frameDataCPU);
    }

    // t3: TAA history (optional)
    if (m_taaEnabled && m_hasHistory && m_taaHistory) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_taaHistory.Get(), &srvDesc, srvTable[3].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[3].cpu);
        m_frameDataCPU.taaParams.w = 0.0f;
        m_frameConstantBuffer.UpdateData(m_frameDataCPU);
    }

    // t4: Depth (optional)
    if (m_depthBuffer) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_depthBuffer.Get(), &srvDesc, srvTable[4].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[4].cpu);
    }

    // t5: Normal/Roughness G-buffer (optional)
    if (m_gbufferNormalRoughness) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_gbufferNormalRoughness.Get(), &srvDesc, srvTable[5].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderNormal->GetResource(), nullptr, srvTable[5].cpu);
    }

    // t6: SSR color (optional)
    if (m_ssrColor) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_ssrColor.Get(), &srvDesc, srvTable[6].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[6].cpu);
    }

    // t7: Velocity (optional)
    if (m_velocityBuffer) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_velocityBuffer.Get(), &srvDesc, srvTable[7].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[7].cpu);
    }

    // t8: RT reflection color (optional)
    if (m_rtReflectionColor) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_rtReflectionColor.Get(), &srvDesc, srvTable[8].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[8].cpu);
    }

    // t9: RT reflection history (optional)
    if (m_rtReflectionHistory) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_rtReflectionHistory.Get(), &srvDesc, srvTable[9].cpu);
    } else {
        device->CreateShaderResourceView(m_placeholderAlbedo->GetResource(), nullptr, srvTable[9].cpu);
    }

    // Bind the complete SRV table (all 10 descriptors starting at t0)
    m_commandList->SetGraphicsRootDescriptorTable(3, srvTable[0].gpu);
