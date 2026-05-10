#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void VisibilityBufferRenderer::Shutdown() {
    // Unmap and release all triple-buffered instance buffers
    for (uint32_t i = 0; i < kVBFrameCount; ++i) {
        if (m_instanceBuffer[i] && m_instanceBufferMapped[i]) {
            m_instanceBuffer[i]->Unmap(0, nullptr);
            m_instanceBufferMapped[i] = nullptr;
        }
        m_instanceBuffer[i].Reset();
    }
    // Unmap and release all triple-buffered material buffers
    for (uint32_t i = 0; i < kVBFrameCount; ++i) {
        if (m_materialBuffer[i] && m_materialBufferMapped[i]) {
            m_materialBuffer[i]->Unmap(0, nullptr);
            m_materialBufferMapped[i] = nullptr;
        }
        m_materialBuffer[i].Reset();
    }
    // Unmap and release all triple-buffered mesh table buffers
    for (uint32_t i = 0; i < kVBFrameCount; ++i) {
        if (m_meshTableBuffer[i] && m_meshTableBufferMapped[i]) {
            m_meshTableBuffer[i]->Unmap(0, nullptr);
            m_meshTableBufferMapped[i] = nullptr;
        }
        m_meshTableBuffer[i].Reset();
    }
    if (m_reflectionProbeBuffer && m_reflectionProbeBufferMapped) {
        m_reflectionProbeBuffer->Unmap(0, nullptr);
        m_reflectionProbeBufferMapped = nullptr;
    }

    m_visibilityBuffer.Reset();
    m_gbufferAlbedo.Reset();
    m_gbufferNormalRoughness.Reset();
    m_gbufferEmissiveMetallic.Reset();
    m_gbufferMaterialExt0.Reset();
    m_gbufferMaterialExt1.Reset();
    m_gbufferMaterialExt2.Reset();
    m_reflectionProbeBuffer.Reset();
    m_dummyCullMaskBuffer.Reset();
    m_visibilityPipeline.Reset();
    m_visibilityRootSignature.Reset();
    m_resolvePipeline.Reset();
    m_resolveRootSignature.Reset();
    m_motionVectorsPipeline.Reset();
    m_motionVectorsRootSignature.Reset();
    m_deferredLightingCB.Reset();

    m_localLightsSRV = {};
    m_clusterRangesSRV = {};
    m_clusterLightIndicesSRV = {};

    m_device = nullptr;
    m_descriptorManager = nullptr;
    m_bindlessManager = nullptr;
}

