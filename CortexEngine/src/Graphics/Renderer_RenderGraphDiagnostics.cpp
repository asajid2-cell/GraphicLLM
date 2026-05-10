#include "Renderer.h"

#include "Passes/RenderGraphValidationPass.h"
#include "RenderGraph.h"
#include <algorithm>
#include <cstdlib>

namespace Cortex::Graphics {

void Renderer::AccumulateRenderGraphExecutionStats(RenderGraphPassResult* result) {
    if (!m_services.renderGraph) {
        return;
    }

    const uint32_t passCount = m_services.renderGraph->GetPassCount();
    const uint32_t barrierCount = m_services.renderGraph->GetBarrierCount();
    if (result) {
        result->graphPasses = passCount;
        result->graphBarriers = barrierCount;
    }

    m_frameDiagnostics.renderGraph.info.active = true;
    ++m_frameDiagnostics.renderGraph.info.executions;
    m_frameDiagnostics.renderGraph.info.graphPasses += passCount;
    m_frameDiagnostics.renderGraph.info.culledPasses += m_services.renderGraph->GetCulledPassCount();
    m_frameDiagnostics.renderGraph.info.barriers += barrierCount;

    const auto& transient = m_services.renderGraph->GetTransientStats();
    m_frameDiagnostics.renderGraph.info.transientResources += transient.transientResourceCount;
    m_frameDiagnostics.renderGraph.info.placedResources += transient.placedResourceCount;
    m_frameDiagnostics.renderGraph.info.aliasedResources += transient.aliasedResourceCount;
    m_frameDiagnostics.renderGraph.info.aliasingBarriers += transient.aliasBarrierCount;
    m_frameDiagnostics.renderGraph.info.transientRequestedBytes += transient.requestedBytes;
    m_frameDiagnostics.renderGraph.info.transientHeapUsedBytes += transient.heapUsedBytes;
    m_frameDiagnostics.renderGraph.info.transientHeapSizeBytes =
        std::max(m_frameDiagnostics.renderGraph.info.transientHeapSizeBytes, transient.heapSizeBytes);
    m_frameDiagnostics.renderGraph.info.transientSavedBytes += transient.savedBytes;
}

void Renderer::RunRenderGraphTransientValidation() {
    if (m_frameDiagnostics.renderGraph.transientValidationRan || std::getenv("CORTEX_RG_TRANSIENT_VALIDATE") == nullptr) {
        return;
    }
    m_frameDiagnostics.renderGraph.transientValidationRan = true;

    if (!m_services.renderGraph || !m_commandResources.graphicsList || !m_services.device || !m_services.device->GetDevice() || !m_services.descriptorManager) {
        m_frameDiagnostics.contract.contract.warnings.push_back("rg_transient_validation_prerequisites_missing");
        return;
    }

    auto rtvAResult = m_services.descriptorManager->AllocateRTV();
    auto rtvBResult = m_services.descriptorManager->AllocateRTV();
    auto srvAResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
    auto srvBResult = m_services.descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (rtvAResult.IsErr() || rtvBResult.IsErr() || srvAResult.IsErr() || srvBResult.IsErr()) {
        m_frameDiagnostics.contract.contract.warnings.push_back("rg_transient_validation_descriptor_allocation_failed");
        return;
    }

    const DescriptorHandle rtvA = rtvAResult.Value();
    const DescriptorHandle rtvB = rtvBResult.Value();
    const DescriptorHandle srvA = srvAResult.Value();
    const DescriptorHandle srvB = srvBResult.Value();

    auto createViews = [&](ID3D12Resource* resource, DescriptorHandle rtv, DescriptorHandle srv) -> bool {
        if (!resource || !rtv.IsValid() || !srv.IsValid()) {
            return false;
        }
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        m_services.device->GetDevice()->CreateRenderTargetView(resource, &rtvDesc, rtv.cpu);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_services.device->GetDevice()->CreateShaderResourceView(resource, &srvDesc, srv.cpu);
        return true;
    };

    bool stageFailed = false;
    auto failStage = [&](const char* reason) {
        if (!stageFailed) {
            m_frameDiagnostics.contract.contract.warnings.push_back(reason ? reason : "rg_transient_validation_failed");
        }
        stageFailed = true;
    };

    const RGResourceDesc transientDesc = RGResourceDesc::Texture2D(
        64,
        64,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        "RGTransientValidationTarget");

    m_services.renderGraph->BeginFrame();
    RenderGraphValidationPass::TransientValidationContext validationContext{};
    validationContext.transientDesc = transientDesc;
    validationContext.passA = [&](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph, RGResourceHandle transient) {
        ID3D12Resource* resource = graph.GetResource(transient);
        if (!createViews(resource, rtvA, srvA)) {
            failStage("rg_transient_validation_a_descriptor_failed");
            return;
        }
        const float color[4] = {0.05f, 0.15f, 0.35f, 1.0f};
        commandList->ClearRenderTargetView(rtvA.cpu, color, 0, nullptr);
    };
    validationContext.passB = [&](ID3D12GraphicsCommandList* commandList, const RenderGraph& graph, RGResourceHandle transient) {
        ID3D12Resource* resource = graph.GetResource(transient);
        if (!createViews(resource, rtvB, srvB)) {
            failStage("rg_transient_validation_b_descriptor_failed");
            return;
        }
        const float color[4] = {0.35f, 0.15f, 0.05f, 1.0f};
        commandList->ClearRenderTargetView(rtvB.cpu, color, 0, nullptr);
    };
    validationContext.failStage = failStage;

    if (!RenderGraphValidationPass::AddTransientValidation(*m_services.renderGraph, validationContext)) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        RecordFramePass("RGTransientValidation",
                        true,
                        false,
                        0,
                        {},
                        {"rg_transient_validation"},
                        true,
                        "rg_transient_validation_graph_contract",
                        true);
        m_services.renderGraph->EndFrame();
        return;
    }

    const auto execResult = m_services.renderGraph->Execute(m_commandResources.graphicsList.Get());
    AccumulateRenderGraphExecutionStats();
    if (execResult.IsErr()) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
        m_frameDiagnostics.contract.contract.warnings.push_back("rg_transient_validation_execute_failed:" + execResult.Error());
    } else if (stageFailed) {
        ++m_frameDiagnostics.renderGraph.info.fallbackExecutions;
    }
    RecordFramePass("RGTransientValidation",
                    true,
                    execResult.IsOk() && !stageFailed,
                    0,
                    {},
                    {"rg_transient_validation"},
                    execResult.IsErr() || stageFailed,
                    execResult.IsErr() ? execResult.Error().c_str() : nullptr,
                    true);
    m_services.renderGraph->EndFrame();
}

} // namespace Cortex::Graphics
