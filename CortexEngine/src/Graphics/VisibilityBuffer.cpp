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
    m_visibilityBuffer.Reset();
    m_gbufferAlbedo.Reset();
    m_gbufferNormalRoughness.Reset();
    m_gbufferEmissiveMetallic.Reset();
    m_instanceBuffer.Reset();
    m_visibilityPipeline.Reset();
    m_visibilityRootSignature.Reset();
    m_resolvePipeline.Reset();
    m_resolveRootSignature.Reset();

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

    // Albedo buffer (RGBA8 SRGB)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = m_width;
        desc.Height = m_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

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
        m_device->GetDevice()->CreateRenderTargetView(m_gbufferAlbedo.Get(), nullptr, m_albedoRTV.cpu);

        // Create SRV
        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) return Result<void>::Err("Failed to allocate albedo SRV");
        m_albedoSRV = srvResult.Value();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->GetDevice()->CreateShaderResourceView(m_gbufferAlbedo.Get(), &srvDesc, m_albedoSRV.cpu);

        // Create UAV
        auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (uavResult.IsErr()) return Result<void>::Err("Failed to allocate albedo UAV");
        m_albedoUAV = uavResult.Value();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // UAV can't use SRGB
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

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::CreateRootSignatures() {
    // ========================================================================
    // Visibility Pass Root Signature
    // Matches VisibilityPass.hlsl:
    //   b0: ViewProjection matrix
    //   t0: Instance data (StructuredBuffer<VBInstanceData>)
    // ========================================================================
    {
        D3D12_ROOT_PARAMETER1 params[2] = {};

        // b0: View-projection matrix (16 floats = 4x4 matrix)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 16;
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
    // Material Resolve Root Signature (Compute)
    // Matches MaterialResolve.hlsl:
    //   b0: Resolution constants (width, height, rcpWidth, rcpHeight)
    //   t0: Visibility buffer SRV (Texture2D - needs descriptor table)
    //   t1: Instance data SRV (StructuredBuffer - can use root descriptor)
    //   t2: Depth buffer SRV (Texture2D - needs descriptor table)
    //   u0-u2: G-buffer UAVs (RWTexture2D - need descriptor tables)
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

        // u0-u2: G-buffer UAVs (3 consecutive UAVs)
        D3D12_DESCRIPTOR_RANGE1 uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 3;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
        uavRange.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER1 params[4] = {};

        // b0: Resolution constants (4 uints/floats)
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 4;
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

        // u0-u2: G-buffer UAVs (descriptor table)
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &uavRange;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootDesc.Desc_1_1.NumParameters = 4;
        rootDesc.Desc_1_1.pParameters = params;
        rootDesc.Desc_1_1.NumStaticSamplers = 0;
        rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

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

    // Input layout MUST match actual Vertex structure (52 bytes)
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

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    HRESULT hr = m_instanceBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map instance buffer");
    }

    memcpy(mapped, instances.data(), instances.size() * sizeof(VBInstanceData));
    m_instanceBuffer->Unmap(0, nullptr);

    m_instanceCount = static_cast<uint32_t>(instances.size());
    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::RenderVisibilityPass(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV,
    const glm::mat4& viewProj
) {
    // Transition visibility buffer to render target
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

    // Clear visibility buffer
    UINT clearValues[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0};
    cmdList->ClearRenderTargetView(m_visibilityRTV.cpu, reinterpret_cast<float*>(clearValues), 0, nullptr);

    // Set render target
    cmdList->OMSetRenderTargets(1, &m_visibilityRTV.cpu, FALSE, &depthDSV);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    return Result<void>::Ok();
}

Result<void> VisibilityBufferRenderer::ResolveMaterials(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* depthBuffer,
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRV
) {
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
    D3D12_RESOURCE_BARRIER barriers[3] = {};
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

    if (barrierCount > 0) {
        cmdList->ResourceBarrier(barrierCount, barriers);
    }

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

} // namespace Cortex::Graphics
