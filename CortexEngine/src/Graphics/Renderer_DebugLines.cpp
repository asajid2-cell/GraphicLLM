#include "Renderer.h"

#include "Graphics/Passes/DebugLinePass.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)
void Renderer::AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    DebugLineVertex v0{ a, color };
    DebugLineVertex v1{ b, color };
    m_debugLineState.lines.push_back(v0);
    m_debugLineState.lines.push_back(v1);
}

void Renderer::ClearDebugLines() {
    m_debugLineState.lines.clear();
}

void Renderer::RenderDebugLines() {
    if (m_frameLifecycle.deviceRemoved || m_debugLineState.disabled || !m_pipelineState.debugLine || m_debugLineState.lines.empty() || !m_services.window) {
        m_debugLineState.lines.clear();
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device || !m_commandResources.graphicsList) {
        m_debugLineState.lines.clear();
        return;
    }

    const UINT vertexCount = static_cast<UINT>(m_debugLineState.lines.size());

    const UINT requiredCapacity = vertexCount;
    const UINT minCapacity = 4096; // vertices

    if (m_debugLineState.NeedsVertexCapacity(requiredCapacity) && m_debugLineState.vertexBuffer) {
        WaitForGPU();
    }
    const HRESULT bufferHr = m_debugLineState.EnsureVertexBuffer(device, requiredCapacity, minCapacity);
    if (FAILED(bufferHr)) {
        spdlog::warn("RenderDebugLines: failed to allocate vertex buffer (hr=0x{:08X}); disabling debug lines for this run",
                     static_cast<unsigned int>(bufferHr));
        CORTEX_REPORT_DEVICE_REMOVED("RenderDebugLines_CreateVertexBuffer", bufferHr);
        m_debugLineState.disabled = true;
        m_debugLineState.lines.clear();
        return;
    }

    UINT bufferSize = 0;
    const HRESULT mapHr = m_debugLineState.UploadVertices(m_debugLineState.lines.data(), vertexCount, bufferSize);
    if (FAILED(mapHr)) {
        spdlog::warn("RenderDebugLines: failed to map vertex buffer (hr=0x{:08X}); disabling debug lines for this run",
                     static_cast<unsigned int>(mapHr));
        CORTEX_REPORT_DEVICE_REMOVED("RenderDebugLines_MapVertexBuffer", mapHr);
        m_debugLineState.disabled = true;
        m_debugLineState.lines.clear();
        return;
    }

    // Set pipeline state and render target (back buffer).
    ID3D12Resource* backBuffer = m_services.window->GetCurrentBackBuffer();
    if (!backBuffer) {
        m_debugLineState.lines.clear();
        return;
    }

    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    const auto objAddr = m_constantBuffers.object.AllocateAndWrite(obj);

    DebugLinePass::DrawContext drawContext{};
    drawContext.commandList = m_commandResources.graphicsList.Get();
    drawContext.rootSignature = m_pipelineState.rootSignature.get();
    drawContext.pipeline = m_pipelineState.debugLine.get();
    drawContext.state = &m_debugLineState;
    drawContext.objectConstants = objAddr;
    drawContext.vertexCount = vertexCount;
    drawContext.vertexBytes = bufferSize;

    if (DebugLinePass::Draw(drawContext)) {
        ++m_frameDiagnostics.contract.drawCounts.debugLineDraws;
        m_frameDiagnostics.contract.drawCounts.debugLineVertices += vertexCount;
    }

    // Clear for next frame.
    m_debugLineState.lines.clear();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics

