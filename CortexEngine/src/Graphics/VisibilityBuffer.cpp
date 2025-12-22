#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DescriptorHeap.h"
#include "RHI/BindlessResources.h"
#include "RHI/DX12Pipeline.h"
#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {

Result<void> VisibilityBufferRenderer::Initialize(
    DX12Device* device,
    DescriptorHeapManager* descriptorManager,
    BindlessResourceManager* bindlessManager,
    uint32_t width,
    uint32_t height
) {
    if (!device || !descriptorManager) {
        return Result<void>::Err("VisibilityBuffer requires device and descriptor manager");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_bindlessManager = bindlessManager;
    m_width = width;
    m_height = height;

    // Create visibility buffer
    auto vbResult = CreateVisibilityBuffer();
    if (vbResult.IsErr()) {
        return vbResult;
    }

    // Create G-buffer outputs
    auto gbResult = CreateGBuffers();
    if (gbResult.IsErr()) {
        return gbResult;
    }

    // Create instance buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = m_maxInstances * sizeof(VBInstanceData);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_instanceBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visibility buffer instance buffer");
    }
    m_instanceBuffer->SetName(L"VB_InstanceBuffer");

    // Persistently map the instance buffer (upload heap allows this)
    D3D12_RANGE readRange = {0, 0}; // We won't read from this buffer on CPU
    hr = m_instanceBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_instanceBufferMapped));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to persistently map instance buffer");
    }

    // Create instance buffer SRV
    auto instanceSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (instanceSrvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate instance SRV: " + instanceSrvResult.Error());
    }
    m_instanceSRV = instanceSrvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_maxInstances;
    srvDesc.Buffer.StructureByteStride = sizeof(VBInstanceData);
    m_device->GetDevice()->CreateShaderResourceView(
        m_instanceBuffer.Get(), &srvDesc, m_instanceSRV.cpu
    );

    // Create a default-sized material buffer (upload heap, persistently mapped).
    // This is populated per-frame by the renderer and consumed by the material resolve compute shader.
    {
        D3D12_HEAP_PROPERTIES matHeapProps{};
        matHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC matDesc{};
        matDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        matDesc.Width = static_cast<UINT64>(m_maxMaterials) * sizeof(VBMaterialConstants);
        matDesc.Height = 1;
        matDesc.DepthOrArraySize = 1;
        matDesc.MipLevels = 1;
        matDesc.Format = DXGI_FORMAT_UNKNOWN;
        matDesc.SampleDesc.Count = 1;
        matDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hrMat = m_device->GetDevice()->CreateCommittedResource(
            &matHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &matDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_materialBuffer)
        );
        if (FAILED(hrMat)) {
            return Result<void>::Err("Failed to create VB material constants buffer");
        }
        m_materialBuffer->SetName(L"VB_MaterialBuffer");

        D3D12_RANGE readRangeMat{0, 0};
        hrMat = m_materialBuffer->Map(0, &readRangeMat, reinterpret_cast<void**>(&m_materialBufferMapped));
        if (FAILED(hrMat)) {
            return Result<void>::Err("Failed to persistently map VB material constants buffer");
        }
        m_materialCount = 0;
    }

    // Create a reflection probe table buffer (upload heap, persistently mapped).
    // This is populated per-frame by the renderer and accessed via bindless
    // index (ResourceDescriptorHeap[]) in deferred lighting.
    {
        D3D12_HEAP_PROPERTIES probeHeapProps{};
        probeHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC probeDesc{};
        probeDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        probeDesc.Width = static_cast<UINT64>(m_maxReflectionProbes) * sizeof(VBReflectionProbe);
        probeDesc.Height = 1;
        probeDesc.DepthOrArraySize = 1;
        probeDesc.MipLevels = 1;
        probeDesc.Format = DXGI_FORMAT_UNKNOWN;
        probeDesc.SampleDesc.Count = 1;
        probeDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hrProbe = m_device->GetDevice()->CreateCommittedResource(
            &probeHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &probeDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_reflectionProbeBuffer)
        );
        if (FAILED(hrProbe)) {
            return Result<void>::Err("Failed to create VB reflection probe buffer");
        }
        m_reflectionProbeBuffer->SetName(L"VB_ReflectionProbeBuffer");

        D3D12_RANGE readRangeProbe{0, 0};
        hrProbe = m_reflectionProbeBuffer->Map(0, &readRangeProbe, reinterpret_cast<void**>(&m_reflectionProbeBufferMapped));
        if (FAILED(hrProbe)) {
            return Result<void>::Err("Failed to persistently map VB reflection probe buffer");
        }

        auto probeSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (probeSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB reflection probe SRV: " + probeSrvResult.Error());
        }
        m_reflectionProbeSRV = probeSrvResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC probeSrvDesc{};
        probeSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        probeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        probeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        probeSrvDesc.Buffer.FirstElement = 0;
        probeSrvDesc.Buffer.NumElements = m_maxReflectionProbes;
        probeSrvDesc.Buffer.StructureByteStride = sizeof(VBReflectionProbe);
        probeSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(
            m_reflectionProbeBuffer.Get(), &probeSrvDesc, m_reflectionProbeSRV.cpu
        );

        m_reflectionProbeCount = 0;
    }

    // Create a default-sized mesh table buffer (upload heap, persistently mapped).
    // This is populated per-frame by ResolveMaterials and consumed by compute shaders.
    {
        D3D12_HEAP_PROPERTIES meshHeapProps{};
        meshHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC meshDesc{};
        meshDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        meshDesc.Width = static_cast<UINT64>(m_maxMeshes) * sizeof(VBMeshTableEntry);
        meshDesc.Height = 1;
        meshDesc.DepthOrArraySize = 1;
        meshDesc.MipLevels = 1;
        meshDesc.Format = DXGI_FORMAT_UNKNOWN;
        meshDesc.SampleDesc.Count = 1;
        meshDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hrMesh = m_device->GetDevice()->CreateCommittedResource(
            &meshHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &meshDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_meshTableBuffer)
        );
        if (FAILED(hrMesh)) {
            return Result<void>::Err("Failed to create VB mesh table buffer");
        }
        m_meshTableBuffer->SetName(L"VB_MeshTableBuffer");

        D3D12_RANGE readRangeMesh{0, 0};
        hrMesh = m_meshTableBuffer->Map(0, &readRangeMesh, reinterpret_cast<void**>(&m_meshTableBufferMapped));
        if (FAILED(hrMesh)) {
            return Result<void>::Err("Failed to persistently map VB mesh table buffer");
        }
        m_meshCount = 0;
    }

    // Reserve persistent SRV slots for clustered-light resources so shaders can
    // access them via ResourceDescriptorHeap[] without per-frame allocations.
    {
        auto localLightsSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (localLightsSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB local lights SRV: " + localLightsSrvResult.Error());
        }
        m_localLightsSRV = localLightsSrvResult.Value();

        auto clusterRangesSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (clusterRangesSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB cluster ranges SRV: " + clusterRangesSrvResult.Error());
        }
        m_clusterRangesSRV = clusterRangesSrvResult.Value();

        auto clusterIndicesSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (clusterIndicesSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB cluster indices SRV: " + clusterIndicesSrvResult.Error());
        }
        m_clusterLightIndicesSRV = clusterIndicesSrvResult.Value();

        // Initialize descriptors to null; they'll be updated once resources exist.
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
        nullSrv.Format = DXGI_FORMAT_UNKNOWN;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Buffer.FirstElement = 0;
        nullSrv.Buffer.NumElements = 1;
        nullSrv.Buffer.StructureByteStride = sizeof(uint32_t);
        nullSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_localLightsSRV.cpu);
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_clusterRangesSRV.cpu);
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_clusterLightIndicesSRV.cpu);
    }

    // Create pipelines
    auto pipelineResult = CreatePipelines();
    if (pipelineResult.IsErr()) {
        return pipelineResult;
    }

    spdlog::info("VisibilityBuffer initialized ({}x{}, max {} instances)",
                 m_width, m_height, m_maxInstances);

    return Result<void>::Ok();
}

