#include "VariableRateShading.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"
#include <spdlog/spdlog.h>
#include <d3dcompiler.h>

namespace Cortex::Graphics {

Result<void> VariableRateShadingManager::Initialize(
    DX12Device* device,
    DescriptorHeapManager* descriptorManager,
    uint32_t screenWidth,
    uint32_t screenHeight
) {
    if (!device || !descriptorManager) {
        return Result<void>::Err("VRS requires device and descriptor manager");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Check VRS support via D3D12_FEATURE_D3D12_OPTIONS6
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    HRESULT hr = m_device->GetDevice()->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS6,
        &options6,
        sizeof(options6)
    );

    if (FAILED(hr)) {
        spdlog::info("VRS: D3D12_OPTIONS6 not supported, VRS disabled");
        m_tier = VRSTier::NotSupported;
        return Result<void>::Ok();
    }

    // Determine VRS tier
    switch (options6.VariableShadingRateTier) {
        case D3D12_VARIABLE_SHADING_RATE_TIER_1:
            m_tier = VRSTier::Tier1;
            spdlog::info("VRS: Tier 1 supported (per-draw shading rate)");
            break;
        case D3D12_VARIABLE_SHADING_RATE_TIER_2:
            m_tier = VRSTier::Tier2;
            m_tileSize = options6.ShadingRateImageTileSize;
            spdlog::info("VRS: Tier 2 supported (image-based, tile size {})", m_tileSize);
            break;
        default:
            m_tier = VRSTier::NotSupported;
            spdlog::info("VRS: Not supported on this device");
            return Result<void>::Ok();
    }

    // If Tier 2, create VRS image
    if (m_tier == VRSTier::Tier2) {
        auto vrsResult = CreateVRSImage();
        if (vrsResult.IsErr()) {
            spdlog::warn("VRS: Failed to create VRS image: {}", vrsResult.Error());
            m_tier = VRSTier::Tier1;  // Fall back to Tier 1
        }

        auto pipelineResult = CreateComputePipeline();
        if (pipelineResult.IsErr()) {
            spdlog::warn("VRS: Failed to create compute pipeline: {}", pipelineResult.Error());
            // Continue without adaptive VRS
        }
    }

    return Result<void>::Ok();
}

void VariableRateShadingManager::Shutdown() {
    m_vrsImage.Reset();
    m_computeRootSignature.Reset();
    m_computePipeline.Reset();
    m_device = nullptr;
    m_descriptorManager = nullptr;
    m_tier = VRSTier::NotSupported;
}

Result<void> VariableRateShadingManager::Resize(uint32_t screenWidth, uint32_t screenHeight) {
    if (screenWidth == m_screenWidth && screenHeight == m_screenHeight) {
        return Result<void>::Ok();
    }

    if (m_flushCallback) {
        m_flushCallback();
    }

    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    if (m_tier == VRSTier::Tier2) {
        m_vrsImage.Reset();
        return CreateVRSImage();
    }

    return Result<void>::Ok();
}

Result<void> VariableRateShadingManager::CreateVRSImage() {
    if (m_tileSize == 0) {
        return Result<void>::Err("Invalid VRS tile size");
    }

    m_vrsWidth = (m_screenWidth + m_tileSize - 1) / m_tileSize;
    m_vrsHeight = (m_screenHeight + m_tileSize - 1) / m_tileSize;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_vrsWidth;
    desc.Height = m_vrsHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_vrsImage)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create VRS image");
    }
    m_vrsImage->SetName(L"VRS_ShadingRateImage");
    m_vrsState = D3D12_RESOURCE_STATE_COMMON;

    // Create SRV
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate VRS SRV descriptor");
    }
    m_vrsSRV = srvResult.Value();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UINT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->GetDevice()->CreateShaderResourceView(
        m_vrsImage.Get(), &srvDesc, m_vrsSRV.cpu
    );

    // Create UAV
    auto uavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (uavResult.IsErr()) {
        return Result<void>::Err("Failed to allocate VRS UAV descriptor");
    }
    m_vrsUAV = uavResult.Value();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8_UINT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->GetDevice()->CreateUnorderedAccessView(
        m_vrsImage.Get(), nullptr, &uavDesc, m_vrsUAV.cpu
    );

    spdlog::debug("VRS image created: {}x{} tiles", m_vrsWidth, m_vrsHeight);
    return Result<void>::Ok();
}

