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

    auto result = m_shadowResources.resources.CreateMap(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        shadowDim);
    if (result.IsErr()) {
        return result;
    }

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
