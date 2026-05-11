#include "Renderer.h"
#include "Core/Window.h"
#include "Passes/SSAOPass.h"
#include <algorithm>

namespace Cortex::Graphics {

namespace {
constexpr D3D12_RESOURCE_STATES kDepthSampleState =
    D3D12_RESOURCE_STATE_DEPTH_READ |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

Result<void> Renderer::CreateSSAOResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for SSAO target creation");
    }

    // Render SSAO at half internal resolution for better performance; results
    // are bilinearly upsampled in post-process using depth-aware filtering.
    UINT fullWidth = GetInternalRenderWidth();
    UINT fullHeight = GetInternalRenderHeight();
    if (m_mainTargets.hdr.resources.color) {
        const D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        fullWidth = static_cast<UINT>(hdrDesc.Width);
        fullHeight = hdrDesc.Height;
    }

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create SSAO target");
    }

    const auto budget = BudgetPlanner::BuildPlan(
        m_services.device ? m_services.device->GetDedicatedVideoMemoryBytes() : 0,
        fullWidth,
        fullHeight);
    const UINT ssaoDivisor = std::max<UINT>(1, budget.ssaoDivisor);
    const UINT width = std::max<UINT>(1, fullWidth / ssaoDivisor);
    const UINT height = std::max<UINT>(1, fullHeight / ssaoDivisor);

    // Release existing target (descriptor handles remain valid and are reused)
    m_ssaoResources.resources.texture.Reset();

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    // Allow both RTV (graphics SSAO) and UAV (compute SSAO) access
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8_UNORM;
    // AO of 1.0 means no occlusion
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 1.0f;
    clearValue.Color[2] = 1.0f;
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
        IID_PPV_ARGS(&m_ssaoResources.resources.texture)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create SSAO render target");
    }

    m_ssaoResources.resources.resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV
    if (!m_ssaoResources.resources.rtv.IsValid()) {
        auto rtvResult = m_services.descriptorManager->AllocateRTV();
        if (rtvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate RTV for SSAO target: " + rtvResult.Error());
        }
        m_ssaoResources.resources.rtv = rtvResult.Value();
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateRenderTargetView(
        m_ssaoResources.resources.texture.Get(),
        &rtvDesc,
        m_ssaoResources.resources.rtv.cpu
    );

    // SRV - use staging heap for persistent SSAO SRV (copied in post-process)
    if (!m_ssaoResources.resources.srv.IsValid()) {
        auto srvResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for SSAO target: " + srvResult.Error());
        }
        m_ssaoResources.resources.srv = srvResult.Value();
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_services.device->GetDevice()->CreateShaderResourceView(
        m_ssaoResources.resources.texture.Get(),
        &srvDesc,
        m_ssaoResources.resources.srv.cpu
    );

    // UAV for async compute SSAO
    if (!m_ssaoResources.resources.uav.IsValid()) {
        auto uavResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (uavResult.IsErr()) {
            return Result<void>::Err("Failed to allocate UAV for SSAO target: " + uavResult.Error());
        }
        m_ssaoResources.resources.uav = uavResult.Value();
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_services.device->GetDevice()->CreateUnorderedAccessView(
        m_ssaoResources.resources.texture.Get(),
        nullptr,
        &uavDesc,
        m_ssaoResources.resources.uav.cpu
    );

    spdlog::info("SSAO target created: {}x{}", width, height);
    return Result<void>::Ok();
}

void Renderer::RenderSSAO() {
    if (!m_ssaoResources.controls.enabled || !m_pipelineState.ssao || !m_ssaoResources.resources.texture || !m_depthResources.resources.buffer || !m_depthResources.descriptors.srv.IsValid()) {
        return;
    }

    SSAOPass::PrepareContext prepareContext{};
    prepareContext.commandList = m_commandResources.graphicsList.Get();
    prepareContext.skipTransitions = m_frameDiagnostics.renderGraph.transitions.ssaoSkipTransitions;
    prepareContext.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState, kDepthSampleState};
    prepareContext.target = {m_ssaoResources.resources.texture.Get(), &m_ssaoResources.resources.resourceState, D3D12_RESOURCE_STATE_RENDER_TARGET};
    if (!SSAOPass::PrepareGraphicsTargets(prepareContext)) {
        spdlog::warn("RenderSSAO: target transition failed");
        return;
    }

    if (!m_ssaoResources.descriptors.descriptorTablesValid) {
        spdlog::warn("RenderSSAO: persistent SSAO descriptor tables are unavailable");
        return;
    }
    auto& depthTable = m_ssaoResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];

    if (!SSAOPass::DrawGraphics({
            m_services.device->GetDevice(),
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            m_pipelineState.rootSignature.get(),
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.ssao.get(),
            m_ssaoResources.resources.texture.Get(),
            m_ssaoResources.resources.rtv,
            m_depthResources.resources.buffer.Get(),
            std::span<DescriptorHandle>(depthTable.data(), depthTable.size()),
        })) {
        spdlog::warn("RenderSSAO: pass execution failed");
    }
}

