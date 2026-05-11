#include "Renderer.h"

#include "BudgetPlanner.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace Cortex::Graphics {

Result<void> Renderer::CreateShadowMapResources() {
    if (!m_services.device || !m_services.descriptorManager) {
        return Result<void>::Err("Renderer not initialized for shadow map creation");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        m_services.window ? std::max(1u, m_services.window->GetWidth()) : 0,
        m_services.window ? std::max(1u, m_services.window->GetHeight()) : 0);
    m_shadowResources.controls.mapSize = static_cast<float>(std::max(1u, budget.shadowMapSize));
    const UINT shadowDim = static_cast<UINT>(m_shadowResources.controls.mapSize);

    D3D12_RESOURCE_DESC shadowDesc = {};
    shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    shadowDesc.Width = shadowDim;
    shadowDesc.Height = shadowDim;
    shadowDesc.DepthOrArraySize = kShadowArraySize;
    shadowDesc.MipLevels = 1;
    shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

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
        &shadowDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowResources.resources.map)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create shadow map resource");
    }

    m_shadowResources.resources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    for (uint32_t i = 0; i < kShadowArraySize; ++i) {
        auto dsvResult = m_services.descriptorManager->AllocateDSV();
        if (dsvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate DSV for shadow cascade: " + dsvResult.Error());
        }
        m_shadowResources.resources.dsvs[i] = dsvResult.Value();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        m_services.device->GetDevice()->CreateDepthStencilView(
            m_shadowResources.resources.map.Get(),
            &dsvDesc,
            m_shadowResources.resources.dsvs[i].cpu
        );
    }

    auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate staging SRV for shadow map: " + srvResult.Error());
    }
    m_shadowResources.resources.srv = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = kShadowArraySize;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_shadowResources.resources.map.Get(),
        &srvDesc,
        m_shadowResources.resources.srv.cpu
    );

    m_shadowResources.raster.viewport.TopLeftX = 0.0f;
    m_shadowResources.raster.viewport.TopLeftY = 0.0f;
    m_shadowResources.raster.viewport.Width = static_cast<float>(shadowDim);
    m_shadowResources.raster.viewport.Height = static_cast<float>(shadowDim);
    m_shadowResources.raster.viewport.MinDepth = 0.0f;
    m_shadowResources.raster.viewport.MaxDepth = 1.0f;

    m_shadowResources.raster.scissor.left = 0;
    m_shadowResources.raster.scissor.top = 0;
    m_shadowResources.raster.scissor.right = static_cast<LONG>(shadowDim);
    m_shadowResources.raster.scissor.bottom = static_cast<LONG>(shadowDim);

    spdlog::info("Shadow map created ({}x{})", shadowDim, shadowDim);

    UpdateEnvironmentDescriptorTable();
    return Result<void>::Ok();
}

void Renderer::RecreateShadowMapResourcesForCurrentSize() {
    if (!m_services.device || !m_services.descriptorManager) {
        return;
    }
    if (!m_shadowResources.resources.map) {
        return;
    }

    D3D12_RESOURCE_DESC currentDesc = m_shadowResources.resources.map->GetDesc();
    const UINT desiredDim = static_cast<UINT>(m_shadowResources.controls.mapSize);

    if (currentDesc.Width <= desiredDim && currentDesc.Height <= desiredDim) {
        return;
    }

    m_shadowResources.resources.map.Reset();
    m_shadowResources.resources.srv = {};
    for (auto& dsv : m_shadowResources.resources.dsvs) {
        dsv = {};
    }

    auto result = CreateShadowMapResources();
    if (result.IsErr()) {
        spdlog::warn("Renderer: failed to recreate shadow map at safe size: {}", result.Error());
        m_shadowResources.controls.enabled = false;
    }
}

} // namespace Cortex::Graphics
