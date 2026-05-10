#include "Renderer.h"

#include "Core/Window.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
void Renderer::PrepareMainPass() {
    // Main pass renders into HDR + normal/roughness G-buffer when available,
    // otherwise directly to back buffer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {};
    UINT numRtvs = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthResources.dsv.cpu;

    // Ensure depth buffer is in writable state for the main pass
    if (m_depthResources.buffer && m_depthResources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthResources.buffer.Get();
        depthBarrier.Transition.StateBefore = m_depthResources.resourceState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &depthBarrier);
        m_depthResources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // If the ray-traced shadow mask exists and was written by the DXR pass,
    // transition it to a shader-resource state so the PBR shader can sample it.
    if (m_rtShadowTargets.mask && m_rtShadowTargets.maskState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER rtMaskBarrier{};
        rtMaskBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtMaskBarrier.Transition.pResource = m_rtShadowTargets.mask.Get();
        rtMaskBarrier.Transition.StateBefore = m_rtShadowTargets.maskState;
        rtMaskBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        rtMaskBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &rtMaskBarrier);
        m_rtShadowTargets.maskState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Likewise, if the RT diffuse GI buffer was written by the DXR pass,
    // transition it to a shader-resource state before sampling in the PBR
    // shader.
        if (m_rtGITargets.color && m_rtGITargets.colorState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER giBarrier{};
        giBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        giBarrier.Transition.pResource = m_rtGITargets.color.Get();
        giBarrier.Transition.StateBefore = m_rtGITargets.colorState;
        giBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        giBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &giBarrier);
        m_rtGITargets.colorState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_mainTargets.hdrColor) {
        // Ensure HDR is in render target state
        if (m_mainTargets.hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_mainTargets.hdrColor.Get();
            barrier.Transition.StateBefore = m_mainTargets.hdrState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
            m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        rtvs[numRtvs++] = m_mainTargets.hdrRTV.cpu;

        // Ensure G-buffer is in render target state
        if (m_mainTargets.gbufferNormalRoughness && m_mainTargets.gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER gbufBarrier = {};
            gbufBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            gbufBarrier.Transition.pResource = m_mainTargets.gbufferNormalRoughness.Get();
            gbufBarrier.Transition.StateBefore = m_mainTargets.gbufferNormalRoughnessState;
            gbufBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            gbufBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandResources.graphicsList->ResourceBarrier(1, &gbufBarrier);
            m_mainTargets.gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        if (m_mainTargets.gbufferNormalRoughness) {
            rtvs[numRtvs++] = m_mainTargets.gbufferNormalRoughnessRTV.cpu;
        }
    } else {
        // Fallback: render directly to back buffer
        ID3D12Resource* backBuffer = m_services.window->GetCurrentBackBuffer();
        if (!backBuffer) {
            spdlog::error("PrepareMainPass: back buffer is null; skipping frame");
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
        m_frameLifecycle.backBufferUsedAsRTThisFrame = true;
        rtvs[numRtvs++] = m_services.window->GetCurrentRTV();
    }

    m_commandResources.graphicsList->OMSetRenderTargets(numRtvs, rtvs, FALSE, &dsv);

    // Clear render targets and depth buffer
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };  // Dark blue
    for (UINT i = 0; i < numRtvs; ++i) {
        m_commandResources.graphicsList->ClearRenderTargetView(rtvs[i], clearColor, 0, nullptr);
    }
    m_commandResources.graphicsList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor to match the internal render resolution when
    // using HDR (which may be supersampled relative to the window).
    D3D12_VIEWPORT viewport = {};
    D3D12_RECT scissorRect = {};
    if (m_mainTargets.hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdrColor->GetDesc();
        viewport.Width  = static_cast<float>(hdrDesc.Width);
        viewport.Height = static_cast<float>(hdrDesc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right  = static_cast<LONG>(hdrDesc.Width);
        scissorRect.bottom = static_cast<LONG>(hdrDesc.Height);
    } else {
        viewport.Width  = static_cast<float>(m_services.window->GetWidth());
        viewport.Height = static_cast<float>(m_services.window->GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right  = static_cast<LONG>(m_services.window->GetWidth());
        scissorRect.bottom = static_cast<LONG>(m_services.window->GetHeight());
    }

    m_commandResources.graphicsList->RSSetViewports(1, &viewport);
    m_commandResources.graphicsList->RSSetScissorRects(1, &scissorRect);

    // Set pipeline state and root signature
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.geometry->GetPipelineState());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);

    // Set primitive topology
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace Cortex::Graphics

