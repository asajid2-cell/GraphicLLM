#include "Renderer.h"

#include "Core/Window.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Thin forwarders to SSAOSubsystem. The SSAO target + descriptor tables and the
// immediate graphics/compute draws live in Graphics/Subsystems/SSAOSubsystem.
// The render-graph SSAO orchestration (ExecuteSSAOInRenderGraph) stays in the
// renderer and reads m_ssao.State().
namespace Cortex::Graphics {

SSAORenderContext Renderer::MakeSSAORenderContext() {
    SSAORenderContext ctx{};
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.descriptorManager = m_services.descriptorManager.get();
    ctx.graphicsRootSignature = m_pipelineState.rootSignature.get();
    ctx.ssaoPipeline = m_pipelineState.ssao.get();
    ctx.ssaoComputePipeline = m_pipelineState.ssaoCompute.get();
    ctx.compactComputeRootSignature = m_pipelineState.singleSrvUavComputeRootSignature.Get();
    ctx.computeRootSignature = m_pipelineState.computeRootSignature.get();
    ctx.frameConstants = m_constantBuffers.currentFrameGPU;
    ctx.frameIndex = m_frameRuntime.frameIndex;
    ctx.skipTransitions = m_frameDiagnostics.renderGraph.transitions.ssaoSkipTransitions;
    ctx.depthBuffer = m_depthResources.resources.buffer.Get();
    ctx.depthState = &m_depthResources.resources.resourceState;
    ctx.depthSrvValid = m_depthResources.descriptors.srv.IsValid();
    return ctx;
}

Result<void> Renderer::CreateSSAOResources() {
    if (!m_services.device || !m_services.descriptorManager || !m_services.window) {
        return Result<void>::Err("Renderer not initialized for SSAO target creation");
    }

    // Render SSAO at a budget-driven divisor of internal resolution; upsampled
    // with depth-aware filtering in post-process.
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

    return m_ssao.CreateTarget(m_services.device->GetDevice(), m_services.descriptorManager.get(), width, height);
}

void Renderer::RenderSSAO() {
    m_ssao.RenderImmediate(MakeSSAORenderContext());
}

void Renderer::RenderSSAOAsync() {
    m_ssao.RenderAsync(MakeSSAORenderContext());
}

void Renderer::SetSSAOEnabled(bool enabled) {
    if (m_ssao.State().controls.enabled == enabled) {
        return;
    }
    m_ssao.State().controls.enabled = enabled;
    spdlog::info("SSAO {}", m_ssao.State().controls.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSSAOParams(float radius, float bias, float intensity) {
    float r = glm::clamp(radius, 0.05f, 5.0f);
    float b = glm::clamp(bias, 0.0f, 0.1f);
    float i = glm::clamp(intensity, 0.0f, 4.0f);

    if (std::abs(r - m_ssao.State().controls.radius) < 1e-3f &&
        std::abs(b - m_ssao.State().controls.bias) < 1e-4f &&
        std::abs(i - m_ssao.State().controls.intensity) < 1e-3f) {
        return;
    }

    m_ssao.State().controls.radius = r;
    m_ssao.State().controls.bias = b;
    m_ssao.State().controls.intensity = i;
    spdlog::info("SSAO params set to radius={}, bias={}, intensity={}",
                 m_ssao.State().controls.radius,
                 m_ssao.State().controls.bias,
                 m_ssao.State().controls.intensity);
}

} // namespace Cortex::Graphics
