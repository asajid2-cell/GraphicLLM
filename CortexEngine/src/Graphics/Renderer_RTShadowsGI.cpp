#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

void Renderer::RenderRayTracing(Scene::ECS_Registry* registry) {
    if (!m_framePlanning.rtPlan.enabled || !m_services.rayTracingContext || !registry) {
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = m_commandResources.graphicsList.As(&rtCmdList);
    if (FAILED(hr) || !rtCmdList) {
        return;
    }

    // Ensure the depth buffer is in a readable state for the DXR passes.
    // Depth resources should include DEPTH_READ when sampled as SRVs.
    if (m_depthResources.buffer && m_depthResources.resourceState != kDepthSampleState) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resourceState;
        depthBarrier.Transition.StateAfter = kDepthSampleState;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resourceState = kDepthSampleState;
    }

    // Ensure the RT shadow mask is ready for UAV writes before the DXR pass.
    if (m_rtShadowTargets.mask && m_rtShadowTargets.maskState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_rtShadowTargets.mask.Get();
        barrier.Transition.StateBefore = m_rtShadowTargets.maskState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &barrier);
        m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Set the current frame index so BLAS builds can track when they were
    // recorded. This is used by ReleaseScratchBuffers() to ensure scratch
    // buffers aren't freed until the GPU has finished using them.
    m_services.rayTracingContext->SetCurrentFrameIndex(m_frameRuntime.absoluteFrameIndex);

    // Build TLAS from the renderer's per-frame scene snapshot when available.
    // The registry path remains as a fallback for editor/debug calls outside
    // normal render orchestration.
    if (m_framePlanning.rtPlan.buildTLAS && m_framePlanning.sceneSnapshot.IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
        std::vector<DX12RaytracingContext::TLASBuildInput> tlasInputs;
        tlasInputs.reserve(std::min<uint32_t>(
            static_cast<uint32_t>(m_framePlanning.sceneSnapshot.rtCandidateIndices.size()),
            m_framePlanning.rtPlan.budget.maxTLASInstances));
        for (uint32_t entryIndex : m_framePlanning.sceneSnapshot.rtCandidateIndices) {
            if (tlasInputs.size() >= m_framePlanning.rtPlan.budget.maxTLASInstances) {
                break;
            }
            if (entryIndex >= m_framePlanning.sceneSnapshot.entries.size()) {
                continue;
            }
            const RendererSceneRenderable& sceneEntry = m_framePlanning.sceneSnapshot.entries[entryIndex];
            if (!sceneEntry.renderable || !sceneEntry.hasTransform || !sceneEntry.hasMesh) {
                continue;
            }

            DX12RaytracingContext::TLASBuildInput input{};
            input.stableId = static_cast<uint32_t>(sceneEntry.entity);
            input.renderable = sceneEntry.renderable;
            input.worldMatrix = sceneEntry.worldMatrix;
            input.maxWorldScale = GetMaxWorldScale(sceneEntry.worldMatrix);
            tlasInputs.push_back(input);
        }
        m_services.rayTracingContext->BuildTLAS(tlasInputs, rtCmdList.Get());
    } else if (m_framePlanning.rtPlan.buildTLAS) {
        m_services.rayTracingContext->BuildTLAS(registry, rtCmdList.Get());
    }

    // Dispatch the DXR sun-shadow pass when depth and mask descriptors are ready.
    if (m_framePlanning.rtPlan.dispatchShadows && m_depthResources.srv.IsValid() && m_rtShadowTargets.maskUAV.IsValid()) {
        DescriptorHandle envTable = m_environmentState.shadowAndEnvDescriptors[0];
            m_services.rayTracingContext->DispatchRayTracing(
                rtCmdList.Get(),
                m_depthResources.srv,
                m_rtShadowTargets.maskUAV,
                m_constantBuffers.currentFrameGPU,
                envTable);
    }

    // Note: RT reflections are dispatched later (after the main pass has
    // written the current frame's normal/roughness target). Dispatching
    // reflections here would sample previous-frame G-buffer data and produce
    // severe temporal instability / edge artifacts.

    // Optional RT diffuse GI: writes a low-frequency indirect lighting buffer
    // that can be sampled by the main PBR shader. As with reflections, this
    // pass is optional and disabled by default; DispatchGI is a no-op if the
    // GI pipeline is not available.
    if (m_framePlanning.rtPlan.dispatchGI && m_rtGITargets.color && m_rtGITargets.uav.IsValid()) {
        if (m_rtGITargets.colorState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER giBarrier{};
            giBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            giBarrier.Transition.pResource = m_rtGITargets.color.Get();
            giBarrier.Transition.StateBefore = m_rtGITargets.colorState;
            giBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            giBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rtCmdList->ResourceBarrier(1, &giBarrier);
            m_rtGITargets.colorState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (m_depthResources.srv.IsValid() && m_services.rayTracingContext->HasGIPipeline()) {
            DescriptorHandle envTable = m_environmentState.shadowAndEnvDescriptors[0];
            D3D12_RESOURCE_DESC giDesc = m_rtGITargets.color->GetDesc();
            const uint32_t giW = m_framePlanning.rtPlan.budget.giWidth > 0
                ? std::min(m_framePlanning.rtPlan.budget.giWidth, static_cast<uint32_t>(giDesc.Width))
                : static_cast<uint32_t>(giDesc.Width);
            const uint32_t giH = m_framePlanning.rtPlan.budget.giHeight > 0
                ? std::min(m_framePlanning.rtPlan.budget.giHeight, static_cast<uint32_t>(giDesc.Height))
                : static_cast<uint32_t>(giDesc.Height);
            m_services.rayTracingContext->DispatchGI(
                rtCmdList.Get(),
                m_depthResources.srv,
                m_rtGITargets.uav,
                m_constantBuffers.currentFrameGPU,
                envTable,
                giW,
                giH);
        }
    }
}

} // namespace Cortex::Graphics
