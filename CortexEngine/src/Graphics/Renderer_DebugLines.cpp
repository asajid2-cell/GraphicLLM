#include "Renderer.h"

#include "Core/Window.h"

#include <glm/glm.hpp>

// Thin forwarders to DebugLineSubsystem. The debug-line state, GPU buffer
// lifecycle, and draw logic now live in Graphics/Subsystems/DebugLineSubsystem.
namespace Cortex::Graphics {

void Renderer::AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    m_debugLines.AddLine(a, b, color);
}

void Renderer::ClearDebugLines() {
    m_debugLines.Clear();
}

void Renderer::RenderDebugLines() {
    DebugLineDrawContext ctx{};
    ctx.deviceRemoved = m_frameLifecycle.deviceRemoved;
    ctx.device = m_services.device ? m_services.device->GetDevice() : nullptr;
    ctx.commandList = m_commandResources.graphicsList.Get();
    ctx.backBuffer = m_services.window ? m_services.window->GetCurrentBackBuffer() : nullptr;
    ctx.rootSignature = m_pipelineState.rootSignature.get();
    ctx.pipeline = m_pipelineState.debugLine.get();
    ctx.allocObjectConstants = [this]() -> D3D12_GPU_VIRTUAL_ADDRESS {
        ObjectConstants obj{};
        obj.modelMatrix = glm::mat4(1.0f);
        obj.normalMatrix = glm::mat4(1.0f);
        return m_constantBuffers.object.AllocateAndWrite(obj);
    };
    ctx.waitForGpu = [this]() { WaitForGPU(); };
    ctx.reportDeviceRemoved = [this](const char* label, HRESULT hr) {
        ReportDeviceRemoved(label, hr, __FILE__, __LINE__);
    };
    ctx.outDrawCount = &m_frameDiagnostics.contract.drawCounts.debugLineDraws;
    ctx.outVertexCount = &m_frameDiagnostics.contract.drawCounts.debugLineVertices;
    m_debugLines.Render(ctx);
}

} // namespace Cortex::Graphics