Result<void> VisibilityBufferRenderer::Resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return Result<void>::Ok();
    }

    if (m_flushCallback) {
        m_flushCallback();
    }

    m_width = width;
    m_height = height;

    // Recreate visibility buffer
    m_visibilityBuffer.Reset();
    auto vbResult = CreateVisibilityBuffer();
    if (vbResult.IsErr()) {
        return vbResult;
    }

    // Recreate G-buffers
    m_gbufferAlbedo.Reset();
    m_gbufferNormalRoughness.Reset();
    m_gbufferEmissiveMetallic.Reset();
    m_gbufferMaterialExt0.Reset();
    m_gbufferMaterialExt1.Reset();
    m_gbufferMaterialExt2.Reset();
    auto gbResult = CreateGBuffers();
    if (gbResult.IsErr()) {
        return gbResult;
    }

    spdlog::info("VisibilityBuffer resized to {}x{}", m_width, m_height);
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::CreateVisibilityBuffer() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32_UINT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R32G32_UINT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        IID_PPV_ARGS(&m_visibilityBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visibility buffer texture");
    }
    m_visibilityBuffer->SetName(L"VisibilityBuffer");
    m_visibilityState = D3D12_RESOURCE_STATE_COMMON;

    // Create RTV (reuse existing handle on resize to avoid descriptor leak)
    if (!m_visibilityRTV.IsValid()) {
        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate visibility RTV");
        }
        m_visibilityRTV = rtvResult.Value();
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R32G32_UINT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateRenderTargetView(
        m_visibilityBuffer.Get(), &rtvDesc, m_visibilityRTV.cpu
    );

    // Create SRV (reuse existing handle on resize to avoid descriptor leak)
    if (!m_visibilitySRV.IsValid()) {
        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate visibility SRV");
        }
        m_visibilitySRV = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32G32_UINT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(
        m_visibilityBuffer.Get(), &srvDesc, m_visibilitySRV.cpu
    );

    // Create UAV for compute access (reuse existing handles on resize to avoid descriptor leak)
    if (!m_visibilityUAV.IsValid()) {
        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            return Result<void>::Err("Failed to allocate visibility UAV");
        }
        m_visibilityUAV = uavResult.Value();
    }
    if (!m_visibilityUAVStaging.IsValid()) {
        auto uavStagingResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavStagingResult.IsErr()) {
            return Result<void>::Err("Failed to allocate visibility UAV staging descriptor");
        }
        m_visibilityUAVStaging = uavStagingResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32G32_UINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(
        m_visibilityBuffer.Get(), nullptr, &uavDesc, m_visibilityUAV.cpu
    );
    m_device->GetDevice()->CreateUnorderedAccessView(
        m_visibilityBuffer.Get(), nullptr, &uavDesc, m_visibilityUAVStaging.cpu
    );

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::CreateGBuffers() {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Albedo buffer (RGBA8 UNORM, stored as TYPELESS for UAV legality)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferAlbedo)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB albedo buffer");
        }
        m_gbufferAlbedo->SetName(L"VB_GBuffer_Albedo");
        m_albedoState = D3D12_RESOURCE_STATE_COMMON;

        // Create RTV (reuse existing handle on resize to avoid descriptor leak)
        if (!m_albedoRTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate albedo RTV");
            m_albedoRTV = rtvResult.Value();
        }
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferAlbedo.Get(), &rtvDesc, m_albedoRTV.cpu);

        // Create SRV (reuse existing handle on resize to avoid descriptor leak)
        if (!m_albedoSRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate albedo SRV");
            m_albedoSRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // linear
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferAlbedo.Get(), &srvDesc, m_albedoSRV.cpu);

        // Create UAV (reuse existing handle on resize to avoid descriptor leak)
        if (!m_albedoUAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate albedo UAV");
            m_albedoUAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferAlbedo.Get(), nullptr, &uavDesc, m_albedoUAV.cpu);
    }

    // Normal + Roughness buffer (RGBA16F)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferNormalRoughness)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB normal-roughness buffer");
        }
        m_gbufferNormalRoughness->SetName(L"VB_GBuffer_NormalRoughness");
        m_normalRoughnessState = D3D12_RESOURCE_STATE_COMMON;

        if (!m_normalRoughnessRTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness RTV");
            m_normalRoughnessRTV = rtvResult.Value();
        }
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferNormalRoughness.Get(), nullptr, m_normalRoughnessRTV.cpu);

        if (!m_normalRoughnessSRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness SRV");
            m_normalRoughnessSRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferNormalRoughness.Get(), &srvDesc, m_normalRoughnessSRV.cpu);

        if (!m_normalRoughnessUAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness UAV");
            m_normalRoughnessUAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferNormalRoughness.Get(), nullptr, &uavDesc, m_normalRoughnessUAV.cpu);
    }

    // Emissive + Metallic buffer (RGBA16F)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferEmissiveMetallic)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB emissive-metallic buffer");
        }
        m_gbufferEmissiveMetallic->SetName(L"VB_GBuffer_EmissiveMetallic");
        m_emissiveMetallicState = D3D12_RESOURCE_STATE_COMMON;

        if (!m_emissiveMetallicRTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic RTV");
            m_emissiveMetallicRTV = rtvResult.Value();
        }
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferEmissiveMetallic.Get(), nullptr, m_emissiveMetallicRTV.cpu);

        if (!m_emissiveMetallicSRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic SRV");
            m_emissiveMetallicSRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferEmissiveMetallic.Get(), &srvDesc, m_emissiveMetallicSRV.cpu);

        if (!m_emissiveMetallicUAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic UAV");
            m_emissiveMetallicUAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferEmissiveMetallic.Get(), nullptr, &uavDesc, m_emissiveMetallicUAV.cpu);
    }

    // Material extension buffer 0 (RGBA16F): clearcoat/specular/IOR
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferMaterialExt0)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB material ext0 buffer");
        }
        m_gbufferMaterialExt0->SetName(L"VB_GBuffer_MaterialExt0");
        m_materialExt0State = D3D12_RESOURCE_STATE_COMMON;

        if (!m_materialExt0RTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 RTV");
            m_materialExt0RTV = rtvResult.Value();
        }
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferMaterialExt0.Get(), nullptr, m_materialExt0RTV.cpu);

        if (!m_materialExt0SRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 SRV");
            m_materialExt0SRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt0.Get(), &srvDesc, m_materialExt0SRV.cpu);

        if (!m_materialExt0UAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 UAV");
            m_materialExt0UAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt0.Get(), nullptr, &uavDesc, m_materialExt0UAV.cpu);
    }

    // Material extension buffer 1 (RGBA16F): specular color + transmission
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferMaterialExt1)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB material ext1 buffer");
        }
        m_gbufferMaterialExt1->SetName(L"VB_GBuffer_MaterialExt1");
        m_materialExt1State = D3D12_RESOURCE_STATE_COMMON;

        if (!m_materialExt1RTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 RTV");
            m_materialExt1RTV = rtvResult.Value();
        }
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferMaterialExt1.Get(), nullptr, m_materialExt1RTV.cpu);

        if (!m_materialExt1SRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 SRV");
            m_materialExt1SRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt1.Get(), &srvDesc, m_materialExt1SRV.cpu);

        if (!m_materialExt1UAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 UAV");
            m_materialExt1UAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt1.Get(), nullptr, &uavDesc, m_materialExt1UAV.cpu);
    }

    // Material extension buffer 2 (RGBA8): compact material class and post masks.
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, &clearValue,
            IID_PPV_ARGS(&m_gbufferMaterialExt2)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB material ext2 buffer");
        }
        m_gbufferMaterialExt2->SetName(L"VB_GBuffer_MaterialExt2");
        m_materialExt2State = D3D12_RESOURCE_STATE_COMMON;

        if (!m_materialExt2RTV.IsValid()) {
            auto rtvResult = m_descriptorManager->AllocateRTV();
            if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext2 RTV");
            m_materialExt2RTV = rtvResult.Value();
        }
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferMaterialExt2.Get(), &rtvDesc, m_materialExt2RTV.cpu);

        if (!m_materialExt2SRV.IsValid()) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext2 SRV");
            m_materialExt2SRV = srvResult.Value();
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt2.Get(), &srvDesc, m_materialExt2SRV.cpu);

        if (!m_materialExt2UAV.IsValid()) {
            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate material ext2 UAV");
            m_materialExt2UAV = uavResult.Value();
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt2.Get(), nullptr, &uavDesc, m_materialExt2UAV.cpu);
    }

    return Result<void>::Ok();
}


} // namespace Cortex::Graphics
