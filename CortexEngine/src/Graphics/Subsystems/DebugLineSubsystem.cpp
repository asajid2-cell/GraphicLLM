#include "Graphics/Subsystems/DebugLineSubsystem.h"

#include "Graphics/Passes/DebugLinePass.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void DebugLineSubsystem::AddLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    m_state.lines.push_back(DebugLineVertex{ a, color });
    m_state.lines.push_back(DebugLineVertex{ b, color });
}

void DebugLineSubsystem::Clear() {
    m_state.lines.clear();
}

void DebugLineSubsystem::Render(const DebugLineDrawContext& ctx) {
    if (ctx.deviceRemoved || m_state.disabled || !ctx.pipeline || m_state.lines.empty() ||
        !ctx.device || !ctx.commandList || !ctx.backBuffer) {
        m_state.lines.clear();
        return;
    }

    const UINT vertexCount = static_cast<UINT>(m_state.lines.size());
    const UINT requiredCapacity = vertexCount;
    const UINT minCapacity = 4096; // vertices

    if (m_state.NeedsVertexCapacity(requiredCapacity) && m_state.vertexBuffer) {
        if (ctx.waitForGpu) {
            ctx.waitForGpu();
        }
    }
    const HRESULT bufferHr = m_state.EnsureVertexBuffer(ctx.device, requiredCapacity, minCapacity);
    if (FAILED(bufferHr)) {
        spdlog::warn("RenderDebugLines: failed to allocate vertex buffer (hr=0x{:08X}); disabling debug lines for this run",
                     static_cast<unsigned int>(bufferHr));
        if (ctx.reportDeviceRemoved) {
            ctx.reportDeviceRemoved("RenderDebugLines_CreateVertexBuffer", bufferHr);
        }
        m_state.disabled = true;
        m_state.lines.clear();
        return;
    }

    UINT bufferSize = 0;
    const HRESULT mapHr = m_state.UploadVertices(m_state.lines.data(), vertexCount, bufferSize);
    if (FAILED(mapHr)) {
        spdlog::warn("RenderDebugLines: failed to map vertex buffer (hr=0x{:08X}); disabling debug lines for this run",
                     static_cast<unsigned int>(mapHr));
        if (ctx.reportDeviceRemoved) {
            ctx.reportDeviceRemoved("RenderDebugLines_MapVertexBuffer", mapHr);
        }
        m_state.disabled = true;
        m_state.lines.clear();
        return;
    }

    DebugLinePass::DrawContext drawContext{};
    drawContext.commandList = ctx.commandList;
    drawContext.rootSignature = ctx.rootSignature;
    drawContext.pipeline = ctx.pipeline;
    drawContext.state = &m_state;
    drawContext.objectConstants = ctx.allocObjectConstants ? ctx.allocObjectConstants() : 0;
    drawContext.vertexCount = vertexCount;
    drawContext.vertexBytes = bufferSize;

    if (DebugLinePass::Draw(drawContext)) {
        if (ctx.outDrawCount) {
            ++(*ctx.outDrawCount);
        }
        if (ctx.outVertexCount) {
            *ctx.outVertexCount += vertexCount;
        }
    }

    m_state.lines.clear();
}

} // namespace Cortex::Graphics