Result<void> VariableRateShadingManager::CreateComputePipeline() {
    // Create root signature for VRS compute shader
    D3D12_DESCRIPTOR_RANGE1 srvRanges[2] = {};
    // t0: Velocity buffer
    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 1;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // t1: Depth buffer
    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[1].NumDescriptors = 1;
    srvRanges[1].BaseShaderRegister = 1;
    srvRanges[1].RegisterSpace = 0;
    srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 params[3] = {};

    // Constants (thresholds, dimensions)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 8;  // width, height, tile size, thresholds
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // SRV table (velocity, depth)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = srvRanges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV (VRS image output)
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootDesc.Desc_1_1.NumParameters = 3;
    rootDesc.Desc_1_1.pParameters = params;
    rootDesc.Desc_1_1.NumStaticSamplers = 0;
    rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to serialize VRS root signature");
    }

    hr = m_device->GetDevice()->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_computeRootSignature)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create VRS root signature");
    }

    spdlog::debug("VRS compute root signature created (shader pending)");
    return Result<void>::Ok();
}

void VariableRateShadingManager::SetMode(VRSMode mode) {
    if (mode == VRSMode::ImageBased && m_tier != VRSTier::Tier2) {
        spdlog::warn("VRS: Image-based mode requires Tier 2, falling back to PerDraw");
        m_mode = VRSMode::PerDraw;
        return;
    }
    if (mode == VRSMode::Adaptive && m_tier != VRSTier::Tier2) {
        spdlog::warn("VRS: Adaptive mode requires Tier 2, falling back to PerDraw");
        m_mode = VRSMode::PerDraw;
        return;
    }
    if (mode != VRSMode::Off && m_tier == VRSTier::NotSupported) {
        spdlog::warn("VRS: Not supported, forcing Off mode");
        m_mode = VRSMode::Off;
        return;
    }

    m_mode = mode;
}

Result<void> VariableRateShadingManager::UpdateVRSImage(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* velocityBuffer,
    D3D12_GPU_DESCRIPTOR_HANDLE velocitySRV,
    ID3D12Resource* depthBuffer,
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRV
) {
    if (m_mode != VRSMode::Adaptive || m_tier != VRSTier::Tier2) {
        return Result<void>::Ok();  // Nothing to do
    }

    if (!m_computePipeline) {
        // Shader not compiled yet, fill with default rate
        return Result<void>::Ok();
    }

    // Transition VRS image to UAV
    if (m_vrsState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_vrsImage.Get();
        barrier.Transition.StateBefore = m_vrsState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
        m_vrsState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // TODO: Dispatch compute shader to generate VRS image
    // For now, just transition state

    return Result<void>::Ok();
}

void VariableRateShadingManager::BindForRendering(ID3D12GraphicsCommandList5* cmdList) {
    if (!cmdList || m_mode == VRSMode::Off || m_tier == VRSTier::NotSupported) {
        return;
    }

    // Set base shading rate
    D3D12_SHADING_RATE baseRate = static_cast<D3D12_SHADING_RATE>(m_baseShadingRate);
    D3D12_SHADING_RATE_COMBINER combiners[2] = {
        D3D12_SHADING_RATE_COMBINER_MAX,      // Combiner 0: max(provoking, primitive)
        D3D12_SHADING_RATE_COMBINER_MAX       // Combiner 1: max(result, image)
    };

    cmdList->RSSetShadingRate(baseRate, combiners);

    // Set VRS image if using image-based mode
    if ((m_mode == VRSMode::ImageBased || m_mode == VRSMode::Adaptive) && m_tier == VRSTier::Tier2) {
        // Transition VRS image to shading rate source
        if (m_vrsState != D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE) {
            // Note: This transition should be done before BindForRendering
            // The UpdateVRSImage call handles the UAV state, we need to transition here
        }

        cmdList->RSSetShadingRateImage(m_vrsImage.Get());
    }
}

void VariableRateShadingManager::UnbindForRendering(ID3D12GraphicsCommandList5* cmdList) {
    if (!cmdList || m_tier == VRSTier::NotSupported) {
        return;
    }

    // Reset to 1x1 shading rate
    D3D12_SHADING_RATE_COMBINER combiners[2] = {
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH
    };
    cmdList->RSSetShadingRate(D3D12_SHADING_RATE_1X1, combiners);
    cmdList->RSSetShadingRateImage(nullptr);
}

} // namespace Cortex::Graphics
