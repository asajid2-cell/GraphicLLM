#include "Renderer.h"

#include "Graphics/MeshBuffers.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    depthDesc.Width = GetInternalRenderWidth();
    depthDesc.Height = GetInternalRenderHeight();
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_services.device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthResources.resources.buffer)
    );

    if (FAILED(hr)) {
        m_depthResources.resources.buffer.Reset();
        m_depthResources.descriptors.dsv = {};
        m_depthResources.descriptors.readOnlyDsv = {};
        m_depthResources.descriptors.srv = {};

        CORTEX_REPORT_DEVICE_REMOVED("CreateDepthBuffer", hr);

        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
        char dim[64];
        sprintf_s(dim, "%llux%u", static_cast<unsigned long long>(depthDesc.Width), depthDesc.Height);
        return Result<void>::Err(std::string("Failed to create depth buffer (")
                                 + dim + ", scale=" + std::to_string(scale)
                                 + ", hr=" + buf + ")");
    }

    m_depthResources.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    // Create or rewrite DSV descriptors. Resolution changes replace the
    // resource, not the descriptor slot; preserving the slot prevents heap
    // exhaustion under adaptive render-scale changes.
    if (!m_depthResources.descriptors.dsv.IsValid()) {
        auto dsvResult = m_services.descriptorManager->AllocateDSV();
        if (dsvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate DSV: " + dsvResult.Error());
        }
        m_depthResources.descriptors.dsv = dsvResult.Value();
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    m_services.device->GetDevice()->CreateDepthStencilView(
        m_depthResources.resources.buffer.Get(),
        &dsvDesc,
        m_depthResources.descriptors.dsv.cpu
    );

    // Create a read-only DSV so we can depth-test while the depth buffer is in
    // DEPTH_READ (e.g., after VB resolve / post passes).
    if (!m_depthResources.descriptors.readOnlyDsv.IsValid()) {
        auto roDsvResult = m_services.descriptorManager->AllocateDSV();
        if (roDsvResult.IsErr()) {
            spdlog::warn("Failed to allocate read-only DSV (continuing without): {}", roDsvResult.Error());
        } else {
            m_depthResources.descriptors.readOnlyDsv = roDsvResult.Value();
        }
    }
    if (m_depthResources.descriptors.readOnlyDsv.IsValid()) {
        D3D12_DEPTH_STENCIL_VIEW_DESC roDesc = dsvDesc;
        roDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        m_services.device->GetDevice()->CreateDepthStencilView(
            m_depthResources.resources.buffer.Get(),
            &roDesc,
            m_depthResources.descriptors.readOnlyDsv.cpu
        );
    }

    // Create SRV for depth sampling (SSAO) - use staging heap for persistent descriptors
    if (!m_depthResources.descriptors.srv.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for depth buffer: " + srvResult.Error());
        }
        m_depthResources.descriptors.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_depthResources.resources.buffer.Get(),
        &depthSrvDesc,
        m_depthResources.descriptors.srv.cpu
    );

    spdlog::info("Depth buffer created");
    return Result<void>::Ok();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics
