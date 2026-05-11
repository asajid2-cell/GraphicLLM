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

    auto result = m_ssaoResources.resources.CreateTarget(
        m_services.device->GetDevice(),
        m_services.descriptorManager.get(),
        width,
        height);
    if (result.IsErr()) {
        return result;
    }

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
