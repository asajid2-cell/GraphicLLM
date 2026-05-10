#include "Renderer.h"

#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateHDRTarget() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for HDR target creation");
    }

    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    const UINT width  = GetInternalRenderWidth();
    const UINT height = GetInternalRenderHeight();

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create HDR target");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_mainTargets.hdrColor)
    );

    if (FAILED(hr)) {
        m_mainTargets.hdrColor.Reset();
        m_mainTargets.hdrRTV = {};
        m_mainTargets.hdrSRV = {};

        CORTEX_REPORT_DEVICE_REMOVED("CreateHDRTarget", hr);

        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
        char dim[64];
        sprintf_s(dim, "%ux%u", width, height);
        return Result<void>::Err(std::string("Failed to create HDR color target (")
                                 + dim + ", scale=" + std::to_string(scale)
                                 + ", hr=" + buf + ")");
    }

    m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV. Reuse the descriptor slot across render-scale resizes.
    if (!m_mainTargets.hdrRTV.IsValid()) {
        auto rtvResult = m_services.descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate RTV for HDR target: " + rtvResult.Error());
        }
        m_mainTargets.hdrRTV = rtvResult.Value();
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateRenderTargetView(
        m_mainTargets.hdrColor.Get(),
        &rtvDesc,
        m_mainTargets.hdrRTV.cpu
    );

    // SRV - use staging heap for persistent descriptors
    if (!m_mainTargets.hdrSRV.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for HDR target: " + srvResult.Error());
        }
        m_mainTargets.hdrSRV = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_mainTargets.hdrColor.Get(),
        &srvDesc,
        m_mainTargets.hdrSRV.cpu
    );

    spdlog::info("HDR target created: {}x{} (scale {:.2f})", width, height, scale);

    // Normal/roughness G-buffer target (full resolution, matched to HDR)
    m_mainTargets.gbufferNormalRoughness.Reset();
    m_mainTargets.gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC gbufDesc = desc;
    gbufDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_CLEAR_VALUE gbufClear = {};
    gbufClear.Format = gbufDesc.Format;
    gbufClear.Color[0] = 0.5f; // Encoded normal (0,0,1) -> (0.5,0.5,1.0)
    gbufClear.Color[1] = 0.5f;
    gbufClear.Color[2] = 1.0f;
    gbufClear.Color[3] = 1.0f; // Roughness default

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &gbufClear,
        IID_PPV_ARGS(&m_mainTargets.gbufferNormalRoughness)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create normal/roughness G-buffer target");
    } else {
        m_mainTargets.gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        // RTV for G-buffer
        auto gbufRtvResult = m_services.descriptorManager->AllocateRTV();
        if (gbufRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for normal/roughness G-buffer: {}", gbufRtvResult.Error());
        } else {
            m_mainTargets.gbufferNormalRoughnessRTV = gbufRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC gbufRtvDesc = {};
            gbufRtvDesc.Format = gbufDesc.Format;
            gbufRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_services.device->GetDevice()->CreateRenderTargetView(
                m_mainTargets.gbufferNormalRoughness.Get(),
                &gbufRtvDesc,
                m_mainTargets.gbufferNormalRoughnessRTV.cpu
            );
        }

        // SRV for sampling G-buffer in SSR/post - use staging heap for persistent descriptors
        auto gbufSrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (gbufSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for normal/roughness G-buffer: {}", gbufSrvResult.Error());
        } else {
            m_mainTargets.gbufferNormalRoughnessSRV = gbufSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC gbufSrvDesc = {};
            gbufSrvDesc.Format = gbufDesc.Format;
            gbufSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            gbufSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            gbufSrvDesc.Texture2D.MipLevels = 1;

            m_services.device->GetDevice()->CreateShaderResourceView(
                m_mainTargets.gbufferNormalRoughness.Get(),
                &gbufSrvDesc,
                m_mainTargets.gbufferNormalRoughnessSRV.cpu
            );
        }
    }

    // (Re)create history color buffer for temporal AA in HDR space. This
    // matches the main HDR target format so TAA operates on linear lighting
    // before tonemapping and late post-effects.
    m_temporalScreenState.historyColor.Reset();
    m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_COMMON;
    InvalidateTAAHistory("resource_recreated");

    D3D12_RESOURCE_DESC historyDesc = {};
    historyDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    historyDesc.Width = width;
    historyDesc.Height = height;
    historyDesc.DepthOrArraySize = 1;
    historyDesc.MipLevels = 1;
    historyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    historyDesc.SampleDesc.Count = 1;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&m_temporalScreenState.historyColor)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create TAA history buffer");
    } else {
        m_temporalScreenState.historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        if (!m_temporalScreenState.historySRV.IsValid()) {
            // Use staging heap for persistent TAA history SRV
            auto historySrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (historySrvResult.IsErr()) {
                spdlog::warn("Failed to allocate staging SRV for TAA history: {}", historySrvResult.Error());
            } else {
                m_temporalScreenState.historySRV = historySrvResult.Value();

                D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc = {};
                historySrvDesc.Format = historyDesc.Format;
                historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                historySrvDesc.Texture2D.MipLevels = 1;

                m_services.device->GetDevice()->CreateShaderResourceView(
                    m_temporalScreenState.historyColor.Get(),
                    &historySrvDesc,
                    m_temporalScreenState.historySRV.cpu
                );
            }
        }
    }

    // (Re)create intermediate TAA resolve target (matches HDR resolution/format).
    m_temporalScreenState.taaIntermediate.Reset();
    m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC taaDesc = desc;
    D3D12_CLEAR_VALUE taaClear = {};
    taaClear.Format = taaDesc.Format;
    taaClear.Color[0] = 0.0f;
    taaClear.Color[1] = 0.0f;
    taaClear.Color[2] = 0.0f;
    taaClear.Color[3] = 1.0f;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &taaDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &taaClear,
        IID_PPV_ARGS(&m_temporalScreenState.taaIntermediate)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create TAA intermediate HDR target");
    } else {
        m_temporalScreenState.taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto taaRtvResult = m_services.descriptorManager->AllocateRTV();
        if (taaRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for TAA intermediate: {}", taaRtvResult.Error());
        } else {
            m_temporalScreenState.taaIntermediateRTV = taaRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC taaRtvDesc = {};
            taaRtvDesc.Format = taaDesc.Format;
            taaRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_services.device->GetDevice()->CreateRenderTargetView(
                m_temporalScreenState.taaIntermediate.Get(),
                &taaRtvDesc,
                m_temporalScreenState.taaIntermediateRTV.cpu
            );
        }
    }

    // (Re)create SSR color buffer (matches HDR resolution/format)
    m_ssrResources.color.Reset();
    m_ssrResources.resourceState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC ssrDesc = desc;
    ssrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_CLEAR_VALUE ssrClear = {};
    ssrClear.Format = ssrDesc.Format;
    ssrClear.Color[0] = 0.0f;
    ssrClear.Color[1] = 0.0f;
    ssrClear.Color[2] = 0.0f;
    ssrClear.Color[3] = 0.0f;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ssrDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &ssrClear,
        IID_PPV_ARGS(&m_ssrResources.color)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create SSR color buffer");
    } else {
        m_ssrResources.resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto ssrRtvResult = m_services.descriptorManager->AllocateRTV();
        if (ssrRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for SSR buffer: {}", ssrRtvResult.Error());
        } else {
            m_ssrResources.rtv = ssrRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC ssrRtvDesc = {};
            ssrRtvDesc.Format = ssrDesc.Format;
            ssrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_services.device->GetDevice()->CreateRenderTargetView(
                m_ssrResources.color.Get(),
                &ssrRtvDesc,
                m_ssrResources.rtv.cpu
            );
        }

        // Use staging heap for persistent SSR SRV (copied in post-process)
        auto ssrSrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (ssrSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for SSR buffer: {}", ssrSrvResult.Error());
        } else {
            m_ssrResources.srv = ssrSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC ssrSrvDesc = {};
            ssrSrvDesc.Format = ssrDesc.Format;
            ssrSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            ssrSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            ssrSrvDesc.Texture2D.MipLevels = 1;

            m_services.device->GetDevice()->CreateShaderResourceView(
                m_ssrResources.color.Get(),
                &ssrSrvDesc,
                m_ssrResources.srv.cpu
            );
        }
    }

    // (Re)create motion vector buffer (camera-only velocity in UV space)
    m_temporalScreenState.velocityBuffer.Reset();
    m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC velDesc = desc;
    velDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    velDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_CLEAR_VALUE velClear = {};
    velClear.Format = velDesc.Format;
    velClear.Color[0] = 0.0f;
    velClear.Color[1] = 0.0f;
    velClear.Color[2] = 0.0f;
    velClear.Color[3] = 0.0f;

    hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &velDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &velClear,
        IID_PPV_ARGS(&m_temporalScreenState.velocityBuffer)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create motion vector buffer");
    } else {
        m_temporalScreenState.velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto velRtvResult = m_services.descriptorManager->AllocateRTV();
        if (velRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for motion vector buffer: {}", velRtvResult.Error());
        } else {
            m_temporalScreenState.velocityRTV = velRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC velRtvDesc = {};
            velRtvDesc.Format = velDesc.Format;
            velRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_services.device->GetDevice()->CreateRenderTargetView(
                m_temporalScreenState.velocityBuffer.Get(),
                &velRtvDesc,
                m_temporalScreenState.velocityRTV.cpu
            );
        }

        // Use staging heap for persistent velocity SRV (used in TAA)
        auto velSrvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (velSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for motion vector buffer: {}", velSrvResult.Error());
        } else {
            m_temporalScreenState.velocitySRV = velSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC velSrvDesc = {};
            velSrvDesc.Format = velDesc.Format;
            velSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            velSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            velSrvDesc.Texture2D.MipLevels = 1;

            m_services.device->GetDevice()->CreateShaderResourceView(
                m_temporalScreenState.velocityBuffer.Get(),
                &velSrvDesc,
                m_temporalScreenState.velocitySRV.cpu
            );
        }
    }

    auto temporalMaskResult = CreateTemporalRejectionMaskResources();
    if (temporalMaskResult.IsErr()) {
        spdlog::warn("Failed to create temporal rejection mask: {}", temporalMaskResult.Error());
    }

    // (Re)create bloom render targets that depend on HDR size
    auto bloomResult = CreateBloomResources();
    if (bloomResult.IsErr()) {
        spdlog::warn("Failed to create bloom resources: {}", bloomResult.Error());
    }

    // SSAO target depends on window size as well
    auto ssaoResult = CreateSSAOResources();
    if (ssaoResult.IsErr()) {
        spdlog::warn("Failed to create SSAO resources: {}", ssaoResult.Error());
    }

    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