void Renderer::SetSSAOEnabled(bool enabled) {
    if (m_ssaoResources.controls.enabled == enabled) {
        return;
    }
    m_ssaoResources.controls.enabled = enabled;
    spdlog::info("SSAO {}", m_ssaoResources.controls.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSSAOParams(float radius, float bias, float intensity) {
    float r = glm::clamp(radius, 0.05f, 5.0f);
    float b = glm::clamp(bias, 0.0f, 0.1f);
    float i = glm::clamp(intensity, 0.0f, 4.0f);

    if (std::abs(r - m_ssaoResources.controls.radius) < 1e-3f &&
        std::abs(b - m_ssaoResources.controls.bias) < 1e-4f &&
        std::abs(i - m_ssaoResources.controls.intensity) < 1e-3f) {
        return;
    }

    m_ssaoResources.controls.radius = r;
    m_ssaoResources.controls.bias = b;
    m_ssaoResources.controls.intensity = i;
    spdlog::info("SSAO params set to radius={}, bias={}, intensity={}",
                 m_ssaoResources.controls.radius,
                 m_ssaoResources.controls.bias,
                 m_ssaoResources.controls.intensity);
}

void Renderer::RenderSSAOAsync() {
    // Compute shader version of SSAO - runs on graphics queue for now
    // TODO: Move to dedicated async compute queue for true parallel execution
    if (!m_ssaoResources.controls.enabled || !m_pipelineState.ssaoCompute || !m_ssaoResources.resources.texture ||
        !m_depthResources.resources.buffer || !m_depthResources.descriptors.srv.IsValid() || !m_ssaoResources.resources.uav.IsValid()) {
        return;
    }

    SSAOPass::PrepareContext prepareContext{};
    prepareContext.commandList = m_commandResources.graphicsList.Get();
    prepareContext.skipTransitions = m_frameDiagnostics.renderGraph.transitions.ssaoSkipTransitions;
    prepareContext.depth = {m_depthResources.resources.buffer.Get(), &m_depthResources.resources.resourceState, kDepthSampleState};
    prepareContext.target = {m_ssaoResources.resources.texture.Get(), &m_ssaoResources.resources.resourceState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    if (!SSAOPass::PrepareComputeTargets(prepareContext)) {
        spdlog::warn("RenderSSAOAsync: target transition failed");
        return;
    }

    const bool compactRoot = m_pipelineState.singleSrvUavComputeRootSignature != nullptr;
    ID3D12RootSignature* ssaoRootSignature =
        compactRoot ? m_pipelineState.singleSrvUavComputeRootSignature.Get() : m_pipelineState.computeRootSignature->GetRootSignature();
    const UINT frameConstantsRoot = compactRoot ? 0u : 1u;
    const UINT srvTableRoot = compactRoot ? 1u : 3u;
    const UINT uavTableRoot = compactRoot ? 2u : 6u;
    const uint32_t srvTableSize = compactRoot ? 1u : 10u;
    const uint32_t uavTableSize = compactRoot ? 1u : 4u;

    if (!m_ssaoResources.descriptors.descriptorTablesValid) {
        spdlog::warn("RenderSSAOAsync: persistent SSAO descriptor tables are unavailable");
        return;
    }
    auto& depthTable = m_ssaoResources.descriptors.srvTables[m_frameRuntime.frameIndex % kFrameCount];
    auto& uavTable = m_ssaoResources.descriptors.uavTables[m_frameRuntime.frameIndex % kFrameCount];

    if (!SSAOPass::DispatchCompute({
            m_services.device->GetDevice(),
            m_commandResources.graphicsList.Get(),
            m_services.descriptorManager.get(),
            ssaoRootSignature,
            m_constantBuffers.currentFrameGPU,
            m_pipelineState.ssaoCompute.get(),
            frameConstantsRoot,
            srvTableRoot,
            uavTableRoot,
            m_ssaoResources.resources.texture.Get(),
            m_depthResources.resources.buffer.Get(),
            std::span<DescriptorHandle>(depthTable.data(), srvTableSize),
            std::span<DescriptorHandle>(uavTable.data(), uavTableSize),
        })) {
        spdlog::warn("RenderSSAOAsync: pass dispatch failed");
        return;
    }

    if (!SSAOPass::FinishComputeTarget(prepareContext)) {
        spdlog::warn("RenderSSAOAsync: target final transition failed");
    }
}

} // namespace Cortex::Graphics
