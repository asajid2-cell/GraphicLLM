#include "Renderer.h"

#include "BudgetPlanner.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

Result<void> Renderer::CreateRTShadowMask() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT shadow mask creation");
    }

    UINT width = GetInternalRenderWidth();
    UINT height = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        width = static_cast<UINT>(hdrDesc.Width);
        height = hdrDesc.Height;
    }

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT shadow mask");
    }

    auto targetResult = m_rtShadowTargets.CreateResources(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        ID3D12Device* device = m_services.device->GetDevice();
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[3].cpu,
            m_rtShadowTargets.maskSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_rtShadowTargets.historySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_environmentState.shadowAndEnvDescriptors[4].cpu,
                m_rtShadowTargets.historySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    InvalidateRTShadowHistory("resource_recreated");

    return Result<void>::Ok();
}

Result<void> Renderer::CreateRTGIResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT GI creation");
    }

    UINT fullWidth = GetInternalRenderWidth();
    UINT fullHeight = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT GI buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        fullWidth,
        fullHeight);
    const UINT halfWidth = std::max<UINT>(1, fullWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, fullHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(fullHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    InvalidateRTGIHistory("resource_recreated");

    auto targetResult = m_rtGITargets.CreateResources(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid() && m_rtGITargets.srv.IsValid()) {
        ID3D12Device* device = m_services.device->GetDevice();
        device->CopyDescriptorsSimple(
            1,
            m_environmentState.shadowAndEnvDescriptors[5].cpu,
            m_rtGITargets.srv.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_rtGITargets.historySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_environmentState.shadowAndEnvDescriptors[6].cpu,
                m_rtGITargets.historySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    return Result<void>::Ok();
}

Result<void> Renderer::CreateRTReflectionResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for RT reflection creation");
    }

    UINT baseWidth = GetInternalRenderWidth();
    UINT baseHeight = GetInternalRenderHeight();

    if (m_mainTargets.hdr.resources.color) {
        D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        baseWidth = static_cast<UINT>(hdrDesc.Width);
        baseHeight = static_cast<UINT>(hdrDesc.Height);
    }

    if (baseWidth == 0 || baseHeight == 0) {
        return Result<void>::Err("Render target size is zero; cannot create RT reflection buffer");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        baseWidth,
        baseHeight);
    const UINT halfWidth = std::max<UINT>(1, baseWidth / 2u);
    const UINT halfHeight = std::max<UINT>(1, baseHeight / 2u);
    const UINT budgetWidth = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseWidth) * budget.rtResolutionScale)));
    const UINT budgetHeight = std::max<UINT>(
        1,
        static_cast<UINT>(std::floor(static_cast<float>(baseHeight) * budget.rtResolutionScale)));
    const UINT width = std::min(halfWidth, budgetWidth);
    const UINT height = std::min(halfHeight, budgetHeight);

    m_rtReflectionSignalState.ResetResources();
    auto targetResult = m_rtReflectionTargets.CreateResources(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (targetResult.IsErr()) {
        return targetResult;
    }

    auto signalStatsResult = m_rtReflectionSignalState.CreateStatsResources(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        RTReflectionSignalStats::kStatsBytes,
        RTReflectionSignalStats::kStatsWords);
    if (signalStatsResult.IsErr()) {
        m_rtReflectionTargets.ResetResources();
        return signalStatsResult;
    }

    InvalidateRTReflectionHistory("resource_recreated");

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