void VisibilityBufferRenderer::Shutdown() {
    // Unmap instance buffer before releasing it
    if (m_instanceBuffer && m_instanceBufferMapped) {
        m_instanceBuffer->Unmap(0, nullptr);
        m_instanceBufferMapped = nullptr;
    }
    if (m_materialBuffer && m_materialBufferMapped) {
        m_materialBuffer->Unmap(0, nullptr);
        m_materialBufferMapped = nullptr;
    }
    if (m_meshTableBuffer && m_meshTableBufferMapped) {
        m_meshTableBuffer->Unmap(0, nullptr);
        m_meshTableBufferMapped = nullptr;
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
    m_instanceBuffer.Reset();
    m_materialBuffer.Reset();
    m_meshTableBuffer.Reset();
    m_reflectionProbeBuffer.Reset();
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

    // Create RTV
    auto rtvResult = m_descriptorManager->AllocateRTV();
    if (rtvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate visibility RTV");
    }
    m_visibilityRTV = rtvResult.Value();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R32G32_UINT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateRenderTargetView(
        m_visibilityBuffer.Get(), &rtvDesc, m_visibilityRTV.cpu
    );

    // Create SRV
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate visibility SRV");
    }
    m_visibilitySRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32G32_UINT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(
        m_visibilityBuffer.Get(), &srvDesc, m_visibilitySRV.cpu
    );

    // Create UAV for compute access
    auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (uavResult.IsErr()) {
        return Result<void>::Err("Failed to allocate visibility UAV");
    }
    m_visibilityUAV = uavResult.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32G32_UINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(
        m_visibilityBuffer.Get(), nullptr, &uavDesc, m_visibilityUAV.cpu
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

        // Create RTV
        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate albedo RTV");
        m_albedoRTV = rtvResult.Value();
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferAlbedo.Get(), &rtvDesc, m_albedoRTV.cpu);

        // Create SRV
        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate albedo SRV");
        m_albedoSRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // linear
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferAlbedo.Get(), &srvDesc, m_albedoSRV.cpu);

        // Create UAV
        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate albedo UAV");
        m_albedoUAV = uavResult.Value();
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

        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness RTV");
        m_normalRoughnessRTV = rtvResult.Value();
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferNormalRoughness.Get(), nullptr, m_normalRoughnessRTV.cpu);

        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness SRV");
        m_normalRoughnessSRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferNormalRoughness.Get(), &srvDesc, m_normalRoughnessSRV.cpu);

        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate normal-roughness UAV");
        m_normalRoughnessUAV = uavResult.Value();
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

        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic RTV");
        m_emissiveMetallicRTV = rtvResult.Value();
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferEmissiveMetallic.Get(), nullptr, m_emissiveMetallicRTV.cpu);

        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic SRV");
        m_emissiveMetallicSRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferEmissiveMetallic.Get(), &srvDesc, m_emissiveMetallicSRV.cpu);

        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate emissive-metallic UAV");
        m_emissiveMetallicUAV = uavResult.Value();
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

        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 RTV");
        m_materialExt0RTV = rtvResult.Value();
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferMaterialExt0.Get(), nullptr, m_materialExt0RTV.cpu);

        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 SRV");
        m_materialExt0SRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt0.Get(), &srvDesc, m_materialExt0SRV.cpu);

        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate material ext0 UAV");
        m_materialExt0UAV = uavResult.Value();
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

        auto rtvResult = m_descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 RTV");
        m_materialExt1RTV = rtvResult.Value();
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferMaterialExt1.Get(), nullptr, m_materialExt1RTV.cpu);

        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 SRV");
        m_materialExt1SRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferMaterialExt1.Get(), &srvDesc, m_materialExt1SRV.cpu);

        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate material ext1 UAV");
        m_materialExt1UAV = uavResult.Value();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->GetDevice()->CreateUnorderedAccessView(m_gbufferMaterialExt1.Get(), nullptr, &uavDesc, m_materialExt1UAV.cpu);
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::CreateRootSignatures() {
    // ========================================================================
    // Visibility Pass Root Signature
    // Matches VisibilityPass.hlsl:
    //   b0: ViewProjection matrix + mesh index (16 + 4 = 20 dwords)
    //   t0: Instance data (StructuredBuffer<VBInstanceData>)
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[2] = {};

        // b0: View-projection matrix (16 floats) + mesh index (1 uint) + padding (3 uints)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 20;  // 16 for matrix + 4 for (meshIdx + materialCount + pad2)
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        // t0: Instance buffer SRV (via descriptor)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 2;
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize visibility root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_visibilityRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create visibility root signature");
        }
    }

    // ========================================================================
    // Alpha-Tested Visibility Pass Root Signature
    // Matches VisibilityPass.hlsl PSMainAlphaTest:
    //   b0: ViewProjection matrix + mesh index + material count (20 dwords)
    //   t0: Instance data (StructuredBuffer<VBInstanceData>)
    //   t1: Material constants (StructuredBuffer<VBMaterialConstants>)
    //   s0: Linear wrap sampler for baseColor alpha test
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[3] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 20;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 1;
        rootDesc.Desc_1_1.pStaticSamplers = &sampler;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#ifdef ENABLE_BINDLESS
        rootDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize alpha-tested visibility root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_visibilityAlphaRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create alpha-tested visibility root signature");
        }
    }

    // ========================================================================
    // Material Resolve Root Signature (Compute)
    // Matches MaterialResolve.hlsl:
    //   b0: Resolution constants (width, height, rcpWidth, rcpHeight)
    //   t0: Visibility buffer SRV (Texture2D - needs descriptor table)
    //   t1: Instance data SRV (StructuredBuffer - can use root descriptor)
    //   t2: Depth buffer SRV (Texture2D - needs descriptor table)
    //   t3: Mesh table SRV (StructuredBuffer - can use root descriptor)
    //   t5: Material constants SRV (StructuredBuffer - can use root descriptor)
    //   u0-u4: G-buffer UAVs (RWTexture2D - need descriptor tables)
    //   s0: Linear wrap sampler for material textures
    // ========================================================================
    {
        // Descriptor ranges for texture SRVs and UAVs
        D3D12_DESCRIPTOR_RANGE1 srvRanges[2] = {};
        // t0: Visibility buffer
        srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[0].NumDescriptors = 1;
        srvRanges[0].BaseShaderRegister = 0;
        srvRanges[0].RegisterSpace = 0;
        srvRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        srvRanges[0].OffsetInDescriptorsFromTableStart = 0;

        // t2: Depth buffer
        srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[1].NumDescriptors = 1;
        srvRanges[1].BaseShaderRegister = 2;
        srvRanges[1].RegisterSpace = 0;
        srvRanges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        srvRanges[1].OffsetInDescriptorsFromTableStart = 1;

        // u0-u4: G-buffer UAVs (5 consecutive UAVs)
        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 5;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[6] = {};

        // b0: Resolution constants + view-projection matrix + mesh index (4 + 16 + 4 = 24 dwords)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 24;  // 4 for resolution + 16 for mat4x4 + 4 for mesh index + padding
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1: Instance data SRV (StructuredBuffer - can use root descriptor)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 1;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0 + t2: Visibility buffer + depth buffer SRVs (descriptor table)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 2;
        params[2].DescriptorTable.pDescriptorRanges = srvRanges;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0-u4: G-buffer UAVs (descriptor table)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &uavRange;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t3: Mesh table (StructuredBuffer - root descriptor)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor.ShaderRegister = 3;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t5: Material constants buffer (StructuredBuffer - root descriptor)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[5].Descriptor.ShaderRegister = 5;
        params[5].Descriptor.RegisterSpace = 0;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 6;
        rootDesc.Desc_1_1.pParameters = params;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootDesc.Desc_1_1.NumStaticSamplers = 1;
        rootDesc.Desc_1_1.pStaticSamplers = &sampler;
        // Required for SM6.6 bindless access via ResourceDescriptorHeap[].
        rootDesc.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize resolve root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_resolveRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create resolve root signature");
        }
    }

    // ========================================================================
    // VB Motion Vectors Root Signature (Compute)
    // Matches VBMotionVectors.hlsl:
    //   b0: Dispatch constants (width/height/rcp + meshCount)
    //   b1: FrameConstants (current + previous camera matrices)
    //   t0: Visibility buffer SRV (descriptor table)
    //   t1: Instance data SRV (root descriptor)
    //   t3: Mesh table SRV (root descriptor)
    //   u0: Velocity UAV (descriptor table)
    // ========================================================================
    {
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        srvRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[6] = {};

        // b0: small per-dispatch constants
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 8; // width/height/rcpW/rcpH + meshCount + padding
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // b1: FrameConstants CBV (root CBV)
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[1].Descriptor.ShaderRegister = 1;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1: Instance data SRV (root SRV)
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t3: Mesh table SRV (root SRV)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 3;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0: Visibility buffer SRV (descriptor table)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[4].DescriptorTable.NumDescriptorRanges = 1;
        params[4].DescriptorTable.pDescriptorRanges = &srvRange;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0: Velocity UAV (descriptor table)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[5].DescriptorTable.NumDescriptorRanges = 1;
        params[5].DescriptorTable.pDescriptorRanges = &uavRange;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootDesc.Desc_1_1.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize motion vectors root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_motionVectorsRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create motion vectors root signature");
        }
    }

    // ========================================================================
    // Clustered Light Culling Root Signature (Compute)
    // Matches ClusteredLightCulling.hlsl:
    //   b0: view matrix + projection/screen/cluster params (root constants)
    //   t0: local lights (StructuredBuffer<Light>) as root SRV
    //   u0: cluster ranges (RWStructuredBuffer<uint2>) as root UAV
    //   u1: cluster indices (RWStructuredBuffer<uint>) as root UAV
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[4] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 28; // mat4 (16) + 3 vec4/uvec4 (12)
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[2].Descriptor.ShaderRegister = 0;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].Descriptor.ShaderRegister = 1;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = _countof(params);
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to serialize clustered light culling root signature");
        }

        hr = m_device->GetDevice()->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_clusterRootSignature)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create clustered light culling root signature");
        }
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::CreatePipelines() {
    // Create root signatures first
    auto rsResult = CreateRootSignatures();
    if (rsResult.IsErr()) {
        return rsResult;
    }

    ID3D12Device* device = m_device->GetDevice();

    // ========================================================================
    // Phase 1: Visibility Pass Pipeline (Graphics)
    // ========================================================================

    // Compile VisibilityPass vertex shader
    auto vsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/VisibilityPass.hlsl",
        "VSMain",
        "vs_6_6"
    );
    if (vsResult.IsErr()) {
        return Result<void>::Err("Failed to compile VisibilityPass VS: " + vsResult.Error());
    }

    // Compile VisibilityPass pixel shader
    auto psResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/VisibilityPass.hlsl",
        "PSMain",
        "ps_6_6"
    );
    if (psResult.IsErr()) {
        return Result<void>::Err("Failed to compile VisibilityPass PS: " + psResult.Error());
    }

    // Input layout MUST match actual Vertex structure (48 bytes)
    // Shader only uses POSITION, but IA needs full layout for correct stride
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Graphics pipeline state for visibility pass
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_visibilityRootSignature.Get();
    psoDesc.VS = { vsResult.Value().data.data(), vsResult.Value().data.size() };
    psoDesc.PS = { psResult.Value().data.data(), psResult.Value().data.size() };
    psoDesc.BlendState.RenderTarget[0] = {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL
    };
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32_UINT;  // Visibility buffer format
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_visibilityPipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create visibility pass PSO");
    }

    spdlog::info("VisibilityBuffer: Visibility pass pipeline created");

    // Double-sided visibility PSO (cull none) for glTF doubleSided materials.
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dsDesc = psoDesc;
        dsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        hr = device->CreateGraphicsPipelineState(&dsDesc, IID_PPV_ARGS(&m_visibilityPipelineDoubleSided));
        if (FAILED(hr)) {
            spdlog::warn("VisibilityBuffer: Failed to create double-sided visibility PSO (falling back to cull-back)");
            m_visibilityPipelineDoubleSided.Reset();
        } else {
            spdlog::info("VisibilityBuffer: Double-sided visibility pipeline created");
        }
    }

    // Alpha-tested visibility pipeline (same VS, alpha-discard PS)
    {
        auto alphaPS = ShaderCompiler::CompileFromFile(
            "assets/shaders/VisibilityPass.hlsl",
            "PSMainAlphaTest",
            "ps_6_6"
        );
        if (alphaPS.IsErr()) {
            return Result<void>::Err("Failed to compile VisibilityPass PSMainAlphaTest: " + alphaPS.Error());
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaDesc = psoDesc;
        alphaDesc.pRootSignature = m_visibilityAlphaRootSignature.Get();
        alphaDesc.PS = { alphaPS.Value().data.data(), alphaPS.Value().data.size() };

        hr = device->CreateGraphicsPipelineState(&alphaDesc, IID_PPV_ARGS(&m_visibilityAlphaPipeline));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create alpha-tested visibility PSO");
        }

        spdlog::info("VisibilityBuffer: Alpha-tested visibility pipeline created");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaDsDesc = alphaDesc;
        alphaDsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        hr = device->CreateGraphicsPipelineState(&alphaDsDesc, IID_PPV_ARGS(&m_visibilityAlphaPipelineDoubleSided));
        if (FAILED(hr)) {
            spdlog::warn("VisibilityBuffer: Failed to create double-sided alpha-tested visibility PSO (falling back to cull-back)");
            m_visibilityAlphaPipelineDoubleSided.Reset();
        } else {
            spdlog::info("VisibilityBuffer: Double-sided alpha-tested visibility pipeline created");
        }
    }

    // ========================================================================
    // Phase 2: Material Resolve Pipeline (Compute)
    // ========================================================================

    // Compile MaterialResolve compute shader
    auto csResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/MaterialResolve.hlsl",
        "CSMain",
        "cs_6_6"
    );
    if (csResult.IsErr()) {
        return Result<void>::Err("Failed to compile MaterialResolve CS: " + csResult.Error());
    }

    // Compute pipeline state for material resolve
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = m_resolveRootSignature.Get();
    computePsoDesc.CS = { csResult.Value().data.data(), csResult.Value().data.size() };

    hr = device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_resolvePipeline));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create material resolve PSO");
    }

    spdlog::info("VisibilityBuffer: Material resolve pipeline created");

    // ========================================================================
    // Optional: VB Motion Vectors Pipeline (Compute)
    // ========================================================================
    {
        auto mvCs = ShaderCompiler::CompileFromFile(
            "assets/shaders/VBMotionVectors.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (mvCs.IsErr()) {
            spdlog::warn("Failed to compile VBMotionVectors CS: {}", mvCs.Error());
        } else {
            D3D12_COMPUTE_PIPELINE_STATE_DESC mvPso{};
            mvPso.pRootSignature = m_motionVectorsRootSignature.Get();
            mvPso.CS = { mvCs.Value().data.data(), mvCs.Value().data.size() };

            hr = device->CreateComputePipelineState(&mvPso, IID_PPV_ARGS(&m_motionVectorsPipeline));
            if (FAILED(hr)) {
                spdlog::warn("Failed to create VB motion vectors PSO");
                m_motionVectorsPipeline.Reset();
            } else {
                spdlog::info("VisibilityBuffer: Motion vectors pipeline created");
            }
        }
    }

    // ========================================================================
    // Clustered Light Culling Pipeline (Compute)
    // ========================================================================
    {
        auto clusterCS = ShaderCompiler::CompileFromFile(
            "assets/shaders/ClusteredLightCulling.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (clusterCS.IsErr()) {
            return Result<void>::Err("Failed to compile ClusteredLightCulling CS: " + clusterCS.Error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC clusterPso{};
        clusterPso.pRootSignature = m_clusterRootSignature.Get();
        clusterPso.CS = { clusterCS.Value().data.data(), clusterCS.Value().data.size() };

        hr = device->CreateComputePipelineState(&clusterPso, IID_PPV_ARGS(&m_clusterPipeline));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create clustered light culling PSO");
        }

        m_clusterCount = m_clusterCountX * m_clusterCountY * m_clusterCountZ;
        if (!m_clusterRangesBuffer || !m_clusterLightIndicesBuffer) {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC rangesDesc{};
            rangesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rangesDesc.Width = static_cast<UINT64>(m_clusterCount) * sizeof(uint32_t) * 2ull; // uint2
            rangesDesc.Height = 1;
            rangesDesc.DepthOrArraySize = 1;
            rangesDesc.MipLevels = 1;
            rangesDesc.SampleDesc.Count = 1;
            rangesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rangesDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            HRESULT hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &rangesDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_clusterRangesBuffer)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create clustered light ranges buffer");
            }
            m_clusterRangesBuffer->SetName(L"VB_ClusterRanges");
            m_clusterRangesState = D3D12_RESOURCE_STATE_COMMON;

            D3D12_RESOURCE_DESC indicesDesc{};
            indicesDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            indicesDesc.Width = static_cast<UINT64>(m_clusterCount) * static_cast<UINT64>(m_maxLightsPerCluster) * sizeof(uint32_t);
            indicesDesc.Height = 1;
            indicesDesc.DepthOrArraySize = 1;
            indicesDesc.MipLevels = 1;
            indicesDesc.SampleDesc.Count = 1;
            indicesDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            indicesDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &indicesDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_clusterLightIndicesBuffer)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create clustered light indices buffer");
            }
            m_clusterLightIndicesBuffer->SetName(L"VB_ClusterLightIndices");
            m_clusterLightIndicesState = D3D12_RESOURCE_STATE_COMMON;
        }

        // Create persistent SRVs for the clustered light lists so forward+ and
        // deferred shaders can sample them via ResourceDescriptorHeap[].
        if (m_clusterRangesSRV.IsValid() && m_clusterRangesBuffer) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_clusterCount;
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t) * 2; // uint2
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_clusterRangesBuffer.Get(), &srvDesc, m_clusterRangesSRV.cpu);
        }
        if (m_clusterLightIndicesSRV.IsValid() && m_clusterLightIndicesBuffer) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_clusterCount * m_maxLightsPerCluster;
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_clusterLightIndicesBuffer.Get(), &srvDesc, m_clusterLightIndicesSRV.cpu);
        }

        spdlog::info("VisibilityBuffer: Clustered light culling pipeline created (clusters={}x{}x{}, maxLightsPerCluster={})",
                     m_clusterCountX, m_clusterCountY, m_clusterCountZ, m_maxLightsPerCluster);
    }

    // ========================================================================
    // BRDF LUT Generation Pipeline (Compute)
    // ========================================================================
    {
        constexpr uint32_t kBrdfLutSize = 256;

        if (!m_brdfLut) {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC texDesc{};
            texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texDesc.Width = kBrdfLutSize;
            texDesc.Height = kBrdfLutSize;
            texDesc.DepthOrArraySize = 1;
            texDesc.MipLevels = 1;
            texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            texDesc.SampleDesc.Count = 1;
            texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            HRESULT hr2 = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&m_brdfLut)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create BRDF LUT texture");
            }
            m_brdfLut->SetName(L"BRDFLUT");
            m_brdfLutState = D3D12_RESOURCE_STATE_COMMON;

            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate BRDF LUT SRV");
            }
            m_brdfLutSRV = srvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->GetDevice()->CreateShaderResourceView(m_brdfLut.Get(), &srvDesc, m_brdfLutSRV.cpu);

            auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (uavResult.IsErr()) {
                return Result<void>::Err("Failed to allocate BRDF LUT UAV");
            }
            m_brdfLutUAV = uavResult.Value();

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            m_device->GetDevice()->CreateUnorderedAccessView(m_brdfLut.Get(), nullptr, &uavDesc, m_brdfLutUAV.cpu);

            m_brdfLutReady = false;
        }

        if (!m_brdfLutRootSignature) {
            D3D12_DESCRIPTOR_RANGE1 uavRange{};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0;
            uavRange.RegisterSpace = 0;
            uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
            uavRange.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER1 params[2] = {};

            // b0: {width,height,rcpWidth,rcpHeight}
            params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.RegisterSpace = 0;
            params[0].Constants.Num32BitValues = 4;
            params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // u0: BRDF LUT UAV
            params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges = &uavRange;
            params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
            rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            rootDesc.Desc_1_1.NumParameters = _countof(params);
            rootDesc.Desc_1_1.pParameters = params;
            rootDesc.Desc_1_1.NumStaticSamplers = 0;
            rootDesc.Desc_1_1.pStaticSamplers = nullptr;
            rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            ComPtr<ID3DBlob> signature, error;
            HRESULT hr2 = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
            if (FAILED(hr2)) {
                if (error) {
                    return Result<void>::Err(std::string("Failed to serialize BRDF LUT root signature: ") +
                                            static_cast<const char*>(error->GetBufferPointer()));
                }
                return Result<void>::Err("Failed to serialize BRDF LUT root signature");
            }

            hr2 = m_device->GetDevice()->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(),
                IID_PPV_ARGS(&m_brdfLutRootSignature)
            );
            if (FAILED(hr2)) {
                return Result<void>::Err("Failed to create BRDF LUT root signature");
            }
        }

        auto brdfCS = ShaderCompiler::CompileFromFile(
            "assets/shaders/BRDFLUT.hlsl",
            "CSMain",
            "cs_6_6"
        );
        if (brdfCS.IsErr()) {
            return Result<void>::Err("Failed to compile BRDF LUT CS: " + brdfCS.Error());
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC brdfPso{};
        brdfPso.pRootSignature = m_brdfLutRootSignature.Get();
        brdfPso.CS = { brdfCS.Value().data.data(), brdfCS.Value().data.size() };

        HRESULT hr2 = device->CreateComputePipelineState(&brdfPso, IID_PPV_ARGS(&m_brdfLutPipeline));
        if (FAILED(hr2)) {
            return Result<void>::Err("Failed to create BRDF LUT PSO");
        }

        spdlog::info("VisibilityBuffer: BRDF LUT pipeline created");
    }

    // ========================================================================
    // Debug Blit Pipeline (Graphics)
    // ========================================================================

    // Compile blit shaders
    auto blitVS = ShaderCompiler::CompileFromFile(
        "assets/shaders/DebugBlitAlbedo.hlsl",
        "VSMain",
        "vs_6_6"
    );
    if (blitVS.IsErr()) {
        spdlog::warn("Failed to compile DebugBlitAlbedo VS: {}", blitVS.Error());
    }

    auto blitPS = ShaderCompiler::CompileFromFile(
        "assets/shaders/DebugBlitAlbedo.hlsl",
        "PSMain",
        "ps_6_6"
    );
    if (blitPS.IsErr()) {
        spdlog::warn("Failed to compile DebugBlitAlbedo PS: {}", blitPS.Error());
    }

    bool blitReady = blitVS.IsOk() && blitPS.IsOk();

    // Create blit root signature: t0 (albedo SRV), s0 (sampler)
    if (blitReady) {
        D3D12_DESCRIPTOR_RANGE1 srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

        D3D12_DESCRIPTOR_RANGE1 samplerRange = {};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;
        samplerRange.BaseShaderRegister = 0;
        samplerRange.RegisterSpace = 0;
        samplerRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

        D3D12_ROOT_PARAMETER1 params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srvRange;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC blitRootDesc = {};
        blitRootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        blitRootDesc.Desc_1_1.NumParameters = 2;
        blitRootDesc.Desc_1_1.pParameters = params;
        blitRootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> signature, error;
        hr = D3D12SerializeVersionedRootSignature(&blitRootDesc, &signature, &error);
        if (FAILED(hr)) {
            spdlog::warn("Failed to serialize blit root signature");
            blitReady = false;
        } else {
            hr = device->CreateRootSignature(
                0, signature->GetBufferPointer(), signature->GetBufferSize(),
                IID_PPV_ARGS(&m_blitRootSignature)
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create blit root signature");
                blitReady = false;
            }
        }
    }

    // Create blit PSO (fullscreen triangle, no depth)
    if (blitReady) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC blitPsoDesc = {};
        blitPsoDesc.pRootSignature = m_blitRootSignature.Get();
        blitPsoDesc.VS = { blitVS.Value().data.data(), blitVS.Value().data.size() };
        blitPsoDesc.PS = { blitPS.Value().data.data(), blitPS.Value().data.size() };
        blitPsoDesc.BlendState.RenderTarget[0] = {
            FALSE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL
        };
        blitPsoDesc.SampleMask = UINT_MAX;
        blitPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        blitPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        blitPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        blitPsoDesc.RasterizerState.DepthClipEnable = FALSE;
        blitPsoDesc.RasterizerState.MultisampleEnable = FALSE;
        blitPsoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        blitPsoDesc.DepthStencilState.DepthEnable = FALSE;
        blitPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        blitPsoDesc.DepthStencilState.StencilEnable = FALSE;
        blitPsoDesc.InputLayout = { nullptr, 0 }; // No input layout
        blitPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        blitPsoDesc.NumRenderTargets = 1;
        blitPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
        blitPsoDesc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&blitPsoDesc, IID_PPV_ARGS(&m_blitPipeline));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create blit PSO");
            blitReady = false;
        }
    }

    // Create dedicated sampler heap for blit sampler
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.NumDescriptors = 1;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (blitReady) {
        hr = device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_blitSamplerHeap));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create blit sampler heap");
            blitReady = false;
        }
    }

    // Create linear sampler for blit
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    if (blitReady) {
        device->CreateSampler(&samplerDesc, m_blitSamplerHeap->GetCPUDescriptorHandleForHeapStart());
    }

    if (!blitReady) {
        m_blitPipeline.Reset();
        m_blitRootSignature.Reset();
        m_blitSamplerHeap.Reset();
    } else {
        spdlog::info("VisibilityBuffer: Debug blit pipeline created");
    }

    // ========================================================================
    // Deferred Lighting Root Signature (Graphics - Fullscreen Pass)
    // Matches DeferredLighting.hlsl:
    //   b0: Lighting parameters (view matrices, sun, IBL, etc.)
    //   t0: G-buffer albedo SRV
    //   t1: G-buffer normal+roughness SRV
    //   t2: G-buffer emissive+metallic SRV
    //   t3: Depth buffer SRV
    //   t4: Material extension 0 SRV (clearcoat/IOR/specular)
    //   t5: Material extension 1 SRV (specularColor/transmission)
    //   t6: Diffuse irradiance environment SRV (lat-long)
    //   t7: Specular prefiltered environment SRV (lat-long)
    //   t8: Shadow map array SRV (Texture2DArray)
    //   t9: BRDF LUT SRV (Texture2D<float2>)
    //   t10: Local lights (StructuredBuffer<Light>)
    //   t11: Cluster ranges (StructuredBuffer<uint2>)
    //   t12: Cluster light indices (StructuredBuffer<uint>)
    //   s0: Linear sampler
    //   s1: Shadow sampler
    // ========================================================================
    {
        // Descriptor ranges for G-buffer SRVs (t0-t5: 6 textures)
        D3D12_DESCRIPTOR_RANGE1 gbufferRange = {};
        gbufferRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        gbufferRange.NumDescriptors = 6;
        gbufferRange.BaseShaderRegister = 0;
        gbufferRange.RegisterSpace = 0;
        gbufferRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        gbufferRange.OffsetInDescriptorsFromTableStart = 0;

        // Descriptor ranges for env + shadow + BRDF LUT (t6-t9: 4 textures)
        D3D12_DESCRIPTOR_RANGE1 envShadowRange = {};
        envShadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        envShadowRange.NumDescriptors = 4;
        envShadowRange.BaseShaderRegister = 6;
        envShadowRange.RegisterSpace = 0;
        envShadowRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        envShadowRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[6] = {};

        // b0: Lighting parameters (root CBV descriptor - too large for inline constants)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t0-t5: G-buffer + depth + material extension SRVs
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &gbufferRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t6-t9: Environment + shadow map + BRDF LUT SRVs
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &envShadowRange;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t10: Local lights SRV (root descriptor)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[3].Descriptor.ShaderRegister = 10;
        params[3].Descriptor.RegisterSpace = 0;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t11: Cluster ranges SRV (root descriptor)
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[4].Descriptor.ShaderRegister = 11;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // t12: Cluster light indices SRV (root descriptor)
        params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[5].Descriptor.ShaderRegister = 12;
        params[5].Descriptor.RegisterSpace = 0;
        params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Static samplers
        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

        // s0: Linear sampler
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0;
        samplers[0].RegisterSpace = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // s1: Shadow sampler (manual PCF in shader)
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[1].ShaderRegister = 1;
        samplers[1].RegisterSpace = 0;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 6;
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 2;
        rootDesc.Desc_1_1.pStaticSamplers = samplers;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
#ifdef ENABLE_BINDLESS
        rootDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
#endif

        ComPtr<ID3DBlob> signature, error;
        hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                spdlog::warn("Failed to serialize deferred lighting root signature: {}",
                    static_cast<const char*>(error->GetBufferPointer()));
            } else {
                spdlog::warn("Failed to serialize deferred lighting root signature (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            }
            return Result<void>::Ok();
        }

        hr = device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&m_deferredLightingRootSignature)
        );
        if (FAILED(hr)) {
            spdlog::warn("Failed to create deferred lighting root signature (HRESULT: 0x{:08X})", static_cast<uint32_t>(hr));
            return Result<void>::Ok();
        }
    }

    // ========================================================================
    // Deferred Lighting Pipeline (Graphics - Fullscreen)
    // ========================================================================
    {
        // Compile shaders
        auto deferredVS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "VSMain",
            "vs_6_6"
        );
        if (deferredVS.IsErr()) {
            spdlog::warn("Failed to compile deferred lighting VS: {}", deferredVS.Error());
            return Result<void>::Ok();
        }

        auto deferredPS = ShaderCompiler::CompileFromFile(
            "assets/shaders/DeferredLighting.hlsl",
            "PSMain",
            "ps_6_6"
        );
        if (deferredPS.IsErr()) {
            spdlog::warn("Failed to compile deferred lighting PS: {}", deferredPS.Error());
            return Result<void>::Ok();
        }

        // Create PSO
        D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredPsoDesc = {};
        deferredPsoDesc.pRootSignature = m_deferredLightingRootSignature.Get();
        deferredPsoDesc.VS = {deferredVS.Value().data.data(), deferredVS.Value().data.size()};
        deferredPsoDesc.PS = {deferredPS.Value().data.data(), deferredPS.Value().data.size()};

        deferredPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        deferredPsoDesc.SampleMask = UINT_MAX;

        deferredPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        deferredPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        deferredPsoDesc.RasterizerState.DepthClipEnable = TRUE;

        deferredPsoDesc.DepthStencilState.DepthEnable = FALSE;
        deferredPsoDesc.DepthStencilState.StencilEnable = FALSE;

        deferredPsoDesc.InputLayout = {nullptr, 0};
        deferredPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        deferredPsoDesc.NumRenderTargets = 1;
        deferredPsoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
        deferredPsoDesc.SampleDesc.Count = 1;

        hr = device->CreateGraphicsPipelineState(&deferredPsoDesc, IID_PPV_ARGS(&m_deferredLightingPipeline));
        if (FAILED(hr)) {
            spdlog::warn("Failed to create deferred lighting PSO");
            return Result<void>::Ok();
        }

        spdlog::info("VisibilityBuffer: Deferred lighting pipeline created");
    }

    spdlog::info("VisibilityBuffer pipelines created successfully");

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateInstances(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBInstanceData>& instances
) {
    if (instances.empty()) {
        m_instanceCount = 0;
        return Result<void>::Ok();
    }

    if (instances.size() > m_maxInstances) {
        return Result<void>::Err("Too many instances for visibility buffer");
    }

    if (!m_instanceBufferMapped) {
        return Result<void>::Err("Instance buffer not mapped");
    }

    // Write to persistently mapped buffer
    memcpy(m_instanceBufferMapped, instances.data(), instances.size() * sizeof(VBInstanceData));

    m_instanceCount = static_cast<uint32_t>(instances.size());
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateMaterials(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBMaterialConstants>& materials
) {
    (void)cmdList;

    m_materialCount = static_cast<uint32_t>(materials.size());
    if (materials.empty()) {
        return Result<void>::Ok();
    }

    if (materials.size() > m_maxMaterials) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        // Grow capacity (next power-of-two-ish) to reduce churn.
        uint32_t newCap = m_maxMaterials;
        while (newCap < static_cast<uint32_t>(materials.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxMaterials = newCap;

        if (m_materialBuffer && m_materialBufferMapped) {
            m_materialBuffer->Unmap(0, nullptr);
            m_materialBufferMapped = nullptr;
        }
        m_materialBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxMaterials) * sizeof(VBMaterialConstants);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_materialBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to resize VB material constants buffer");
        }
        m_materialBuffer->SetName(L"VB_MaterialBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_materialBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_materialBufferMapped));
        if (FAILED(hr) || !m_materialBufferMapped) {
            return Result<void>::Err("Failed to map resized VB material constants buffer");
        }
    }

    if (!m_materialBufferMapped) {
        return Result<void>::Err("VB material constants buffer not mapped");
    }

    memcpy(m_materialBufferMapped, materials.data(), materials.size() * sizeof(VBMaterialConstants));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateReflectionProbes(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<VBReflectionProbe>& probes
) {
    (void)cmdList;

    m_reflectionProbeCount = static_cast<uint32_t>(probes.size());
    if (probes.empty()) {
        return Result<void>::Ok();
    }

    if (probes.size() > m_maxReflectionProbes) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        uint32_t newCap = m_maxReflectionProbes;
        while (newCap < static_cast<uint32_t>(probes.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxReflectionProbes = newCap;

        if (m_reflectionProbeBuffer && m_reflectionProbeBufferMapped) {
            m_reflectionProbeBuffer->Unmap(0, nullptr);
            m_reflectionProbeBufferMapped = nullptr;
        }
        m_reflectionProbeBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxReflectionProbes) * sizeof(VBReflectionProbe);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_reflectionProbeBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to resize VB reflection probe buffer");
        }
        m_reflectionProbeBuffer->SetName(L"VB_ReflectionProbeBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_reflectionProbeBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_reflectionProbeBufferMapped));
        if (FAILED(hr) || !m_reflectionProbeBufferMapped) {
            return Result<void>::Err("Failed to map resized VB reflection probe buffer");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_maxReflectionProbes;
        srvDesc.Buffer.StructureByteStride = sizeof(VBReflectionProbe);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(
            m_reflectionProbeBuffer.Get(), &srvDesc, m_reflectionProbeSRV.cpu
        );
    }

    if (!m_reflectionProbeBufferMapped) {
        return Result<void>::Err("VB reflection probe buffer not mapped");
    }

    memcpy(m_reflectionProbeBufferMapped, probes.data(), probes.size() * sizeof(VBReflectionProbe));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::UpdateLocalLights(
    ID3D12GraphicsCommandList* cmdList,
    const std::vector<Light>& localLights
) {
    (void)cmdList;

    m_localLightCount = static_cast<uint32_t>(localLights.size());
    if (localLights.empty()) {
        return Result<void>::Ok();
    }

    if (localLights.size() > m_maxLocalLights) {
        if (m_flushCallback) {
            m_flushCallback();
        }

        uint32_t newCap = m_maxLocalLights;
        while (newCap < static_cast<uint32_t>(localLights.size())) {
            newCap = std::max(1u, newCap * 2u);
        }
        m_maxLocalLights = newCap;

        if (m_localLightsBuffer && m_localLightsBufferMapped) {
            m_localLightsBuffer->Unmap(0, nullptr);
            m_localLightsBufferMapped = nullptr;
        }
        m_localLightsBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxLocalLights) * sizeof(Light);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_localLightsBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to resize VB local lights buffer");
        }
        m_localLightsBuffer->SetName(L"VB_LocalLightsBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_localLightsBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_localLightsBufferMapped));
        if (FAILED(hr) || !m_localLightsBufferMapped) {
            return Result<void>::Err("Failed to map resized VB local lights buffer");
        }

        if (m_localLightsSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_maxLocalLights;
            srvDesc.Buffer.StructureByteStride = sizeof(Light);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_localLightsBuffer.Get(), &srvDesc, m_localLightsSRV.cpu);
        }
    }

    if (!m_localLightsBufferMapped) {
        // First-use allocation (keep consistent with the grow path).
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = static_cast<UINT64>(m_maxLocalLights) * sizeof(Light);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_localLightsBuffer)
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create VB local lights buffer");
        }
        m_localLightsBuffer->SetName(L"VB_LocalLightsBuffer");

        D3D12_RANGE readRange{0, 0};
        hr = m_localLightsBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_localLightsBufferMapped));
        if (FAILED(hr) || !m_localLightsBufferMapped) {
            return Result<void>::Err("Failed to map VB local lights buffer");
        }

        if (m_localLightsSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = m_maxLocalLights;
            srvDesc.Buffer.StructureByteStride = sizeof(Light);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            m_device->GetDevice()->CreateShaderResourceView(m_localLightsBuffer.Get(), &srvDesc, m_localLightsSRV.cpu);
        }
    }

    memcpy(m_localLightsBufferMapped, localLights.data(), localLights.size() * sizeof(Light));
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::RenderVisibilityPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
    const glm::mat4& viewProj,
    const std::vector<VBMeshDrawInfo>& meshDraws
) {
    if (meshDraws.empty() || m_instanceCount == 0) {
        return Result<void>::Ok(); // Nothing to draw
    }

    // Ensure descriptor heap is bound for UAV clear.
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Clear visibility buffer to 0xFFFFFFFF (background marker) via UAV to avoid
    // undefined float->uint conversions on integer RT formats.
    if (m_visibilityState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    const UINT clearValues[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    cmdList->ClearUnorderedAccessViewUint(
        m_visibilityUAV.gpu,
        m_visibilityUAV.cpu,
        m_visibilityBuffer.Get(),
        clearValues,
        0,
        nullptr
    );

    // Transition visibility buffer to render target for the visibility pass.
    if (m_visibilityState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Set render target
    cmdList->OMSetRenderTargets(1, &m_visibilityRTV.cpu, FALSE, &depthDSV);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline state and root signature (opaque, single-sided)
    cmdList->SetPipelineState(m_visibilityPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_visibilityRootSignature.Get());

    // Set instance buffer (root descriptor t0) - only needs to be set once
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress = m_instanceBuffer->GetGPUVirtualAddress();
    cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);

    // Set primitive topology
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto drawMeshRange = [&](uint32_t meshIdx,
                             const VBMeshDrawInfo& drawInfo,
                             uint32_t instanceCount,
                             uint32_t startInstance) {
        if (!drawInfo.vertexBuffer || !drawInfo.indexBuffer || drawInfo.indexCount == 0 || instanceCount == 0) {
            return;
        }

        // Set per-mesh constants (view-projection matrix + current mesh index + material count)
        struct {
            glm::mat4 viewProj;
            uint32_t meshIndex;
            uint32_t materialCount;
            uint32_t pad[2];
        } perMeshData;
        perMeshData.viewProj = viewProj;
        perMeshData.meshIndex = meshIdx;
        perMeshData.materialCount = m_materialCount;
        cmdList->SetGraphicsRoot32BitConstants(0, 20, &perMeshData, 0);

        // Set vertex buffer for this mesh
        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = drawInfo.vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = drawInfo.vertexCount * 48; // sizeof(Vertex) = 48 bytes
        vbv.StrideInBytes = 48;
        cmdList->IASetVertexBuffers(0, 1, &vbv);

        // Set index buffer for this mesh
        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = drawInfo.indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = drawInfo.indexCount * sizeof(uint32_t);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        cmdList->IASetIndexBuffer(&ibv);

        cmdList->DrawIndexedInstanced(
            drawInfo.indexCount,
            instanceCount,
            drawInfo.firstIndex,
            drawInfo.baseVertex,
            startInstance
        );
    };

    // Draw each unique mesh with only the instances that reference it (opaque, single-sided).
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
        const auto& drawInfo = meshDraws[meshIdx];
        drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCount, drawInfo.startInstance);
    }

    // Opaque, double-sided (cull none).
    if (m_visibilityPipelineDoubleSided) {
        cmdList->SetPipelineState(m_visibilityPipelineDoubleSided.Get());
        cmdList->SetGraphicsRootSignature(m_visibilityRootSignature.Get());
        cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
    }
    for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
        const auto& drawInfo = meshDraws[meshIdx];
        drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountDoubleSided, drawInfo.startInstanceDoubleSided);
    }

    // Alpha-tested visibility pass (cutout materials)
    if (m_visibilityAlphaPipeline && m_visibilityAlphaRootSignature && m_materialBuffer) {
        bool anyAlpha = false;
        for (const auto& drawInfo : meshDraws) {
            if (drawInfo.instanceCountAlpha > 0 || drawInfo.instanceCountAlphaDoubleSided > 0) {
                anyAlpha = true;
                break;
            }
        }

        if (anyAlpha) {
            cmdList->SetPipelineState(m_visibilityAlphaPipeline.Get());
            cmdList->SetGraphicsRootSignature(m_visibilityAlphaRootSignature.Get());

            cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
            cmdList->SetGraphicsRootShaderResourceView(2, m_materialBuffer->GetGPUVirtualAddress());

            for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
                const auto& drawInfo = meshDraws[meshIdx];
                drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountAlpha, drawInfo.startInstanceAlpha);
            }

            // Alpha-tested, double-sided (cull none).
            if (m_visibilityAlphaPipelineDoubleSided) {
                cmdList->SetPipelineState(m_visibilityAlphaPipelineDoubleSided.Get());
                cmdList->SetGraphicsRootSignature(m_visibilityAlphaRootSignature.Get());
                cmdList->SetGraphicsRootShaderResourceView(1, instanceBufferAddress);
                cmdList->SetGraphicsRootShaderResourceView(2, m_materialBuffer->GetGPUVirtualAddress());
            }
            for (uint32_t meshIdx = 0; meshIdx < static_cast<uint32_t>(meshDraws.size()); ++meshIdx) {
                const auto& drawInfo = meshDraws[meshIdx];
                drawMeshRange(meshIdx, drawInfo, drawInfo.instanceCountAlphaDoubleSided, drawInfo.startInstanceAlphaDoubleSided);
            }
        }
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ResolveMaterials(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthSRV,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    const glm::mat4& viewProj
) {
    if (meshDraws.empty()) {
        return Result<void>::Err("No meshes provided for material resolve");
    }
    if (depthSRV.ptr == 0) {
        return Result<void>::Err("Material resolve requires a valid depth SRV");
    }
    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Material resolve missing device or descriptor manager");
    }
    // Transition visibility buffer to shader resource
    if (m_visibilityState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_visibilityBuffer.Get();
        barrier.Transition.StateBefore = m_visibilityState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_visibilityState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    // Transition G-buffers to UAV
    D3D12_RESOURCE_BARRIER barriers[5] = {};
    int barrierCount = 0;

    if (m_albedoState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferAlbedo.Get();
        barriers[barrierCount].Transition.StateBefore = m_albedoState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_albedoState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_normalRoughnessState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_normalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_normalRoughnessState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_emissiveMetallicState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferEmissiveMetallic.Get();
        barriers[barrierCount].Transition.StateBefore = m_emissiveMetallicState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_emissiveMetallicState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_materialExt0State != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt0.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt0State;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt0State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_materialExt1State != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt1.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt1State;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt1State = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(barrierCount, barriers);
    }

    // Set compute pipeline
    cmdList->SetPipelineState(m_resolvePipeline.Get());
    cmdList->SetComputeRootSignature(m_resolveRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    // Param 0 (b0): Resolution constants + view-projection matrix + mesh index
    struct ResolutionConstants {
        uint32_t width;
        uint32_t height;
        float rcpWidth;
        float rcpHeight;
        glm::mat4 viewProj;  // 16 floats
        uint32_t materialCount;
        uint32_t meshCount;
        uint32_t pad[2];
    } resConsts;

    // Param 1 (t1): Instance buffer SRV (root descriptor)
    D3D12_GPU_VIRTUAL_ADDRESS instanceBufferAddress = m_instanceBuffer->GetGPUVirtualAddress();
    cmdList->SetComputeRootShaderResourceView(1, instanceBufferAddress);

    // Param 5 (t5): Material constants buffer SRV (root descriptor)
    resConsts.materialCount = m_materialCount;
    resConsts.meshCount = static_cast<uint32_t>(meshDraws.size());
    D3D12_GPU_VIRTUAL_ADDRESS materialBufferAddress = 0;
    if (m_materialBuffer) {
        materialBufferAddress = m_materialBuffer->GetGPUVirtualAddress();
    }
    cmdList->SetComputeRootShaderResourceView(5, materialBufferAddress);

    // Upload mesh table for this frame (bindless indices are persistent per mesh).
    if (resConsts.meshCount > 0) {
        if (resConsts.meshCount > m_maxMeshes) {
            if (m_flushCallback) {
                m_flushCallback();
            }

            uint32_t newCap = m_maxMeshes;
            while (newCap < resConsts.meshCount) {
                newCap = std::max(1u, newCap * 2u);
            }
            m_maxMeshes = newCap;

            if (m_meshTableBuffer && m_meshTableBufferMapped) {
                m_meshTableBuffer->Unmap(0, nullptr);
                m_meshTableBufferMapped = nullptr;
            }
            m_meshTableBuffer.Reset();

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC bufferDesc{};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = static_cast<UINT64>(m_maxMeshes) * sizeof(VBMeshTableEntry);
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_meshTableBuffer)
            );
            if (FAILED(hr)) {
                return Result<void>::Err("Failed to resize VB mesh table buffer");
            }
            m_meshTableBuffer->SetName(L"VB_MeshTableBuffer");

            D3D12_RANGE readRange{0, 0};
            hr = m_meshTableBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_meshTableBufferMapped));
            if (FAILED(hr) || !m_meshTableBufferMapped) {
                return Result<void>::Err("Failed to map resized VB mesh table buffer");
            }
        }

        std::vector<VBMeshTableEntry> meshEntries;
        meshEntries.resize(resConsts.meshCount);

        for (uint32_t meshIdx = 0; meshIdx < resConsts.meshCount; ++meshIdx) {
            const auto& draw = meshDraws[meshIdx];

            VBMeshTableEntry entry{};
            entry.vertexBufferIndex = draw.vertexBufferIndex;
            entry.indexBufferIndex = draw.indexBufferIndex;
            entry.vertexStrideBytes = draw.vertexStrideBytes;
            entry.indexFormat = draw.indexFormat;

            if (entry.vertexBufferIndex == 0xFFFFFFFFu || entry.indexBufferIndex == 0xFFFFFFFFu) {
                return Result<void>::Err("VB mesh table missing persistent VB/IB SRV indices (mesh upload must register SRVs)");
            }

            meshEntries[meshIdx] = entry;
        }

        if (!m_meshTableBufferMapped || !m_meshTableBuffer) {
            return Result<void>::Err("VB mesh table buffer not mapped");
        }
        memcpy(m_meshTableBufferMapped, meshEntries.data(), meshEntries.size() * sizeof(VBMeshTableEntry));
        m_meshCount = resConsts.meshCount;

        cmdList->SetComputeRootShaderResourceView(4, m_meshTableBuffer->GetGPUVirtualAddress());
    } else {
        m_meshCount = 0;
        cmdList->SetComputeRootShaderResourceView(4, 0);
    }

    // Param 2: Descriptor table with t0 (visibility) + t2 (depth).
    // Allocate a contiguous transient range (2 descriptors) and copy the SRVs into it.
    auto visDepthTableResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(2);
    if (visDepthTableResult.IsErr()) {
        return Result<void>::Err("Failed to allocate transient SRV table for visibility+depth: " + visDepthTableResult.Error());
    }

    DescriptorHandle visDepthTable = visDepthTableResult.Value();
    const UINT descriptorSize = m_device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE visDst = visDepthTable.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDst = visDepthTable.cpu;
    depthDst.ptr += static_cast<SIZE_T>(descriptorSize);

    m_device->GetDevice()->CopyDescriptorsSimple(1, visDst, m_visibilitySRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, depthDst, depthSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    cmdList->SetComputeRootDescriptorTable(2, visDepthTable.gpu);

    // Param 3: Descriptor table with u0-u4 (G-buffer UAVs).
    // Allocate a contiguous transient range and copy the persistent UAVs into it.
    auto gbufferUavTableResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(5);
    if (gbufferUavTableResult.IsErr()) {
        return Result<void>::Err("Failed to allocate transient UAV table for G-buffers: " + gbufferUavTableResult.Error());
    }

    DescriptorHandle gbufferUavTable = gbufferUavTableResult.Value();

    D3D12_CPU_DESCRIPTOR_HANDLE albedoUavDst = gbufferUavTable.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE normalUavDst = gbufferUavTable.cpu;
    normalUavDst.ptr += static_cast<SIZE_T>(descriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE emissiveUavDst = gbufferUavTable.cpu;
    emissiveUavDst.ptr += static_cast<SIZE_T>(descriptorSize) * 2;
    D3D12_CPU_DESCRIPTOR_HANDLE ext0UavDst = gbufferUavTable.cpu;
    ext0UavDst.ptr += static_cast<SIZE_T>(descriptorSize) * 3;
    D3D12_CPU_DESCRIPTOR_HANDLE ext1UavDst = gbufferUavTable.cpu;
    ext1UavDst.ptr += static_cast<SIZE_T>(descriptorSize) * 4;

    m_device->GetDevice()->CopyDescriptorsSimple(1, albedoUavDst, m_albedoUAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, normalUavDst, m_normalRoughnessUAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, emissiveUavDst, m_emissiveMetallicUAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, ext0UavDst, m_materialExt0UAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, ext1UavDst, m_materialExt1UAV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    cmdList->SetComputeRootDescriptorTable(3, gbufferUavTable.gpu);

    uint32_t dispatchX = (m_width + 7) / 8;
    uint32_t dispatchY = (m_height + 7) / 8;

    resConsts.width = m_width;
    resConsts.height = m_height;
    resConsts.rcpWidth = 1.0f / static_cast<float>(m_width);
    resConsts.rcpHeight = 1.0f / static_cast<float>(m_height);
    resConsts.viewProj = viewProj;
    cmdList->SetComputeRoot32BitConstants(0, 24, &resConsts, 0);  // 4 + 16 + 4 = 24 dwords

    // Single fullscreen dispatch; per-pixel mesh selection is driven by instance.meshIndex.
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ComputeMotionVectors(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* velocityBuffer,
    const std::vector<VBMeshDrawInfo>& meshDraws,
    D3D12_GPU_VIRTUAL_ADDRESS frameConstantsAddress
) {
    if (!cmdList || !velocityBuffer) {
        return Result<void>::Err("VB motion vectors requires a valid command list and velocity buffer");
    }
    if (!m_motionVectorsPipeline || !m_motionVectorsRootSignature) {
        return Result<void>::Err("VB motion vectors pipeline not initialized");
    }
    if (!m_instanceBuffer || m_instanceCount == 0) {
        return Result<void>::Ok();
    }
    if (!m_visibilitySRV.IsValid()) {
        return Result<void>::Err("VB motion vectors requires valid visibility SRV");
    }
    if (!m_meshTableBuffer || !m_meshTableBufferMapped) {
        return Result<void>::Err("VB motion vectors requires mesh table buffer (run ResolveMaterials first)");
    }
    if (!meshDraws.empty() && m_meshCount != static_cast<uint32_t>(meshDraws.size())) {
        // Keep this strict for now: ResolveMaterials populates the mesh table for the current frame.
        return Result<void>::Err("VB motion vectors mesh table out of date (mesh count mismatch)");
    }
    if (frameConstantsAddress == 0) {
        return Result<void>::Err("VB motion vectors requires a valid FrameConstants GPU address");
    }

    cmdList->SetPipelineState(m_motionVectorsPipeline.Get());
    cmdList->SetComputeRootSignature(m_motionVectorsRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    struct MotionConstants {
        uint32_t width;
        uint32_t height;
        float rcpWidth;
        float rcpHeight;
        uint32_t meshCount;
        uint32_t pad[3];
    } mv{};
    mv.width = m_width;
    mv.height = m_height;
    mv.rcpWidth = (m_width > 0) ? (1.0f / static_cast<float>(m_width)) : 0.0f;
    mv.rcpHeight = (m_height > 0) ? (1.0f / static_cast<float>(m_height)) : 0.0f;
    mv.meshCount = m_meshCount;
    cmdList->SetComputeRoot32BitConstants(0, 8, &mv, 0);

    cmdList->SetComputeRootConstantBufferView(1, frameConstantsAddress);

    cmdList->SetComputeRootShaderResourceView(2, m_instanceBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootShaderResourceView(3, m_meshTableBuffer->GetGPUVirtualAddress());

    // t0: visibility SRV table (copy to transient to avoid adjacency assumptions)
    auto visTableResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(1);
    if (visTableResult.IsErr()) {
        return Result<void>::Err("VB motion vectors failed to allocate transient visibility SRV: " + visTableResult.Error());
    }
    DescriptorHandle visTable = visTableResult.Value();
    m_device->GetDevice()->CopyDescriptorsSimple(
        1, visTable.cpu, m_visibilitySRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cmdList->SetComputeRootDescriptorTable(4, visTable.gpu);

    // u0: velocity UAV table (create UAV descriptor in transient heap)
    auto velUavResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(1);
    if (velUavResult.IsErr()) {
        return Result<void>::Err("VB motion vectors failed to allocate transient velocity UAV: " + velUavResult.Error());
    }
    DescriptorHandle velUav = velUavResult.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    m_device->GetDevice()->CreateUnorderedAccessView(velocityBuffer, nullptr, &uavDesc, velUav.cpu);

    cmdList->SetComputeRootDescriptorTable(5, velUav.gpu);

    const uint32_t dispatchX = (m_width + 7u) / 8u;
    const uint32_t dispatchY = (m_height + 7u) / 8u;
    cmdList->Dispatch(dispatchX, dispatchY, 1);

    return Result<void>::Ok();
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetAlbedoSRV() const {
    return m_albedoSRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetNormalRoughnessSRV() const {
    return m_normalRoughnessSRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE VisibilityBufferRenderer::GetEmissiveMetallicSRV() const {
    return m_emissiveMetallicSRV.gpu;
}

Result<void> VisibilityBufferRenderer::DebugBlitAlbedoToHDR(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV
) {
    if (!m_blitPipeline) {
        return Result<void>::Err("Blit pipeline not initialized");
    }

    // Transition albedo to shader resource
    if (m_albedoState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_gbufferAlbedo.Get();
        barrier.Transition.StateBefore = m_albedoState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_albedoState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Set HDR buffer as render target
    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set blit pipeline
    cmdList->SetPipelineState(m_blitPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_blitRootSignature.Get());

    // Set descriptor heaps (CBV/SRV/UAV heap for texture, sampler heap for sampler)
    ID3D12DescriptorHeap* heaps[] = {
        m_descriptorManager->GetCBV_SRV_UAV_Heap(),
        m_blitSamplerHeap.Get()
    };
    cmdList->SetDescriptorHeaps(2, heaps);

    // Bind albedo texture (t0)
    cmdList->SetGraphicsRootDescriptorTable(0, m_albedoSRV.gpu);

    // Bind sampler (s0)
    cmdList->SetGraphicsRootDescriptorTable(1, m_blitSamplerHeap->GetGPUDescriptorHandleForHeapStart());

    // Set primitive topology
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::BuildClusteredLightLists(
    ID3D12GraphicsCommandList* cmdList,
    const DeferredLightingParams& params
) {
    if (!cmdList) {
        return Result<void>::Err("Clustered light culling requires a valid command list");
    }

    // Disabled or no local lights for this frame.
    if (!m_clusterPipeline || !m_clusterRootSignature) {
        return Result<void>::Ok();
    }
    if (m_localLightCount == 0 || params.clusterParams.z == 0u) {
        return Result<void>::Ok();
    }
    if (!m_localLightsBuffer || !m_clusterRangesBuffer || !m_clusterLightIndicesBuffer) {
        return Result<void>::Err("Clustered light culling missing buffers");
    }

    // Keep shader/cpu configuration consistent.
    if (params.screenAndCluster.z != m_clusterCountX ||
        params.screenAndCluster.w != m_clusterCountY ||
        params.clusterParams.x != m_clusterCountZ ||
        params.clusterParams.y != m_maxLightsPerCluster)
    {
        return Result<void>::Err("Clustered light params mismatch (cluster dims/max lights)");
    }

    // Transition outputs to UAV.
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (m_clusterRangesState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterRangesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterRangesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterRangesState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (m_clusterLightIndicesState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterLightIndicesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterLightIndicesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterLightIndicesState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(static_cast<UINT>(barrierCount), barriers);
    }

    cmdList->SetPipelineState(m_clusterPipeline.Get());
    cmdList->SetComputeRootSignature(m_clusterRootSignature.Get());

    struct ClusterConstants {
        glm::mat4 viewMatrix;
        glm::vec4 projectionParams;
        glm::uvec4 screenAndCluster;
        glm::uvec4 clusterParams;
    } constants{};

    constants.viewMatrix = params.viewMatrix;
    constants.projectionParams = params.projectionParams;
    constants.screenAndCluster = params.screenAndCluster;
    constants.clusterParams = params.clusterParams;

    cmdList->SetComputeRoot32BitConstants(0, 28, &constants, 0);
    cmdList->SetComputeRootShaderResourceView(1, m_localLightsBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(2, m_clusterRangesBuffer->GetGPUVirtualAddress());
    cmdList->SetComputeRootUnorderedAccessView(3, m_clusterLightIndicesBuffer->GetGPUVirtualAddress());

    const uint32_t groups = (m_clusterCount + 63u) / 64u;
    cmdList->Dispatch(groups, 1, 1);

    // Transition to SRV for deferred shading.
    barrierCount = 0;
    if (m_clusterRangesState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterRangesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterRangesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterRangesState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_clusterLightIndicesState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_clusterLightIndicesBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_clusterLightIndicesState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_clusterLightIndicesState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(static_cast<UINT>(barrierCount), barriers);
    }

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::EnsureBRDFLUT(ID3D12GraphicsCommandList* cmdList) {
    if (m_brdfLutReady) {
        return Result<void>::Ok();
    }

    if (!cmdList) {
        return Result<void>::Err("EnsureBRDFLUT requires a valid command list");
    }
    if (!m_brdfLut || !m_brdfLutPipeline || !m_brdfLutRootSignature) {
        return Result<void>::Err("BRDF LUT resources/pipeline not initialized");
    }
    if (!m_brdfLutSRV.IsValid() || !m_brdfLutUAV.IsValid()) {
        return Result<void>::Err("BRDF LUT SRV/UAV handles invalid");
    }

    constexpr uint32_t kBrdfLutSize = 256;
    const float rcp = 1.0f / static_cast<float>(kBrdfLutSize);

    // Transition to UAV for generation.
    if (m_brdfLutState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_brdfLut.Get();
        barrier.Transition.StateBefore = m_brdfLutState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_brdfLutState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetPipelineState(m_brdfLutPipeline.Get());
    cmdList->SetComputeRootSignature(m_brdfLutRootSignature.Get());

    uint32_t constants[4];
    constants[0] = kBrdfLutSize;
    constants[1] = kBrdfLutSize;
    static_assert(sizeof(float) == sizeof(uint32_t));
    memcpy(&constants[2], &rcp, sizeof(float));
    memcpy(&constants[3], &rcp, sizeof(float));

    cmdList->SetComputeRoot32BitConstants(0, _countof(constants), constants, 0);
    cmdList->SetComputeRootDescriptorTable(1, m_brdfLutUAV.gpu);

    const uint32_t groupCount = (kBrdfLutSize + 7u) / 8u;
    cmdList->Dispatch(groupCount, groupCount, 1);

    // Transition to SRV for sampling in deferred lighting and DXR/compute consumers.
    {
        constexpr D3D12_RESOURCE_STATES kSrvState =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_brdfLut.Get();
        barrier.Transition.StateBefore = m_brdfLutState;
        barrier.Transition.StateAfter = kSrvState;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_brdfLutState = kSrvState;
    }

    m_brdfLutReady = true;
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ApplyDeferredLighting(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
    ID3D12Resource* depthBuffer,
    const DescriptorHandle& depthSRV,
    const DescriptorHandle& envDiffuseSRV,
    const DescriptorHandle& envSpecularSRV,
    const DescriptorHandle& shadowMapSRV,
    const DeferredLightingParams& params
) {
    if (!m_deferredLightingPipeline) {
        return Result<void>::Err("Deferred lighting pipeline not initialized");
    }
    if (!depthSRV.IsValid() || !envDiffuseSRV.IsValid() || !envSpecularSRV.IsValid() || !shadowMapSRV.IsValid()) {
        return Result<void>::Err("Deferred lighting requires valid depth/envDiffuse/envSpecular/shadow SRVs");
    }
    if (!m_albedoSRV.IsValid() || !m_normalRoughnessSRV.IsValid() || !m_emissiveMetallicSRV.IsValid()) {
        return Result<void>::Err("Deferred lighting requires valid G-buffer SRVs");
    }
    if (!m_materialExt0SRV.IsValid() || !m_materialExt1SRV.IsValid()) {
        return Result<void>::Err("Deferred lighting requires valid material extension G-buffer SRVs");
    }
    {
        auto brdfResult = EnsureBRDFLUT(cmdList);
        if (brdfResult.IsErr()) {
            return Result<void>::Err("Deferred lighting failed to ensure BRDF LUT: " + brdfResult.Error());
        }
    }
    if (params.clusterParams.z > 0u) {
        auto clusterResult = BuildClusteredLightLists(cmdList, params);
        if (clusterResult.IsErr()) {
            return Result<void>::Err("Deferred lighting failed to build clustered light lists: " + clusterResult.Error());
        }
    }

    // Transition G-buffers to shader resource (pixel + non-pixel) so the
    // results can be sampled by post-process and DXR without additional barriers.
    constexpr D3D12_RESOURCE_STATES kSrvState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER barriers[5] = {};
    int barrierCount = 0;

    if (m_albedoState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferAlbedo.Get();
        barriers[barrierCount].Transition.StateBefore = m_albedoState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_albedoState = kSrvState;
    }

    if (m_normalRoughnessState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_normalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_normalRoughnessState = kSrvState;
    }

    if (m_emissiveMetallicState != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferEmissiveMetallic.Get();
        barriers[barrierCount].Transition.StateBefore = m_emissiveMetallicState;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_emissiveMetallicState = kSrvState;
    }

    if (m_materialExt0State != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt0.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt0State;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt0State = kSrvState;
    }

    if (m_materialExt1State != kSrvState) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferMaterialExt1.Get();
        barriers[barrierCount].Transition.StateBefore = m_materialExt1State;
        barriers[barrierCount].Transition.StateAfter = kSrvState;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_materialExt1State = kSrvState;
    }

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(barrierCount, barriers);
    }

    // Set render target
    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline
    cmdList->SetPipelineState(m_deferredLightingPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_deferredLightingRootSignature.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = {m_descriptorManager->GetCBV_SRV_UAV_Heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    // b0: Lighting parameters (using persistent constant buffer)
    // Create CB on first use, reuse every frame
    if (!m_deferredLightingCB) {
        D3D12_HEAP_PROPERTIES uploadHeapProps = {};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = (sizeof(DeferredLightingParams) + 255) & ~255; // Align to 256 bytes
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_deferredLightingCB)
        );

        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create deferred lighting constant buffer");
        }
        m_deferredLightingCB->SetName(L"DeferredLightingCB");
    }

    // Map and update constant buffer with current frame's lighting params
    void* mapped = nullptr;
    HRESULT hr = m_deferredLightingCB->Map(0, nullptr, &mapped);
    if (SUCCEEDED(hr) && mapped) {
        memcpy(mapped, &params, sizeof(DeferredLightingParams));
        m_deferredLightingCB->Unmap(0, nullptr);
    }

    // Bind persistent CB as root CBV
    cmdList->SetGraphicsRootConstantBufferView(0, m_deferredLightingCB->GetGPUVirtualAddress());

    // NOTE: DeferredLighting.hlsl expects a stable texel size and specular max mip
    // in the constant buffer; validate here so missing CPU-side wiring fails loudly.
    if (params.shadowInvSizeAndSpecMaxMip.x <= 0.0f || params.shadowInvSizeAndSpecMaxMip.y <= 0.0f) {
        return Result<void>::Err("Deferred lighting requires valid shadowInvSize (set from shadow map dimensions)");
    }

    // t0-t5: G-buffer + depth + material extension SRVs (descriptor table)
    auto gbufferTableResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(6);
    if (gbufferTableResult.IsErr()) {
        return Result<void>::Err("Deferred lighting failed to allocate G-buffer SRV table: " + gbufferTableResult.Error());
    }

    DescriptorHandle gbufferTable = gbufferTableResult.Value();
    const UINT descriptorSize2 = m_device->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE dst0 = gbufferTable.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dst1 = gbufferTable.cpu;
    dst1.ptr += static_cast<SIZE_T>(descriptorSize2) * 1;
    D3D12_CPU_DESCRIPTOR_HANDLE dst2 = gbufferTable.cpu;
    dst2.ptr += static_cast<SIZE_T>(descriptorSize2) * 2;
    D3D12_CPU_DESCRIPTOR_HANDLE dst3 = gbufferTable.cpu;
    dst3.ptr += static_cast<SIZE_T>(descriptorSize2) * 3;
    D3D12_CPU_DESCRIPTOR_HANDLE dst4 = gbufferTable.cpu;
    dst4.ptr += static_cast<SIZE_T>(descriptorSize2) * 4;
    D3D12_CPU_DESCRIPTOR_HANDLE dst5 = gbufferTable.cpu;
    dst5.ptr += static_cast<SIZE_T>(descriptorSize2) * 5;

    m_device->GetDevice()->CopyDescriptorsSimple(1, dst0, m_albedoSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, dst1, m_normalRoughnessSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, dst2, m_emissiveMetallicSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, dst3, depthSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, dst4, m_materialExt0SRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, dst5, m_materialExt1SRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    cmdList->SetGraphicsRootDescriptorTable(1, gbufferTable.gpu);

    // t6-t9: Environment (diffuse+specular) + shadow map + BRDF LUT SRVs (descriptor table)
    auto envShadowTableResult = m_descriptorManager->AllocateTransientCBV_SRV_UAVRange(4);
    if (envShadowTableResult.IsErr()) {
        return Result<void>::Err("Deferred lighting failed to allocate env/shadow SRV table: " + envShadowTableResult.Error());
    }

    DescriptorHandle envShadowTable = envShadowTableResult.Value();
    D3D12_CPU_DESCRIPTOR_HANDLE envDst = envShadowTable.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE specDst = envShadowTable.cpu;
    specDst.ptr += static_cast<SIZE_T>(descriptorSize2);
    D3D12_CPU_DESCRIPTOR_HANDLE shadowDst = envShadowTable.cpu;
    shadowDst.ptr += static_cast<SIZE_T>(descriptorSize2) * 2;
    D3D12_CPU_DESCRIPTOR_HANDLE brdfDst = envShadowTable.cpu;
    brdfDst.ptr += static_cast<SIZE_T>(descriptorSize2) * 3;

    m_device->GetDevice()->CopyDescriptorsSimple(1, envDst, envDiffuseSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, specDst, envSpecularSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, shadowDst, shadowMapSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->GetDevice()->CopyDescriptorsSimple(1, brdfDst, m_brdfLutSRV.cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    cmdList->SetGraphicsRootDescriptorTable(2, envShadowTable.gpu);

    // t10-t12: Clustered deferred resources (root SRVs). These may be null when localLightCount==0.
    const D3D12_GPU_VIRTUAL_ADDRESS lightsVA =
        (m_localLightCount > 0 && m_localLightsBuffer) ? m_localLightsBuffer->GetGPUVirtualAddress() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS rangesVA =
        m_clusterRangesBuffer ? m_clusterRangesBuffer->GetGPUVirtualAddress() : 0;
    const D3D12_GPU_VIRTUAL_ADDRESS indicesVA =
        m_clusterLightIndicesBuffer ? m_clusterLightIndicesBuffer->GetGPUVirtualAddress() : 0;

    cmdList->SetGraphicsRootShaderResourceView(3, lightsVA);
    cmdList->SetGraphicsRootShaderResourceView(4, rangesVA);
    cmdList->SetGraphicsRootShaderResourceView(5, indicesVA);

    // Draw fullscreen triangle
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

D3D12_GPU_DESCRIPTOR_HANDLE Cortex::Graphics::VisibilityBufferRenderer::GetMaterialExt0SRV() const {
    return m_materialExt0SRV.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE Cortex::Graphics::VisibilityBufferRenderer::GetMaterialExt1SRV() const {
    return m_materialExt1SRV.gpu;
}
